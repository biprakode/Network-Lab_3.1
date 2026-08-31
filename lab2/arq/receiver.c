#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>

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

// Forward declarations for mode-specific implementations
static int receiver_sw_mode(int fd, FILE *output);
static int receiver_gbn_mode(int fd, FILE *output);
static int receiver_sr_mode(int fd, FILE *output, int window_size);

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

    int result = 0;

    // ===== DISPATCH BY ARQ MODE =====
    switch (receiver_config->mode) {
        case ARQ_SW:
            result = receiver_sw_mode(fd, output);
            break;

        case ARQ_GBN:
            result = receiver_gbn_mode(fd, output);
            break;

        case ARQ_SR:
            result = receiver_sr_mode(fd, output, receiver_config->window_size);
            break;

        default:
            fprintf(stderr, "[RECEIVER] Unknown ARQ mode: %d\n", receiver_config->mode);
            result = -1;
            break;
    }

    // ===== RECORD STATS =====
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - g_start_time.tv_sec) +
                     (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
    g_stats.total_time = elapsed;

    if (result == 0) {
        printf("\n[RECEIVER] Transfer complete!\n");
        printf("  Frames received: %u\n", g_stats.frames_sent);
        printf("  ACKs sent: %u\n", g_stats.acks_received);
        printf("  Corrupted frames: %u\n", g_stats.corrupted_count);
        printf("  Time: %.3f seconds\n", g_stats.total_time);
    }

    fclose(output);
    return result;
}

// ===== STOP-AND-WAIT MODE IMPLEMENTATION =====
static int receiver_sw_mode(int fd, FILE *output) {
    uint16_t Rn = 0;  // Expected sequence number (toggle 0-1 for Stop-and-Wait)

    printf("[RECEIVER-SW] Waiting for DATA frames...\n");

    // Main receive loop
    while (1) {
        // ===== RECEIVE FRAME =====
        frame_t frame = {0};
        int bytes = channel_recv(fd, (uint8_t *)&frame, FRAME_SIZE);

        if (bytes <= 0) {
            printf("[RECEIVER-SW] Connection closed or error\n");
            break;
        }

        // ===== VERIFY FCS =====
        uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&frame, FRAME_SIZE - TRAILER_SIZE);
        uint32_t received_fcs = ntohl(frame.trailer.fcs);

        bool delivered = false;
        bool eot = false;

        if (expected_fcs != received_fcs) {
            printf("[RECEIVER-SW] Corrupted frame received (FCS mismatch)\n");
            g_stats.corrupted_count++;
            // Send ACK for last good frame 
            // Fall through to send ACK for current Rn
        } else if (frame.header.frame_type != FRAME_DATA) {
            printf("[RECEIVER-SW] Non-DATA frame received (type=%d)\n", frame.header.frame_type);
            continue;
        } else if (ntohs(frame.header.frame_seq) != Rn) {
            printf("[RECEIVER-SW] Out-of-order frame: expected %d, got %d\n", Rn, ntohs(frame.header.frame_seq));
            // Send ACK for last good frame
        } else {
            // ===== VALID DATA FRAME - DELIVER DATA =====
            uint16_t payload_len = ntohs(frame.header.frame_len);
            fwrite(frame.payload, 1, payload_len, output);
            g_stats.frames_sent++;  // Frames received (using frames_sent for consistency with sender)
            printf("[RECEIVER-SW] Received DATA frame #%d (len=%d)\n", Rn, payload_len);

            eot = (payload_len == 0);  // Check if this is EOT frame
            delivered = true;
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
        printf("[RECEIVER-SW] Sent ACK for frame #%d\n", (Rn - 1 + 2) % 2);

        // Check for end-of-transmission signal (only if frame was delivered)
        if (delivered && eot) {
            printf("[RECEIVER-SW] End of transmission\n");
            break;
        }
    }

    return 0;
}

// ===== GO-BACK-N MODE IMPLEMENTATION =====
static int receiver_gbn_mode(int fd, FILE *output) {
    uint16_t Rn = 0;  // Expected sequence number

    printf("[RECEIVER-GBN] Waiting for DATA frames...\n");

    while (1) {
        // ===== RECEIVE FRAME =====
        frame_t frame = {0};
        int bytes = channel_recv(fd, (uint8_t *)&frame, FRAME_SIZE);

        if (bytes <= 0) {
            printf("[RECEIVER-GBN] Connection closed or error\n");
            break;
        }

        // ===== VERIFY FCS =====
        uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&frame, FRAME_SIZE - TRAILER_SIZE);
        uint32_t received_fcs = ntohl(frame.trailer.fcs);

        bool delivered = false;
        bool eot = false;

        if (expected_fcs != received_fcs) {
            printf("[RECEIVER-GBN] Corrupted frame received (FCS mismatch)\n");
            g_stats.corrupted_count++;
        } else if (frame.header.frame_type != FRAME_DATA) {
            printf("[RECEIVER-GBN] Non-DATA frame received (type=%d)\n", frame.header.frame_type);
        } else if (ntohs(frame.header.frame_seq) != Rn) {
            printf("[RECEIVER-GBN] Out-of-order frame: expected %d, got %d (discarded)\n", Rn, ntohs(frame.header.frame_seq));
        } else {
            // ===== VALID IN-ORDER DATA FRAME =====
            uint16_t payload_len = ntohs(frame.header.frame_len);
            fwrite(frame.payload, 1, payload_len, output);
            g_stats.frames_sent++;
            printf("[RECEIVER-GBN] Received DATA frame #%d (len=%d)\n", Rn, payload_len);
            eot = (payload_len == 0);
            delivered = true;
            Rn++;
        }

        // ===== SEND CUMULATIVE ACK =====
        frame_t ack_frame = {0};
        ack_frame.header.frame_type = FRAME_ACK;
        ack_frame.header.frame_seq = htons(Rn);  // Acknowledge next expected
        ack_frame.header.frame_len = 0;

        uint32_t ack_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
        ack_frame.trailer.fcs = htonl(ack_fcs);

        channel_send(fd, (uint8_t *)&ack_frame, FRAME_SIZE);
        g_stats.acks_received++;
        printf("[RECEIVER-GBN] Sent ACK for frame #%d\n", Rn - 1);

        // Check for end-of-transmission (only if frame was delivered)
        if (delivered && eot) {
            printf("[RECEIVER-GBN] End of transmission\n");
            break;
        }
    }

    return 0;
}

