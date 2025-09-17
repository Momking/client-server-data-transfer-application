#include <stdint.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

#ifndef htonll
#if __BYTE_ORDER == __BIG_ENDIAN
    #define htonll(x) (x)
    #define ntohll(x) (x)
#else
    #define htonll(x) (((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
    #define ntohll(x) (((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#endif
#endif


int logical_clock = 0;
int timestamp = 0;
int seqNo = 0;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    int32_t sequence_number;
    int32_t session_id;
    int64_t logical_clock;
    int64_t timestamp;
} UAP_header;

void addHeaders(char* buff, string payload, int command, int seqNo, int sessionID)
{
    UAP_header header;
    header.magic = htons(0xC461);
    header.version = 1;
    header.command = command;
    header.sequence_number = htonl(seqNo);
    header.session_id = htonl(sessionID);
    header.logical_clock = htonll(logical_clock);
    header.timestamp = htonll(timestamp);

    memcpy(buff, &header, sizeof(UAP_header));

    memcpy(buff + sizeof(UAP_header), payload.c_str(), payload.length());
}

int main(int argc, char** argv)
{
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(atoi(argv[2]));

    inet_pton(AF_INET, argv[1], &serverAddress.sin_addr);

    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(clientSocket < 0) { perror("socket"); return 1; }

    string line;
    while(getline(cin, line)){

        if (line.empty()) {
            continue;
        }

        char packetBuffer[line.length() + sizeof(UAP_header)];

        addHeaders(packetBuffer, line, 1, seqNo++, 12345);

        int sent = sendto(clientSocket, packetBuffer, sizeof(packetBuffer), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

        if(sent < 0) { perror("sendto"); return 1; }
        else { cout << "Sent " << sent << " bytes" << endl; }
        
        char receiveBuff[100];
        socklen_t serverSize = sizeof(serverAddress);
        int receive = recvfrom(clientSocket, receiveBuff, sizeof(receiveBuff)-1, 0, (sockaddr*)&serverAddress, &serverSize);
        
        if(receive < 0) {
            perror("ERROR on recvfrom");
        }
        
        cout << "--------------------------------" << endl;
        cout << "Received " << receive << " bytes." << endl;
        receiveBuff[receive] = '\0';
        cout << "Message: " << receiveBuff << endl;
    }

    return 0;
}