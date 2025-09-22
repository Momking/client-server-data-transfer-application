#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <random>
#include <iomanip>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>

#include "../include/UAP_header.h"

using namespace std;

// Constants
const int BUFFER_SIZE = 2048;
const int RESPONSE_TIMEOUT_SECONDS = 5;
const string SENTINEL_EOF = "---EOF---";
const string SENTINEL_QUIT = "---QUIT---";

enum ClientState { HELLO_WAIT, READY, READY_TIMER, CLOSING, CLOSED };

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue;
    mutable mutex mtx;
    condition_variable cv;
public:
    void push(T item) {
        lock_guard<mutex> lock(mtx);
        queue.push(item);
        cv.notify_one();
    }
    bool try_pop(T& item) {
        lock_guard<mutex> lock(mtx);
        if (queue.empty()) {
            return false;
        }
        item = queue.front();
        queue.pop();
        return true;
    }
};

// Global Shared Resources
ThreadSafeQueue<string> stdin_queue;
ThreadSafeQueue<vector<char>> network_queue;
atomic<bool> running(true);
uint64_t client_logical_clock = 0;

// Function Prototypes
void stdin_reader_thread();
void network_receiver_thread(int sockfd);
void send_uap_message(int sockfd, const struct sockaddr* addr, uint32_t session_id, uint32_t& seq_num, uint8_t command, const string& payload = "");
uint64_t get_current_microseconds();

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <hostname> <portnum>" << endl;
        return 1;
    }
    string hostname = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("ERROR opening socket"); return 1; }

    struct hostent* server = gethostbyname(hostname.c_str());
    if (server == NULL) { cerr << "ERROR, no such host" << endl; return 1; }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<uint32_t> distrib;
    const uint32_t session_id = distrib(gen);

    uint32_t sequence_number = 0;
    ClientState state = HELLO_WAIT;
    
    bool timer_active = false;
    auto timer_start = chrono::steady_clock::now();

    double total_latency = 0.0;
    int packet_count = 0;

    auto initiate_shutdown = [&]() {
        send_uap_message(sockfd, (struct sockaddr*)&serv_addr, session_id, sequence_number, UAP_COMMAND_GOODBYE);
        state = CLOSING;
        timer_start = chrono::steady_clock::now();
        timer_active = true;
    };
    
    thread stdin_thread(stdin_reader_thread);
    thread network_thread(network_receiver_thread, sockfd);

    cout << "Starting session 0x" << hex << session_id << dec << endl;
    send_uap_message(sockfd, (struct sockaddr*)&serv_addr, session_id, sequence_number, UAP_COMMAND_HELLO);
    timer_start = chrono::steady_clock::now();
    timer_active = true;
    state = HELLO_WAIT;
    
    while (running && state != CLOSED) {
        // Check for Network Packets
        vector<char> packet_data;
        if (network_queue.try_pop(packet_data)) {
            if (packet_data.size() < sizeof(UAP_header)) continue;
            
            UAP_header* header = (UAP_header*)packet_data.data();
            if (ntohs(header->magic) != UAP_MAGIC || ntohl(header->session_id) != session_id) {
                continue;
            }
            
            client_logical_clock = max(client_logical_clock, ntohll(header->logical_clock)) + 1;

            // Calculate and Print One-Way Latency
            uint64_t reception_time = get_current_microseconds();
            uint64_t send_timestamp = ntohll(header->timestamp);
            double latency_ms = (reception_time - send_timestamp) / 1000.0;
            total_latency += latency_ms;
            packet_count++;
            cout << "Latency: " << fixed << setprecision(2) << latency_ms << " ms" << endl;
            
            if (header->command == UAP_COMMAND_GOODBYE) {
                cout << "Received GOODBYE from server. Closing." << endl;
                state = CLOSED;
                continue;
            }

            switch (state) {
                case HELLO_WAIT:
                    if (header->command == UAP_COMMAND_HELLO) {
                        cout << "Received HELLO from server. Session established." << endl;
                        state = READY;
                        timer_active = false;
                    }
                    break;
                case READY_TIMER:
                    if (header->command == UAP_COMMAND_ALIVE) {
                        cout << "["<<ntohl(header->sequence_number)<< "] ALIVE received from server." << endl;
                        state = READY;
                        timer_active = false;
                    }
                    break;
                case READY: // Per FSA, ALIVE in Ready state is ignored
                case CLOSING: // Per FSA, ALIVE in Closing state is ignored
                    break; 
                default:
                    break;
            }
        }

        // Check for Stdin Lines
        string stdin_line;
        if (state == READY || state == READY_TIMER) {
            if (stdin_queue.try_pop(stdin_line)) {
                if (stdin_line == SENTINEL_EOF || stdin_line == SENTINEL_QUIT) {
                    initiate_shutdown();
                } else {
                    send_uap_message(sockfd, (struct sockaddr*)&serv_addr, session_id, sequence_number, UAP_COMMAND_DATA, stdin_line);
                    state = READY_TIMER;
                    timer_start = chrono::steady_clock::now();
                    timer_active = true;
                }
            }
        }
        
        // Check Timers
        if (timer_active) {
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::seconds>(now - timer_start).count();
            if (elapsed >= RESPONSE_TIMEOUT_SECONDS) {
                timer_active = false;
                if (state == CLOSING) {
                    cout << "GOODBYE response timed out. Closing." << endl;
                    state = CLOSED;
                } else { // Timeout in HELLO_WAIT or READY_TIMER
                    cout << "[ERROR] Server response timed out. Sending GOODBYE." << endl;
                    initiate_shutdown();
                }
            }
        }
    }

    running = false; 
    shutdown(sockfd, SHUT_RDWR); 
    if (stdin_thread.joinable()) { stdin_thread.detach(); }
    if (network_thread.joinable()) { network_thread.join(); }
    close(sockfd);

    if (packet_count > 0) {
        double avg_latency = total_latency / packet_count;
        cout << "Average one-way latency: " << fixed << setprecision(2) << avg_latency << " ms" << endl;
    }
    
    cout << "Client shut down." << endl;
    exit(0);
}

