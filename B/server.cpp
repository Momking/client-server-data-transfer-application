#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <map>
#include <queue>
#include <utility>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <arpa/inet.h>
#include <string>
#include <chrono>
#include <atomic>
#include <fcntl.h>
#include "../include/UAP_header.h"
#include "../include/pack.h"
#include "../include/unpack.h"

using namespace std;
using namespace std::chrono;

int32_t global_squence_no = 0;
mutex global_mutex;

class sessions;
void handle_session(sessions&);
atomic<bool> quitFlag(false);

map<int32_t, unique_ptr<sessions>> session_threads;
int64_t clk = 0;
int64_t get_current_time() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

class sessions {
public:
    int32_t session_id;
    int server_socket;
    sockaddr_in client_addr;
    thread session_thread;
    UAP_header last_header;
    mutex session_mutex;
    atomic<bool> is_done{false};
    steady_clock::time_point timeout_counter;

    queue<pair<UAP_header, string>> message_queue;

    sessions(int32_t id, int sock, sockaddr_in addr, UAP_header header) : session_id(id), server_socket(sock), client_addr(addr) {
        last_header = header;
        last_header.session_id = id;
        session_thread = thread(handle_session, ref(*this));
    }
};

void handle_session(sessions &s) {
    char buffer[sizeof(UAP_header)];
    {
        lock_guard<mutex> lock(global_mutex);
        clk = max(clk, s.last_header.logical_clock) + 1;
        pack(buffer, "", UAP_COMMAND_HELLO, s.last_header.sequence_number, s.session_id, clk, get_current_time());
        global_squence_no++;
    }
    s.timeout_counter = steady_clock::now();
    int send = sendto(s.server_socket, buffer, sizeof(UAP_header), 0, (struct sockaddr*)&s.client_addr, sizeof(s.client_addr));
    if(send < 0) { perror("sendto"); s.is_done = true; return; }

    while(true) {
        if(!s.message_queue.empty()) {
            UAP_header head;
            string payload = "";
            {
                lock_guard<mutex> lock(s.session_mutex);
                auto [h, p] = s.message_queue.front();
                head = h;
                payload = p;
                s.message_queue.pop();
            }

            if(head.sequence_number != s.last_header.sequence_number + 1) {
                if(head.sequence_number < s.last_header.sequence_number) {
                    char buffer[sizeof(UAP_header)];
                    {
                        lock_guard<mutex> lock(global_mutex);
                        clk = max(clk, head.logical_clock) + 1;
                        pack(buffer, "", UAP_COMMAND_GOODBYE, global_squence_no, s.session_id, clk, get_current_time());
                        global_squence_no++;
                    }
                    int send = sendto(s.server_socket, buffer, sizeof(UAP_header), 0, (struct sockaddr*)&s.client_addr, sizeof(s.client_addr));
                    cout << "out of order packet, closing session" << endl;
                    break;
                }else if(head.sequence_number == s.last_header.sequence_number) {
                    cout << "duplicate packet" << endl;
                    continue;
                }else{
                    for(int i=s.last_header.sequence_number + 1; i < head.sequence_number; i++) {
                        cout << "lost packet" << endl;
                    }
                }
            }

            if(head.command == UAP_COMMAND_GOODBYE) {
                char buffer[sizeof(UAP_header)];
                {
                    lock_guard<mutex> lock(global_mutex);
                    clk = max(clk, head.logical_clock) + 1;
                    pack(buffer, "", UAP_COMMAND_GOODBYE, global_squence_no, s.session_id, clk, get_current_time());
                    global_squence_no++;
                }
                int send = sendto(s.server_socket, buffer, sizeof(UAP_header), 0, (struct sockaddr*)&s.client_addr, sizeof(s.client_addr));
                break;
            }

            s.last_header = head;
            cout << s.session_id << " [" << s.last_header.sequence_number << "] " << payload << endl;

            char buffer[sizeof(UAP_header)];
            {
                lock_guard<mutex> lock(global_mutex);
                clk = max(clk, head.logical_clock) + 1;
                pack(buffer, "", UAP_COMMAND_ALIVE, head.sequence_number, s.session_id, clk, get_current_time());
                global_squence_no++;
            }
            s.timeout_counter = steady_clock::now();
            int send = sendto(s.server_socket, buffer, sizeof(UAP_header), 0, (struct sockaddr*)&s.client_addr, sizeof(s.client_addr));
            
            if(send < 0) { perror("sendto"); break; }
            
        }else {
            auto elapsed = duration_cast<seconds>(steady_clock::now() - s.timeout_counter).count();
            // cout << "Time since last message: " << elapsed<< " seconds" << endl;
            if(elapsed > 10) {
                char buffer[sizeof(UAP_header)];
                {
                    lock_guard<mutex> lock(global_mutex);
                    clk = max(clk, s.last_header.logical_clock) + 1;
                    pack(buffer, "", UAP_COMMAND_GOODBYE, global_squence_no, s.session_id, clk, get_current_time());
                    global_squence_no++;
                }
                int send = sendto(s.server_socket, buffer, sizeof(UAP_header), 0, (struct sockaddr*)&s.client_addr, sizeof(s.client_addr));
                break;
            }
        }
    }
    s.is_done = true;
}

