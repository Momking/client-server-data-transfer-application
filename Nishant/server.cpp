#include <iostream.h>
#include <stdint.h>

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    int32_t sequence_number;
    int32_t session_id;
    int64_t logical_clock;
    int64_t timestamp;
} UAP_header;

void serverAddr (sockaddr_in& serverAddress, int portNo)
{
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
}

int main (int argc, **char argv)
{
    int portNo = argv[1];

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddress;
    serverAddr(serverAddress, portNo);
}