#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iomanip>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/select.h>

#include "../include/UAP_header.h"

using namespace std;

// Constants
const int BUFFER_SIZE = 2048;
const int SESSION_TIMEOUT_SECONDS = 10;

struct Session {
    struct sockaddr_in client_addr;
    uint32_t expected_seq_num;
    time_t last_message_time;
    double total_latency;
    int packet_count;
};

// Global server state
map<uint32_t, Session> sessions;
uint64_t server_logical_clock = 0;
uint32_t server_sequence_number = 0;

// Function Prototypes
void print_hex(uint32_t val);
void send_uap_message(int sockfd, const struct sockaddr_in& addr, uint32_t session_id, uint8_t command, const string& payload = "");
uint64_t get_current_microseconds();
void close_session(int sockfd, uint32_t session_id, bool notify_client);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <portnum>" << endl;
        return 1;
    }
    int port = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        close(sockfd);
        return 1;
    }

    cout << "Waiting on port " << port << "..." << endl;

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select error");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            string line;
            if (getline(cin, line)) {
                if (line == "q") {
                    cout << "Server shutting down by user command." << endl;
                    break; 
                }
            } else { // EOF detected
                cout << "Server shutting down by EOF on stdin." << endl;
                break;
            }
        }

        if (FD_ISSET(sockfd, &readfds)) {
            char buffer[BUFFER_SIZE];
            struct sockaddr_in cli_addr;
            socklen_t clilen = sizeof(cli_addr);

            int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&cli_addr, &clilen);
            if (n < 0) {
                perror("ERROR in recvfrom");
                continue;
            }

            if (n < sizeof(UAP_header)) {
                continue;
            }
            
            uint64_t reception_time = get_current_microseconds();

            UAP_header* header = (UAP_header*)buffer;

            if (ntohs(header->magic) != UAP_MAGIC || header->version != UAP_VERSION) {
                continue;
            }

            server_logical_clock = max(server_logical_clock, ntohll(header->logical_clock)) + 1;
            
            // Calculate and Print One-Way Latency
            uint64_t send_timestamp = ntohll(header->timestamp);
            double latency_ms = (reception_time - send_timestamp) / 1000.0;
            print_hex(ntohl(header->session_id));
            cout << " [" << ntohl(header->sequence_number) << "] Latency: " << fixed << setprecision(2) << latency_ms << " ms" << endl;

            uint32_t session_id = ntohl(header->session_id);
            uint32_t client_seq_num = ntohl(header->sequence_number);
            uint8_t command = header->command;
            
            auto it = sessions.find(session_id);
            if (it == sessions.end()) {
                if (command == UAP_COMMAND_HELLO) {
                    print_hex(session_id);
                    cout << " [" << client_seq_num << "] Session created" << endl;

                    sessions[session_id] = {cli_addr, 1, time(nullptr), latency_ms, 1};
                    
                    send_uap_message(sockfd, cli_addr, session_id, UAP_COMMAND_HELLO);
                } else {
                    // Per FSA, initial message must be HELLO, otherwise terminate
                    // We don't have a session to terminate, so we just ignore.
                }
            } else {
                Session& session = it->second;
                session.last_message_time = time(nullptr);
                
                session.total_latency += latency_ms;
                session.packet_count++;
                
                switch (command) {
                    case UAP_COMMAND_DATA: {
                        if (client_seq_num < session.expected_seq_num) {
                            // "from the past", protocol error, close session
                             print_hex(session_id);
                             cout << " [" << client_seq_num << "] Out-of-order packet. Closing session." << endl;
                             close_session(sockfd, session_id, true);
                             continue;
                        }
                        if (client_seq_num == session.expected_seq_num - 1) {
                            // Duplicate packet
                            print_hex(session_id);
                            cout << " [" << client_seq_num << "] Duplicate packet received." << endl;
                            // Discard the packet, don't send ALIVE
                            continue;
                        }
                        while (client_seq_num > session.expected_seq_num) {
                            // Lost packets
                            print_hex(session_id);
                            cout << " [" << session.expected_seq_num << "] Lost packet!" << endl;
                            session.expected_seq_num++;
                        }

                        // Print payload
                        size_t payload_len = n - sizeof(UAP_header);
                        string payload(buffer + sizeof(UAP_header), payload_len);
                        print_hex(session_id);
                        cout << " [" << client_seq_num << "] " << payload << endl;
                        
                        session.expected_seq_num = client_seq_num + 1;

                        // Respond with ALIVE
                        send_uap_message(sockfd, cli_addr, session_id, UAP_COMMAND_ALIVE);
                        break;
                    }
                    case UAP_COMMAND_GOODBYE: {
                        print_hex(session_id);
                        cout << " [" << client_seq_num << "] GOODBYE from client." << endl;
                        close_session(sockfd, session_id, true); // send GOODBYE back
                        break;
                    }
                    case UAP_COMMAND_HELLO:
                    default: {
                        // Protocol error: e.g., HELLO received in established session
                        print_hex(session_id);
                        cout << " [" << client_seq_num << "] Protocol error. Closing session." << endl;
                        close_session(sockfd, session_id, true);
                        break;
                    }
                }
            }
        }

        // Check for Session Timeouts (Garbage Collection)
        time_t now = time(nullptr);
        vector<uint32_t> timed_out_sessions;
        for (auto const& [id, sess] : sessions) {
            if (now - sess.last_message_time > SESSION_TIMEOUT_SECONDS) {
                timed_out_sessions.push_back(id);
            }
        }
        for (uint32_t id : timed_out_sessions) {
            print_hex(id);
            cout << " Session timed out." << endl;
            close_session(sockfd, id, true);
        }
    }

    // Server Shutdown: Send GOODBYE to all active sessions
    cout << "Notifying active clients of shutdown..." << endl;
    for (auto const& [id, sess] : sessions) {
        send_uap_message(sockfd, sess.client_addr, id, UAP_COMMAND_GOODBYE);
    }
    sessions.clear();

    close(sockfd);
    return 0;
}


void print_hex(uint32_t val) {
    cout << "0x" << hex << setw(8) << setfill('0') << val << dec;
}

void send_uap_message(int sockfd, const struct sockaddr_in& addr, uint32_t session_id, uint8_t command, const string& payload) {
    size_t buffer_len = sizeof(UAP_header) + payload.length();
    char buffer[buffer_len];

    UAP_header header;
    header.magic = htons(UAP_MAGIC);
    header.version = UAP_VERSION;
    header.command = command;
    header.sequence_number = htonl(server_sequence_number++);
    header.session_id = htonl(session_id);

    server_logical_clock++;
    header.logical_clock = htonll(server_logical_clock);
    header.timestamp = htonll(get_current_microseconds());

    memcpy(buffer, &header, sizeof(UAP_header));
    memcpy(buffer + sizeof(UAP_header), payload.c_str(), payload.length());

    sendto(sockfd, buffer, buffer_len, 0, (const struct sockaddr*)&addr, sizeof(addr));
}

uint64_t get_current_microseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

void close_session(int sockfd, uint32_t session_id, bool notify_client) {
    auto it = sessions.find(session_id);
    if (it != sessions.end()) {
        if (notify_client) {
            send_uap_message(sockfd, it->second.client_addr, session_id, UAP_COMMAND_GOODBYE);
        }
        
        // Print average latency for the closed session
        double avg_latency = it->second.total_latency / it->second.packet_count;
        print_hex(session_id);
        cout << " Session closed (Avg Latency: " << fixed << setprecision(2) << avg_latency << " ms)" << endl;
        
        sessions.erase(it);
    }
}