void stdin_reader_thread() {
    string line;
    while (running && getline(cin, line)) {
        client_logical_clock++; 
        if (line == "q" && isatty(STDIN_FILENO)) {
            stdin_queue.push(SENTINEL_QUIT);
            break;
        }
        stdin_queue.push(line);
    }
    if (running) {
        cout << "eof" << endl;
        client_logical_clock++; 
        stdin_queue.push(SENTINEL_EOF);
    }
}

void network_receiver_thread(int sockfd) {
    char buffer[BUFFER_SIZE];
    while (running) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
        if (n > 0) {
            vector<char> data(buffer, buffer + n);
            network_queue.push(data);
        } else {
            if (running) { perror("recvfrom error"); }
            break;
        }
    }
}

void send_uap_message(int sockfd, const struct sockaddr* addr, uint32_t session_id, uint32_t& seq_num, uint8_t command, const string& payload) {
    size_t buffer_len = sizeof(UAP_header) + payload.length();
    vector<char> buffer(buffer_len);

    UAP_header header;
    header.magic = htons(UAP_MAGIC);
    header.version = UAP_VERSION;
    header.command = command;
    header.sequence_number = htonl(seq_num++);
    header.session_id = htonl(session_id);
    
    client_logical_clock++; 
    header.logical_clock = htonll(client_logical_clock);
    header.timestamp = htonll(get_current_microseconds());

    memcpy(buffer.data(), &header, sizeof(UAP_header));
    memcpy(buffer.data() + sizeof(UAP_header), payload.c_str(), payload.length());

    sendto(sockfd, buffer.data(), buffer.size(), 0, addr, sizeof(struct sockaddr_in));
}

uint64_t get_current_microseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
