#include <stdint.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    int32_t sequence_number;
    int32_t session_id;
    int64_t logical_clock;
    int64_t timestamp;
} UAP_header;

int main(int argc, char** argv)
{
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(atoi(argv[2]));

    inet_pton(AF_INET, argv[1], &serverAddress.sin_addr);

    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(clientSocket < 0) { perror("socket"); return 1; }

    int sent = sendto(clientSocket, "Hello", 5, 0,
                    (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if(sent < 0) { perror("sendto"); return 1; }
    else { cout << "Sent " << sent << " bytes" << endl; }

    return 0;
}