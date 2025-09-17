#include "/include/UAP_header.h"

bool unPack(const char* buffer, int n, UAP_header& header, std::string& payload) {
    if (n < sizeof(UAP_header)) {
        cerr << "Packet too small to be a UAP message!" << endl;
        return false;
    }

    const UAP_header* receivedHeader = reinterpret_cast<const UAP_header*>(buffer);

    header.magic = ntohs(receivedHeader->magic);
    header.version = receivedHeader->version;
    header.command = receivedHeader->command;
    header.sequence_number = ntohl(receivedHeader->sequence_number);
    header.session_id = ntohl(receivedHeader->session_id);
    header.logical_clock = ntohll(receivedHeader->logical_clock);
    header.timestamp = ntohll(receivedHeader->timestamp);

    if (header.magic != UAP_MAGIC || header.version != UAP_VERSION) {
        cerr << "Packet discarded: Invalid magic number or version." << endl;
        return false;
    }
    
    int payloadSize = n - sizeof(UAP_header);
    if (payloadSize > 0) {
        payload.assign(buffer + sizeof(UAP_header), payloadSize);
    } else {
        payload.clear();
    }

    return true;
}