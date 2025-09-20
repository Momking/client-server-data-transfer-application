#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include <mutex>

#include "../include/UAP_header.h"

using namespace std;

// Shared resources
atomic<bool> running(true);
atomic<bool> shutting_down(false);
mutex mtx;
int32_t session_id;
int32_t sequence_number = 0;
int64_t logical_clock_val = 0;
atomic<bool> server_responded(false);


// Function to pack data into a buffer
void pack(char* buff, const string& payload, uint8_t command) {
    UAP_header header;
    header.magic = htons(UAP_MAGIC);
    header.version = UAP_VERSION;
    header.command = command;
    header.sequence_number = htonl(sequence_number);
    header.session_id = htonl(session_id);
    header.logical_clock = htonll(logical_clock_val);
    header.timestamp = htonll(chrono::duration_cast<chrono::nanoseconds>(chrono::system_clock::now().time_since_epoch()).count());

    memcpy(buff, &header, sizeof(UAP_header));
    memcpy(buff + sizeof(UAP_header), payload.c_str(), payload.length());
}

// Thread function for sending messages
void send_thread(int sock, struct sockaddr_in server_addr) {
    // Send HELLO message
    {
        lock_guard<mutex> lock(mtx);
        logical_clock_val++;
        char buffer[sizeof(UAP_header)];
        pack(buffer, "", UAP_COMMAND_HELLO);
        sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        cout << "Sent HELLO" << endl;
    }

    string line;
    bool eof_received = false;
    while (running && getline(cin, line)) {
        if (line == "q") {
            break;
        }

        lock_guard<mutex> lock(mtx);
        logical_clock_val++;
        sequence_number++;
        char buffer[sizeof(UAP_header) + line.length()];
        pack(buffer, line, UAP_COMMAND_DATA);
        sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    }
    
    // Check if loop ended due to EOF
    if (cin.eof()) {
        eof_received = true;
    }

    // Signal that we are entering the "Closing" state
    shutting_down = true; 

    // Send the final GOODBYE message
    {
        lock_guard<mutex> lock(mtx);
        logical_clock_val++;
        sequence_number++;
        char buffer[sizeof(UAP_header)];
        pack(buffer, "", UAP_COMMAND_GOODBYE);
        sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (eof_received) {
            cout << "eof" << endl;
        }
        cout << "Sent GOODBYE, waiting for reply..." << endl;
    }
    // The send_thread's job is now done. It does not control the 'running' flag anymore.
}

// Thread function for receiving messages
void receive_thread(int sock) {
    char buffer[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // The receive thread now controls the main 'running' loop
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 5; 
        timeout.tv_usec = 0;

        int ret = select(sock + 1, &read_fds, NULL, NULL, &timeout);

        if (ret < 0) {
            if (running) perror("select");
            break;
        } else if (ret == 0) { // Timeout occurred
            if (shutting_down) {
                 cout << "Timeout waiting for GOODBYE reply. Closing." << endl;
                 running = false; // Exit after timeout in "Closing" state
            } else if (!server_responded) {
                cerr << "Timeout: No initial response from server." << endl;
                running = false; // Exit if server never responded at all
            }
            // If neither of the above, it's just an idle timeout, which is fine.
            continue; 
        }

        if (FD_ISSET(sock, &read_fds)) {
            int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
            if (n <= 0) {
                 // Socket was likely closed by shutdown() in main
                if (running) running = false;
                break;
            }

            if (n >= sizeof(UAP_header)) {
                UAP_header* header = (UAP_header*)buffer;
                lock_guard<mutex> lock(mtx);
                if (ntohs(header->magic) == UAP_MAGIC && header->version == UAP_VERSION) {
                    server_responded = true; 
                    logical_clock_val = max(logical_clock_val, (int64_t)ntohll(header->logical_clock)) + 1;
                    
                    if (header->command == UAP_COMMAND_HELLO) {
                        cout << "Received HELLO reply" << endl;
                    } else if (header->command == UAP_COMMAND_ALIVE) {
                        cout << "Received ALIVE" << endl;
                    } else if (header->command == UAP_COMMAND_GOODBYE) {
                        cout << "Received GOODBYE from server. Closing." << endl;
                        running = false; 
                    }
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <hostname> <portnum>" << endl;
        return 1;
    }

    const char* hostname = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    srand(time(0) ^ getpid());
    session_id = rand();

    thread sender(send_thread, sock, server_addr);
    thread receiver(receive_thread, sock);

    sender.join();
    // After the sender is done, the receiver might still be waiting. 
    // The receiver will now terminate based on its own logic (receiving GOODBYE or timeout).
    // The shutdown call helps unblock it from a blocking recvfrom call if needed.
    shutdown(sock, SHUT_RDWR);
    receiver.join();

    close(sock);
    return 0;
}
