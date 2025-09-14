#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

void serverCreateAddr(sockaddr_in& serverAddress, int portNo)
{
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
}

int main (int argc, char** argv)
{
    int portNo = atoi(argv[1]);

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddress;
    serverCreateAddr(serverAddress, portNo);
}