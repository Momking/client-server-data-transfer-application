#include <iostream>
#include <string>
#include <map>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <chrono>
#include <algorithm>

#include "../include/UAP_header.h"

using namespace std;

struct Session {
    struct sockaddr_in client_addr;
    int last_sequence_number = -1;
    chrono::steady_clock::time_point last_message_time;
};

map<int32_t, Session> sessions;
int64_t logical_clock_val = 0;

// Function to unpack data from a buffer
bool unpack(const char* buffer, int n, UAP_header& header, string& payload) {
    if (n < sizeof(UAP_header)) {
        return false;
    }
    const UAP_header* received_header = (const UAP_header*)buffer;

    header.magic = ntohs(received_header->magic);
    header.version = received_header->version;
    header.command = received_header->command;
    header.sequence_number = ntohl(received_header->sequence_number);
    header.session_id = ntohl(received_header->session_id);
    header.logical_clock = ntohll(received_header->logical_clock);
    header.timestamp = ntohll(received_header->timestamp);

    if (header.magic != UAP_MAGIC || header.version != UAP_VERSION) {
        return false;
    }

    int payload_size = n - sizeof(UAP_header);
    if (payload_size > 0) {
        payload.assign(buffer + sizeof(UAP_header), payload_size);
    } else {
        payload.clear();
    }
    return true;
}

// Function to send a response to the client
void send_response(int sock, const struct sockaddr_in& client_addr, uint8_t command, int32_t session_id) {
    UAP_header response_header;
    response_header.magic = htons(UAP_MAGIC);
    response_header.version = UAP_VERSION;
    response_header.command = command;
    response_header.sequence_number = htonl(0); // Server sequence number not specified in detail
    response_header.session_id = htonl(session_id);
    logical_clock_val++;
    response_header.logical_clock = htonll(logical_clock_val);
    response_header.timestamp = htonll(chrono::duration_cast<chrono::nanoseconds>(chrono::system_clock::now().time_since_epoch()).count());

    sendto(sock, &response_header, sizeof(response_header), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <portnum>" << endl;
        return 1;
    }

    int port = atoi(argv[1]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    cout << "Waiting on port " << port << "..." << endl;

    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 35; // Slightly longer timeout
        timeout.tv_usec = 0;

        int max_fd = max(sock, STDIN_FILENO);

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ret < 0) {
            perror("select");
            break;
        }

        // Check for stdin activity
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            string line;
            getline(cin, line);
            if (line == "q" || cin.eof()) {
                cout << "Shutdown signal received from stdin. Sending GOODBYE to all clients." << endl;
                for (auto const& [id, session] : sessions) {
                    send_response(sock, session.client_addr, UAP_COMMAND_GOODBYE, id);
                }
                break;
            }
        }

        // Check for network activity
        if (FD_ISSET(sock, &read_fds)) {
            char buffer[2048];
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);

            if (n < 0) {
                perror("recvfrom");
                continue;
            }

            UAP_header header;
            string payload;
            if (unpack(buffer, n, header, payload)) {
                logical_clock_val = max(logical_clock_val, header.logical_clock) + 1;
                auto it = sessions.find(header.session_id);

                if (it == sessions.end()) {
                    if (header.command == UAP_COMMAND_HELLO) {
                        cout << hex << "0x" << header.session_id << " [" << dec << header.sequence_number << "] Session created" << endl;
                        sessions[header.session_id] = {client_addr, header.sequence_number, chrono::steady_clock::now()};
                        send_response(sock, client_addr, UAP_COMMAND_HELLO, header.session_id);
                    }
                } else {
                    it->second.last_message_time = chrono::steady_clock::now();
                    switch (header.command) {
                        case UAP_COMMAND_DATA:
                            if (header.sequence_number > it->second.last_sequence_number + 1) {
                                for (int i = it->second.last_sequence_number + 1; i < header.sequence_number; ++i) {
                                    cout << hex << "0x" << header.session_id << " [" << dec << i << "] Lost packet!" << endl;
                                }
                            }
                            // Process data packet only if it's new
                            if (header.sequence_number > it->second.last_sequence_number) {
                                cout << hex << "0x" << header.session_id << " [" << dec << header.sequence_number << "] " << payload << endl;
                                it->second.last_sequence_number = header.sequence_number;
                            } else {
                                cout << hex << "0x" << header.session_id << " [" << dec << header.sequence_number << "] Duplicate packet" << endl;
                            }
                            send_response(sock, client_addr, UAP_COMMAND_ALIVE, header.session_id);
                            break;
                        case UAP_COMMAND_GOODBYE:
                            cout << hex << "0x" << header.session_id << " [" << dec << header.sequence_number << "] GOODBYE from client." << endl;
                            send_response(sock, client_addr, UAP_COMMAND_GOODBYE, header.session_id); 
                            cout << hex << "0x" << header.session_id << " Session closed" << endl;
                            sessions.erase(it);
                            break;
                        case UAP_COMMAND_HELLO:
                             // This is a protocol error according to the FSA
                             cout << hex << "0x" << header.session_id << " Protocol error: HELLO received in active session. Closing session." << endl;
                             send_response(sock, client_addr, UAP_COMMAND_GOODBYE, header.session_id);
                             sessions.erase(it);
                             break;
                        default:
                            break;
                    }
                }
            }
        }
        
        // Check for session timeouts
        auto now = chrono::steady_clock::now();
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (chrono::duration_cast<chrono::seconds>(now - it->second.last_message_time).count() > 30) {
                cout << hex << "0x" << it->first << " Session timed out." << endl;
                send_response(sock, it->second.client_addr, UAP_COMMAND_GOODBYE, it->first);
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    close(sock);
    return 0;
}
