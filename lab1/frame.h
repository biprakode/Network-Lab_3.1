#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

// FRAME -> [ HEADER 16B ][ PAYLOAD 44B ][ TRAILER 4B ]

#define HEADER_SIZE   16
#define PAYLOAD_SIZE  44
#define TRAILER_SIZE  4
#define FRAME_SIZE    (HEADER_SIZE + PAYLOAD_SIZE + TRAILER_SIZE)

#pragma pack(push , 1) // disable compiler padding

typedef struct {
    uint8_t src_addr[6];
    uint8_t dest_addr[6];
    uint16_t frame_len;
    uint16_t frame_seq;
}frame_header;

typedef struct {
    uint32_t fcs;
}frame_trailer;

typedef struct {
    frame_header header;
    uint8_t payload[PAYLOAD_SIZE];
    frame_trailer trailer;
}frame_t;

#pragma pack(pop)

#endif