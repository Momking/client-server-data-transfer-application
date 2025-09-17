#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string>

using namespace std;


void sendResponse(int serverSocket, const sockaddr_in& clientAddress, const string& message) {
    ssize_t bytesSent = sendto(serverSocket, message.c_str(), message.length(), 0,
                               (const struct sockaddr*)&clientAddress, sizeof(clientAddress));

    if (bytesSent < 0) {
        perror("ERROR on sendto");
    } else {
        cout << "Sent response: '" << message << "' to client." << endl;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) {
        cerr << "ERROR: No port provided! Usage: ./server <port>" << endl;
        return 1;
    }

    int portNo = atoi(argv[1]);

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        perror("ERROR opening socket");
        return 1;
    }
    cout << "Socket created successfully." << endl;

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(struct sockaddr)) < 0) {
        perror("ERROR on binding");
        return 1;
    }
    cout << "Binding successful on port " << portNo << "." << endl;


    while (true)
    {
        char buffer[1024];
        sockaddr_in clientAddress;
        socklen_t size = sizeof(clientAddress);

        cout << "\nServer waiting for a packet..." << endl;
        int n = recvfrom(serverSocket, buffer, sizeof(buffer)-1, 0,
                        (struct sockaddr*)&clientAddress, &size);

        if(n < 0) {
            perror("ERROR on recvfrom");
            continue;
        }
        cout << "n: " << n << endl;
        receivedPacket(buffer, n);

        sendResponse(serverSocket, clientAddress, "Alive");
    }

    close(serverSocket);
    return 0;
}