// ===== SELECTIVE REPEAT MODE IMPLEMENTATION =====
static int receiver_sr_mode(int fd, FILE *output, int window_size) {
    uint16_t Rn = 0;  // Expected sequence number
    bool nak_sent = false;
    bool ack_needed = false;

    frame_t buffer[256];        // Frame buffer
    bool marked[256];           // Marked array: true if frame received
    memset(marked, false, sizeof(marked));

    printf("[RECEIVER-SR] Waiting for DATA frames...\n");

    while (1) {
        // ===== RECEIVE FRAME =====
        frame_t frame = {0};
        int bytes = channel_recv(fd, (uint8_t *)&frame, FRAME_SIZE);

        if (bytes <= 0) {
            printf("[RECEIVER-SR] Connection closed or error\n");
            break;
        }

        // ===== VERIFY FCS =====
        uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&frame, FRAME_SIZE - TRAILER_SIZE);
        uint32_t received_fcs = ntohl(frame.trailer.fcs);

        if (expected_fcs != received_fcs) {
            // Corrupted frame: send NAK if not already sent
            if (!nak_sent) {
                printf("[RECEIVER-SR] Corrupted frame received, sending NAK for frame #%d\n", Rn);
                frame_t nak_frame = {0};
                nak_frame.header.frame_type = FRAME_NAK;
                nak_frame.header.frame_seq = htons(Rn);
                nak_frame.header.frame_len = 0;

                uint32_t nak_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&nak_frame, FRAME_SIZE - TRAILER_SIZE);
                nak_frame.trailer.fcs = htonl(nak_fcs);

                channel_send(fd, (uint8_t *)&nak_frame, FRAME_SIZE);
                nak_sent = true;
            }
            g_stats.corrupted_count++;
            continue;
        }

        if (frame.header.frame_type != FRAME_DATA) {
            printf("[RECEIVER-SR] Non-DATA frame received (type=%d)\n", frame.header.frame_type);
            continue;
        }

        uint16_t seqNo = ntohs(frame.header.frame_seq);

        // Out-of-order frame: send NAK if not already sent
        if (seqNo != Rn && !nak_sent) {
            printf("[RECEIVER-SR] Out-of-order frame: expected %d, got %d, sending NAK\n", Rn, seqNo);
            frame_t nak_frame = {0};
            nak_frame.header.frame_type = FRAME_NAK;
            nak_frame.header.frame_seq = htons(Rn);
            nak_frame.header.frame_len = 0;

            uint32_t nak_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&nak_frame, FRAME_SIZE - TRAILER_SIZE);
            nak_frame.trailer.fcs = htonl(nak_fcs);

            channel_send(fd, (uint8_t *)&nak_frame, FRAME_SIZE);
            nak_sent = true;
        }

        // Check if frame is in window [Rn, Rn + Sw)
        uint16_t Sw = 1 << (window_size - 1);  // Sw = 2^(m-1)
        bool eot_delivered = false;
        if ((seqNo >= Rn) && (seqNo < Rn + Sw) && !marked[seqNo % 256]) {
            // ===== BUFFER OUT-OF-ORDER FRAME =====
            buffer[seqNo % 256] = frame;
            marked[seqNo % 256] = true;
            printf("[RECEIVER-SR] Buffered frame #%d\n", seqNo);

            // ===== DELIVER ALL CONSECUTIVE MARKED FRAMES STARTING FROM Rn =====
            while (marked[Rn % 256]) {
                uint16_t payload_len = ntohs(buffer[Rn % 256].header.frame_len);
                fwrite(buffer[Rn % 256].payload, 1, payload_len, output);
                g_stats.frames_sent++;
                printf("[RECEIVER-SR] Delivered frame #%d (len=%d)\n", Rn, payload_len);

                if (payload_len == 0) {
                    eot_delivered = true;
                }

                marked[Rn % 256] = false;
                Rn++;
                ack_needed = true;
            }
        }

        // ===== SEND ACK AFTER DELIVERY =====
        if (ack_needed) {
            frame_t ack_frame = {0};
            ack_frame.header.frame_type = FRAME_ACK;
            ack_frame.header.frame_seq = htons(Rn);  // Acknowledge next expected
            ack_frame.header.frame_len = 0;

            uint32_t ack_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
            ack_frame.trailer.fcs = htonl(ack_fcs);

            channel_send(fd, (uint8_t *)&ack_frame, FRAME_SIZE);
            g_stats.acks_received++;
            printf("[RECEIVER-SR] Sent ACK for frame #%d\n", Rn - 1);

            ack_needed = false;
            nak_sent = false;  // Clear NAK flag after successful delivery

            if (eot_delivered) {
                printf("[RECEIVER-SR] End of transmission\n");
                break;
            }
        }
    }

    return 0;
}

void receiver_free(arq_config_t *receiver) {
    // Cleanup if needed
}

arq_stats_t receiver_get_stats(arq_config_t *receiver) {
    return g_stats;
}
