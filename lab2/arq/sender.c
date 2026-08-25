#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

#include "arq.h"
#include "sender.h"
#include "../../lab1/frame.h"
#include "../../lab1/scheme.h"
#include "../../lab1/error.h"
#include "../../utils/utils.h"
#include "../timer.h"
#include "../channel.h"

static arq_config_t *g_config;
static arq_stats_t g_stats = {0};
static struct timespec g_start_time = {0};

// Helper: Wait for ACK with timeout using select()
int wait_for_ack(int fd, frame_t *ack_frame, double timeout_ms) {
    struct timeval tv;
    tv.tv_sec = (int)(timeout_ms / 1000.0);
    tv.tv_usec = (int)((timeout_ms - tv.tv_sec * 1000) * 1000);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("select");
        return -1;
    }
    if (ret == 0) {
        return 0;  // Timeout
    }

    // Data available to read
    int bytes = channel_recv(fd, (uint8_t *)ack_frame, FRAME_SIZE);
    return bytes;
}

void sender_init(arq_config_t *sender_config) {
    g_config = sender_config;
    memset(&g_stats, 0, sizeof(g_stats));
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
}

int sender_run(arq_config_t *sender_config) {
    g_config = sender_config;

    size_t file_size = 0;
    uint8_t *data = read_file(sender_config->filename, &file_size);
    if (!data) {
        fprintf(stderr, "Failed to read file: %s\n", sender_config->filename);
        return -1;
    }

    int fd = sender_config->socket_fd;
    if (fd < 0) {
        fprintf(stderr, "Invalid socket fd\n");
        free(data);
        return -1;
    }

    uint16_t Sn = 0;  // Sequence number (toggle 0-1 for Stop-and-Wait)
    size_t offset = 0;

    timer_config timer_cfg = {100.0, 0};  // 100ms RTO, no EMA
    timer_init(timer_cfg);

    // Main send loop
    while (offset < file_size) {
        // ===== CREATE DATA FRAME =====
        frame_t frame = {0};

        size_t chunk_len = file_size - offset;
        if (chunk_len > PAYLOAD_SIZE) {
            chunk_len = PAYLOAD_SIZE;
        }

        frame.header.frame_type = FRAME_DATA;
        frame.header.frame_seq = htons(Sn);
        frame.header.frame_len = htons((uint16_t)chunk_len);
        memcpy(frame.payload, data + offset, chunk_len);

        // Compute FCS
        uint32_t fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&frame, FRAME_SIZE - TRAILER_SIZE);
        frame.trailer.fcs = htonl(fcs);

        // ===== SEND FRAME =====
        channel_send(fd, (uint8_t *)&frame, FRAME_SIZE);
        g_stats.frames_sent++;
        printf("[SENDER] Sent DATA frame #%d (offset=%zu, len=%zu)\n", Sn, offset, chunk_len);

        timer_start_window();

        // ===== WAIT FOR ACK WITH TIMEOUT =====
        int ack_received = 0;
        while (!ack_received) {
            double remaining = timer_frame_remaining_ms(Sn);
            if (remaining <= 0) remaining = 0.1;  // At least 0.1ms

            frame_t ack_frame = {0};
            int bytes = wait_for_ack(fd, &ack_frame, remaining);

            if (bytes > 0) {
                // Check if valid ACK
                uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
                uint32_t received_fcs = ntohl(ack_frame.trailer.fcs);

                if (expected_fcs == received_fcs &&
                    ack_frame.header.frame_type == FRAME_ACK &&
                    ntohs(ack_frame.header.frame_seq) == Sn) {
                    // Valid ACK for this frame
                    printf("[SENDER] Received ACK for frame #%d\n", Sn);
                    timer_stop_window();
                    g_stats.acks_received++;
                    ack_received = 1;
                    break;
                } else {
                    printf("[SENDER] Corrupted or wrong ACK received\n");
                    if (expected_fcs != received_fcs) {
                        g_stats.corrupted_count++;
                    }
                }
            } else if (bytes == 0) {
                // Timeout
                if (timer_window_expired()) {
                    printf("[SENDER] Timeout! Retransmitting frame #%d\n", Sn);
                    channel_send(fd, (uint8_t *)&frame, FRAME_SIZE);
                    g_stats.frames_retransmitted++;
                    timer_start_window();
                }
            } else {
                // Error
                perror("wait_for_ack");
                free(data);
                return -1;
            }
        }

        // ===== ADVANCE TO NEXT FRAME =====
        offset += chunk_len;
        Sn = (Sn + 1) % 2;  // Toggle: 0->1, 1->0
    }

    // ===== RECORD STATS =====
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - g_start_time.tv_sec) +
                     (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
    g_stats.total_time = elapsed;

    printf("\n[SENDER] Transfer complete!\n");
    printf("  Total bytes sent: %u\n", (uint32_t)file_size);
    printf("  Frames sent: %u\n", g_stats.frames_sent);
    printf("  Retransmissions: %u\n", g_stats.frames_retransmitted);
    printf("  ACKs received: %u\n", g_stats.acks_received);
    printf("  Time: %.3f seconds\n", g_stats.total_time);

    free(data);
    return 0;
}

void sender_free(arq_config_t *sender) {
    // Cleanup if needed
}

arq_stats_t sender_get_stats(arq_config_t *sender) {
    return g_stats;
}
