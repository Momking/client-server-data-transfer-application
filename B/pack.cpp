#include "pack.h"
#include <arpa/inet.h>
#include <cstring>

void pack(char* buff, const std::string& payload, uint8_t command, int32_t seqNo, int32_t sessionID, int64_t logical_clock, int64_t timestamp)
{
    UAP_header header;
    header.magic = htons(UAP_MAGIC);
    header.version = UAP_VERSION;
    header.command = command;
    header.sequence_number = htonl(seqNo);
    header.session_id = htonl(sessionID);
    header.logical_clock = htonll(logical_clock);
    header.timestamp = htonll(timestamp);

    memcpy(buff, &header, sizeof(UAP_header));

    memcpy(buff + sizeof(UAP_header), payload.c_str(), payload.length());
}