#include <stdint.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

int logical_clock = 0;
int timestamp = 0;
int seqNo = 0;

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