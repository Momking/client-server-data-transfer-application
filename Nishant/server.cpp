#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

void createServerAddr(sockaddr_in& serverAddress, int portNo)
{
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
}

int main (int argc, char** argv)
{
    int portNo = atoi(argv[1]);

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in serverAddress;
    createServerAddr(serverAddress, portNo);

    bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

    char buffer[50];
    sockaddr_in clientAddress;
    socklen_t size = sizeof(clientAddress);

    cout << "Server waiting for a packet..." << endl;
    int n = recvfrom(serverSocket, buffer, sizeof(buffer)-1, 0,
                    (struct sockaddr*)&clientAddress, &size);
    if(n < 0) { perror("recvfrom"); return 1; }
    cout << "recvfrom returned: " << n << " bytes" << endl;

    buffer[n] = '\0';
    cout << "Received: " << buffer << endl;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddress.sin_addr, ip, sizeof(ip));
    cout << "From client: " << ip << ":" << ntohs(clientAddress.sin_port) << endl;


    return 0;
}