// #include <cstdint.h>  // not working in my pc
#include <stdint.h>
#include <cstring>

const uint8_t UAP_COMMAND_HELLO = 0;
const uint8_t UAP_COMMAND_DATA = 1;
const uint8_t UAP_COMMAND_ALIVE = 2;
const uint8_t UAP_COMMAND_GOODBYE = 3;

const uint16_t UAP_MAGIC = 0xC461;
const uint8_t UAP_VERSION = 1;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    int32_t sequence_number;
    int32_t session_id;
    int64_t logical_clock;
    int64_t timestamp;
} UAP_header;

#ifndef htonll
#if __BYTE_ORDER == __BIG_ENDIAN
    #define htonll(x) (x)
    #define ntohll(x) (x)
#else
    #define htonll(x) (((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
    #define ntohll(x) (((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#endif
#endif