int main(int argc, char* argv[]) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        cout << "Failed to create socket" << endl;
        return 1;
    }

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cout << "Bind failed" << endl;
        close(server_socket);
        return 1;
    }

    while(true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = max(server_socket, STDIN_FILENO);

        // cout << session_threads.size() << " active sessions. Type 'q' to quit." << endl;

        // cout << "Waiting for activity..." << endl;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        // cout << "Select returned: " << activity << endl;

        if (activity < 0) {
            perror("select error");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            string line;
            getline(cin, line);
            cout << "Input received: " << line << endl;
            if (line == "q") {
                quitFlag = true;
                break;
            }
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            // cout << "Socket is readable" << endl;

            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            // cout << "Waiting for data..." << endl;

            char buffer[1024];
            int n = recvfrom(server_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_addr_len);
            if (n < 0) {
                continue;
            }

            // cout << "Data received from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;

            string payload = "";
            UAP_header header;
            if(!unPack(buffer, n, header, payload)) {
                cout << "Failed to unpack message" << endl;
                continue;
            }

            if(header.magic != UAP_MAGIC || header.version != UAP_VERSION) {
                continue;
            }

            cout << "Timestamp: " << header.timestamp << endl;
            cout << "Logical Clock: " << header.logical_clock << endl;

            if(header.command == UAP_COMMAND_HELLO) {
                cout << "Received HELLO from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << " " << header.session_id << endl;
                const int32_t session_id_copy = header.session_id;
                if (session_threads.find(session_id_copy) == session_threads.end()) {
                    session_threads[session_id_copy] = make_unique<sessions>(session_id_copy, server_socket, client_addr, header);
                }else{
                    cout << "Session ID already exists, ignoring HELLO" << endl;
                }
            }else if(header.command == UAP_COMMAND_DATA) {
                cout << "Received DATA from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;
                if(session_threads.find(header.session_id) != session_threads.end()) {
                    cout << "Session ID: " << header.session_id << endl;
                    session_threads[header.session_id]->message_queue.push({header, payload});
                }
            }else if (header.command == UAP_COMMAND_GOODBYE) {
                cout << "Received GOODBYE from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;
                if(session_threads.find(header.session_id) != session_threads.end()) {
                    cout << "Session ID: " << header.session_id << endl;
                    session_threads[header.session_id]->message_queue.push({header, ""});
                }
            }
        }

        for (auto it = session_threads.begin(); it != session_threads.end();) {
            if (it->second->is_done) {
                it->second->session_thread.join();
                it = session_threads.erase(it);
            } else {
                ++it;
            }
        }
    }

    cout << "Shutting down server..." << endl;
    for(auto& [id, s] : session_threads) {
        if (s && !s->is_done) {
            char buffer[sizeof(UAP_header)];
            {
                lock_guard<mutex> lock(global_mutex);
                clk = max(clk, s->last_header.logical_clock) + 1;
                pack(buffer, "", UAP_COMMAND_GOODBYE, global_squence_no, s->session_id, clk, get_current_time());
                global_squence_no++;
            }
            sendto(s->server_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&s->client_addr, sizeof(s->client_addr));
        }
    }

    for(auto& [id, s] : session_threads) {
        if (s && s->session_thread.joinable()) {
            s->session_thread.join();
        }
    }

    close(server_socket);
    return 0;
}