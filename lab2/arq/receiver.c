#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "arq.h"
#include "receiver.h"
#include "../../lab1/frame.h"
#include "../../lab1/scheme.h"
#include "../../lab1/error.h"
#include "../../utils/utils.h"
#include "../timer.h"
#include "../channel.h"

static arq_config_t *g_config;
static arq_stats_t g_stats = {0};
static struct timespec g_start_time = {0};

void receiver_init(arq_config_t *receiver_config) {
    g_config = receiver_config;
    memset(&g_stats, 0, sizeof(g_stats));
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
}

int receiver_run(arq_config_t *receiver_config) {
    g_config = receiver_config;

    FILE *output = fopen(receiver_config->filename, "wb");
    if (!output) {
        perror("fopen output");
        return -1;
    }

    int fd = receiver_config->socket_fd;
    if (fd < 0) {
        fprintf(stderr, "Invalid socket fd\n");
        fclose(output);
        return -1;
    }

    uint16_t Rn = 0;  // Expected sequence number (toggle 0-1 for Stop-and-Wait)

    printf("[RECEIVER] Waiting for DATA frames...\n");

    // Main receive loop
    while (1) {
        // ===== RECEIVE FRAME =====
        frame_t frame = {0};
        int bytes = channel_recv(fd, (uint8_t *)&frame, FRAME_SIZE);

        if (bytes <= 0) {
            printf("[RECEIVER] Connection closed or error\n");
            break;
        }

        // ===== VERIFY FCS =====
        uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&frame, FRAME_SIZE - TRAILER_SIZE);
        uint32_t received_fcs = ntohl(frame.trailer.fcs);

        if (expected_fcs != received_fcs) {
            printf("[RECEIVER] Corrupted frame received (FCS mismatch)\n");
            g_stats.corrupted_count++;
            // Send ACK for last good frame (don't advance Rn)
            // Fall through to send ACK for current Rn
        } else if (frame.header.frame_type != FRAME_DATA) {
            printf("[RECEIVER] Non-DATA frame received (type=%d)\n", frame.header.frame_type);
            continue;
        } else if (ntohs(frame.header.frame_seq) != Rn) {
            printf("[RECEIVER] Out-of-order frame: expected %d, got %d\n", Rn, ntohs(frame.header.frame_seq));
            // Send ACK for last good frame
        } else {
            // ===== VALID DATA FRAME - DELIVER DATA =====
            uint16_t payload_len = ntohs(frame.header.frame_len);
            fwrite(frame.payload, 1, payload_len, output);
            g_stats.frames_sent++;  // Frames received (using frames_sent for consistency with sender)
            printf("[RECEIVER] Received DATA frame #%d (len=%d)\n", Rn, payload_len);

            // Advance expected sequence number
            Rn = (Rn + 1) % 2;  // Toggle: 0->1, 1->0
        }

        // ===== SEND ACK =====
        frame_t ack_frame = {0};
        ack_frame.header.frame_type = FRAME_ACK;
        ack_frame.header.frame_seq = htons((Rn - 1 + 2) % 2);  // ACK last received frame
        ack_frame.header.frame_len = 0;  // No payload in ACK

        // Compute FCS for ACK
        uint32_t ack_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
        ack_frame.trailer.fcs = htonl(ack_fcs);

        channel_send(fd, (uint8_t *)&ack_frame, FRAME_SIZE);
        g_stats.acks_received++;  // ACKs sent (using acks_received for consistency)
        printf("[RECEIVER] Sent ACK for frame #%d\n", (Rn - 1 + 2) % 2);

        // Check for end-of-transmission signal (0-length data frame)
        if (frame.header.frame_type == FRAME_DATA && ntohs(frame.header.frame_len) == 0) {
            printf("[RECEIVER] End of transmission\n");
            break;
        }
    }

    // ===== RECORD STATS =====
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - g_start_time.tv_sec) +
                     (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
    g_stats.total_time = elapsed;

    printf("\n[RECEIVER] Transfer complete!\n");
    printf("  Frames received: %u\n", g_stats.frames_sent);
    printf("  ACKs sent: %u\n", g_stats.acks_received);
    printf("  Corrupted frames: %u\n", g_stats.corrupted_count);
    printf("  Time: %.3f seconds\n", g_stats.total_time);

    fclose(output);
    return 0;
}

void receiver_free(arq_config_t *receiver) {
    // Cleanup if needed
}

arq_stats_t receiver_get_stats(arq_config_t *receiver) {
    return g_stats;
}
