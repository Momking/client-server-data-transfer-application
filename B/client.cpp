#include <stdint.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <arpa/inet.h>
#include "../include/UAP_header.h"
#include "../include/pack.h"
#include "../include/unpack.h"

using namespace std;
using namespace std::chrono;
auto deadline = steady_clock::now() + seconds(10);

UAP_header last_header;

int32_t sequence = 0;
int64_t clk = 0;
int64_t get_current_time() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

enum state { HELLO, HELLO_WAIT, READY, READY_TIMER, CLOSING, CLOSED };
state current_state = HELLO;

int32_t sessionID = getpid();

int main(int argc, char* argv[]) {
    char* server_ip = argv[1];
    int server_port = atoi(argv[2]);


    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket < 0) {
        cout << "Failed to create socket" << endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(clientSocket, &readfds);

    char buffer[sizeof(UAP_header)];
    clk = max(clk, last_header.logical_clock) + 1;
    pack(buffer, "", UAP_COMMAND_HELLO, sequence++, sessionID, clk, get_current_time());
    int send = sendto(clientSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    current_state = HELLO_WAIT;
    if (send < 0) {
        cout << "Failed to send HELLO" << endl;
        close(clientSocket);
        return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    int activity = select(clientSocket + 1, &readfds, NULL, NULL, &timeout);
    if(FD_ISSET(clientSocket, &readfds)) {
        char buffer[sizeof(UAP_header)];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int n = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
        if(n < 0) { perror("recvfrom"); close(clientSocket); return 1; }

        string payload = "";
        UAP_header header;
        if(!unPack(buffer, n, header, payload)) {
            cout << "Failed to unpack message" << endl;
            close(clientSocket);
            return 1;
        }

        if(header.magic != UAP_MAGIC || header.version != UAP_VERSION) {
            close(clientSocket);
            return 1;
        }

        if(header.command == UAP_COMMAND_HELLO) {
            cout << "Received HELLO" << endl;
            current_state = READY;
        }else{
            cout << "Unexpected response to HELLO" << endl;
            close(clientSocket);
            return 1;
        }

        cout << "Timestamp: " << header.timestamp << endl;
        cout << "Logical Clock: " << header.logical_clock << endl;
        last_header = header;
    }else{
        cout << "Timeout waiting for HELLO response" << endl;
        close(clientSocket);
        return 1;
    }

    while(true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(clientSocket, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        auto now = steady_clock::now();
        struct timeval timeout;

        if (now < deadline) {
            auto remaining_time = duration_cast<microseconds>(deadline - now);
            timeout.tv_sec = remaining_time.count() / 1000000;
            timeout.tv_usec = remaining_time.count() % 1000000;
        } else {
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
            break;
        }

        int max_fd = max(clientSocket, STDIN_FILENO);
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        string input_buffer;
        if(FD_ISSET(STDIN_FILENO, &readfds) && getline(cin, input_buffer)) {
            if(input_buffer == "q") {
                current_state = CLOSING;
                break;
            }else{
                char buffer[sizeof(UAP_header) + input_buffer.size()];
                clk = max(clk, last_header.logical_clock) + 1;
                pack(buffer, input_buffer, UAP_COMMAND_DATA, sequence++, sessionID, clk, get_current_time());
                int send = sendto(clientSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                if(send < 0) { perror("sendto"); break; }
                current_state = READY_TIMER;
                cout << "Sent DATA: " << input_buffer << endl;
            }
        }else if (cin.eof()) {
            current_state = CLOSING;
            break;
        }

        if(FD_ISSET(clientSocket, &readfds)) {
            deadline = steady_clock::now() + seconds(10);
            // cout << "Resetting deadline to 10 seconds from now." << endl;
            char buffer[1024];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            int n = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
            if(n < 0) { perror("recvfrom"); break; }

            string payload = "";
            UAP_header header;
            if(!unPack(buffer, n, header, payload)) {
                cout << "Failed to unpack message" << endl;
                continue;
            }

            if(header.magic != UAP_MAGIC || header.version != UAP_VERSION) {
                continue;
            }

            if(header.command == UAP_COMMAND_ALIVE) {
                cout << "Received ALIVE" << endl;
                if(current_state == READY_TIMER) {
                    current_state = READY;
                }
            }else if(header.command == UAP_COMMAND_GOODBYE) {
                cout << "Received GOODBYE" << endl;
                current_state = CLOSING;
            }

            cout << "Timestamp: " << header.timestamp << endl;
            cout << "Logical Clock: " << header.logical_clock << endl;
            last_header = header;
        }
    }
    
    char buffer2[sizeof(UAP_header)];
    clk = max(clk, last_header.logical_clock) + 1;
    pack(buffer2, "", UAP_COMMAND_GOODBYE, sequence++, sessionID, clk, get_current_time());
    send = sendto(clientSocket, buffer2, sizeof(buffer2), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (send < 0) {
        cout << "Failed to send GOODBYE" << endl;
    }
    current_state = CLOSED;

    close(clientSocket);

    return 0;
}