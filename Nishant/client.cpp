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