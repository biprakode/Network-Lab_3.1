#ifndef ARQ_H
#define ARQ_H

#include <stdint.h>

// Frame type constants (value for frame_header.frame_type)
#define FRAME_DATA 0
#define FRAME_ACK  1
#define FRAME_NAK  2

typedef enum {
    ARQ_SW,   // Stop-and-Wait
    ARQ_GBN,  // Go-Back-N
    ARQ_SR,   // Selective Repeat
} arq_mode_t;

typedef struct {
    arq_mode_t mode;
    int window_size;        // 1 for Stop-and-Wait
    int socket_fd;
    char *filename;         // File to send/receive
    uint32_t total_bytes;
    uint32_t retransmits;
} arq_config_t;

typedef struct {
    uint32_t frames_sent;
    uint32_t frames_retransmitted;
    uint32_t acks_received;
    uint32_t corrupted_count;
    double total_time;
} arq_stats_t;

#endif
