#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdbool.h>
#include <stdint.h>

#include "arq.h"
#include "sender.h"
#include "../../lab1/frame.h"
#include "../../lab1/scheme.h"
#include "../../lab1/error.h"
#include "../../utils/utils.h"
#include "../timer.h"
#include "../channel.h"

typedef struct {
    uint16_t seq_num;
    struct timespec send_time;
    bool is_pending;
} timer_slot_t;

static arq_config_t *g_config;
static arq_stats_t g_stats = {0};
static struct timespec g_start_time = {0};

// Send timestamp per sequence number, used to measure the time from a data frame
// leaving the sender to the arrival of its ACK
static struct timespec g_send_ts[65536];

static void mark_send_time(uint16_t seq) {
    clock_gettime(CLOCK_MONOTONIC, &g_send_ts[seq]);
}

static void record_rtt(uint16_t seq) {
    if (g_send_ts[seq].tv_sec == 0 && g_send_ts[seq].tv_nsec == 0) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double ms = (now.tv_sec - g_send_ts[seq].tv_sec) * 1000.0 + (now.tv_nsec - g_send_ts[seq].tv_nsec) / 1e6;
    if (ms < 0) {
        return;
    }
    if (g_stats.rtt_samples == 0 || ms < g_stats.rtt_min_ms) {
        g_stats.rtt_min_ms = ms;
    }
    if (g_stats.rtt_samples == 0 || ms > g_stats.rtt_max_ms) {
        g_stats.rtt_max_ms = ms;
    }
    g_stats.rtt_sum_ms += ms;
    g_stats.rtt_samples++;
}

// Forward declarations for mode-specific implementations
static int sender_sw_mode(int fd, uint8_t *data, size_t file_size);
static int sender_gbn_mode(int fd, uint8_t *data, size_t file_size , arq_config_t *sender_config);
static int sender_sr_mode(int fd, uint8_t *data, size_t file_size , arq_config_t *sender_config);

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

    // Data available, or the peer closed the connection
    int bytes = channel_recv(fd, (uint8_t *)ack_frame, FRAME_SIZE);
    if (bytes <= 0) {
        return -2;  // Peer closed: the receiver finished and hung up
    }
    return bytes;
}

// True if fd has at least one frame ready to read right now
static int fd_readable_now(int fd) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    struct timeval z = {0, 0};
    return select(fd + 1, &r, NULL, NULL, &z) > 0;
}

void sender_init(arq_config_t *sender_config) {
    g_config = sender_config;
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_send_ts, 0, sizeof(g_send_ts));
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

    timer_config timer_cfg = {20.0, 0};  // 20ms fixed RTO, no EMA
    timer_init(timer_cfg);

    int result = 0;

    // ===== DISPATCH BY ARQ MODE =====
    switch (sender_config->mode) {
        case ARQ_SW:
            result = sender_sw_mode(fd, data, file_size);
            break;

        case ARQ_GBN:
            result = sender_gbn_mode(fd , data , file_size , sender_config);
            break;

        case ARQ_SR:
            result = sender_sr_mode(fd, data, file_size, sender_config);
            break;

        default:
            fprintf(stderr, "[SENDER] Unknown ARQ mode: %d\n", sender_config->mode);
            result = -1;
            break;
    }

    // ===== RECORD STATS =====
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - g_start_time.tv_sec) + (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
    g_stats.total_time = elapsed;

    if (result == 0) {
        printf("\n[SENDER] Transfer complete!\n");
        printf("  Total bytes sent: %u\n", (uint32_t)file_size);
        printf("  Frames sent: %u\n", g_stats.frames_sent);
        printf("  Retransmissions: %u\n", g_stats.frames_retransmitted);
        printf("  ACKs received: %u\n", g_stats.acks_received);
        printf("  Time: %.3f seconds\n", g_stats.total_time);
    }

    free(data);
    return result;
}

// ===== HELPER: Wait for ACK and handle retransmit on timeout (SW mode) =====
static int sw_wait_for_ack(int fd, frame_t *frame, uint16_t Sn) {
    int ack_received = 0;
    while (!ack_received) {
        // Wait one full retransmission timeout for the ACK. select() returns
        // early the moment the ACK arrives.
        double remaining = timer_get_rto_ms();
        if (remaining <= 0) remaining = 1.0;

        frame_t ack_frame = {0};
        int bytes = wait_for_ack(fd, &ack_frame, remaining);

        if (bytes > 0) {
            // Check if valid ACK
            uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
            uint32_t received_fcs = ntohl(ack_frame.trailer.fcs);

            if (expected_fcs == received_fcs && ack_frame.header.frame_type == FRAME_ACK && ntohs(ack_frame.header.frame_seq) == Sn) {
                // Valid ACK for this frame
                printf("[SENDER-SW] Received ACK for frame #%d\n", Sn);
                timer_stop_window();
                record_rtt(Sn);
                g_stats.acks_received++;
                ack_received = 1;
                break;
            } else {
                printf("[SENDER-SW] Corrupted or wrong ACK received\n");
                if (expected_fcs != received_fcs) {
                    g_stats.corrupted_count++;
                }
            }
        } else if (bytes == 0) {
            // Timeout: no ACK within the RTO, resend and restart the window timer
            printf("[SENDER-SW] Timeout! Retransmitting frame #%d\n", Sn);
            mark_send_time(Sn);
            channel_send(fd, (uint8_t *)frame, FRAME_SIZE);
            g_stats.frames_retransmitted++;
            timer_stop_window();
            timer_start_window();
        } else if (bytes == -2) {
            // Receiver finished and closed the socket
            return 0;
        } else {
            // Error
            perror("wait_for_ack");
            return -1;
        }
    }
    return 0;
}

// ===== STOP-AND-WAIT MODE IMPLEMENTATION =====
static int sender_sw_mode(int fd, uint8_t *data, size_t file_size) {
    uint16_t Sn = 0;  // Sequence number (toggle 0-1 for Stop-and-Wait)
    size_t offset = 0;

    // Main send loop
    while (offset < file_size) {
        // ===== CREATE DATA FRAME ===
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
        mark_send_time(Sn);
        channel_send(fd, (uint8_t *)&frame, FRAME_SIZE);
        g_stats.frames_sent++;
        printf("[SENDER-SW] Sent DATA frame #%d (offset=%zu, len=%zu)\n", Sn, offset, chunk_len);

        timer_start_window();

        // ===== WAIT FOR ACK WITH TIMEOUT ====
        if (sw_wait_for_ack(fd, &frame, Sn) != 0) {
            return -1;
        }

        // ===== ADVANCE TO NEXT FRAME ====
        offset += chunk_len;
        Sn = (Sn + 1) % 2;  // Toggle: 0->1, 1->0
    }

    // ===== SEND END-OF-TRANSMISSION FRAME =====
    frame_t eot_frame = {0};
    eot_frame.header.frame_type = FRAME_DATA;
    eot_frame.header.frame_seq = htons(Sn);
    eot_frame.header.frame_len = 0;  // Zero-length signals EOT
    uint32_t eot_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&eot_frame, FRAME_SIZE - TRAILER_SIZE);
    eot_frame.trailer.fcs = htonl(eot_fcs);

    mark_send_time(Sn);
    channel_send(fd, (uint8_t *)&eot_frame, FRAME_SIZE);
    g_stats.frames_sent++;
    printf("[SENDER-SW] Sent EOT frame #%d\n", Sn);

    timer_start_window();
    if (sw_wait_for_ack(fd, &eot_frame, Sn) != 0) {
        return -1;
    }

    return 0;
}

void sender_free(arq_config_t *sender) {
    // Cleanup if needed
}

arq_stats_t sender_get_stats(arq_config_t *sender) {
    return g_stats;
}


// ===== HELPER: Wait for ACK and handle retransmit on timeout (GBN mode) =====
// Returns 1 if the receiver closed the connection (transfer done), else 0.
static int gbn_wait_step(int fd, frame_t *frame_store, uint16_t Sw, uint16_t *Sf, uint16_t Sn, bool *timer_running) {
    int peer_closed = 0;
    frame_t ack_frame = {0};
    // Wait one retransmission timeout for an ACK, then resend the window
    double rto_ms = timer_get_rto_ms();
    if (rto_ms <= 0) rto_ms = 1.0;
    struct timeval tv;
    tv.tv_sec = (int)(rto_ms / 1000.0);
    tv.tv_usec = (int)((rto_ms - tv.tv_sec * 1000) * 1000);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);

    if(ret > 0) { // one or more ACKs arrived: drain every ACK that is waiting
        do {
            int bytes = channel_recv(fd , (uint8_t *)&ack_frame , FRAME_SIZE);
            if(bytes <= 0) { peer_closed = 1; break; }
            uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16 , (uint8_t *)&ack_frame , FRAME_SIZE - TRAILER_SIZE);
            uint32_t received_fcs = ntohl(ack_frame.trailer.fcs);

            if (expected_fcs == received_fcs && ack_frame.header.frame_type == FRAME_ACK) {
                uint16_t ackNo = ntohs(ack_frame.header.frame_seq);
                if((ackNo > *Sf) && (ackNo <= Sn)) {
                    // Slide window: acknowledge all frames [Sf, ackNo)
                    while(*Sf < ackNo) {
                        (*Sf)++;
                    }
                    record_rtt(ackNo - 1);
                    g_stats.acks_received++;

                    if (*Sf == Sn) { // window now empty
                        timer_stop_window();
                        *timer_running = false;
                    }
                }
            }
        } while(fd_readable_now(fd));
    } else if(ret == 0) {
        // Timeout: resend entire outstanding window [Sf, Sn)
        printf("[SENDER-GBN] Timeout! Resending frames [%d, %d)\n", *Sf, Sn);
        for(uint16_t i = *Sf; i < Sn; i++) {
            mark_send_time(i);
            channel_send(fd, (uint8_t *)&frame_store[i % Sw], FRAME_SIZE);
            g_stats.frames_retransmitted++;
        }
        timer_start_window();
    }
    return peer_closed;
}

static int sender_gbn_mode(int fd, uint8_t *data, size_t file_size , arq_config_t *sender_config) {
    uint16_t Sw = (1 << sender_config->window_size) - 1;
    uint16_t Sf = 0;
    uint16_t Sn = 0;
    size_t offset = 0;
    bool timer_running = false;

    frame_t frame_store[256];  // Fixed size buffer for window storage

    // Main send loop
    while (offset < file_size) {
        if(Sn - Sf >= Sw) { // window full
            if (gbn_wait_step(fd, frame_store, Sw, &Sf, Sn, &timer_running)) return 0;
            continue;
        }

        // ==== CREATE DATA FRAME =====
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
        frame_store[Sn % Sw] = frame;
        mark_send_time(Sn);
        channel_send(fd, (uint8_t *)&frame, FRAME_SIZE);
        g_stats.frames_sent++;
        printf("[SENDER-GBN] Sent DATA frame #%d (offset=%zu, len=%zu)\n", Sn, offset, chunk_len);

        // Advance sequence number
        Sn++;

        // Start timer ONLY if not already running (pseudocode: "if(timer not running)")
        if (!timer_running) {
            timer_start_window();
            timer_running = true;
        }

        // ===== ADVANCE TO NEXT FRAME ====
        offset += chunk_len;
        // Loop back to check if window full or send next frame
    }

    // ===== SEND END-OF-TRANSMISSION FRAME =====
    frame_t eot_frame = {0};
    eot_frame.header.frame_type = FRAME_DATA;
    eot_frame.header.frame_seq = htons(Sn);
    eot_frame.header.frame_len = 0;  // Zero-length signals EOT
    uint32_t eot_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&eot_frame, FRAME_SIZE - TRAILER_SIZE);
    eot_frame.trailer.fcs = htonl(eot_fcs);

    frame_store[Sn % Sw] = eot_frame;
    mark_send_time(Sn);
    channel_send(fd, (uint8_t *)&eot_frame, FRAME_SIZE);
    g_stats.frames_sent++;
    printf("[SENDER-GBN] Sent EOT frame #%d\n", Sn);

    Sn++;
    if (!timer_running) {
        timer_start_window();
        timer_running = true;
    }

    // ===== DRAIN: Wait for all frames including EOT to be acknowledged =====
    while (Sf < Sn) {
        if (gbn_wait_step(fd, frame_store, Sw, &Sf, Sn, &timer_running)) break;
    }

    return 0;
}

// ===== HELPER: Wait for ACK/NAK and handle retransmit on timeout (SR mode) =====
// Returns 1 if the receiver closed the connection (transfer done), else 0.
static int sr_wait_step(int fd, frame_t *frame_store, timer_slot_t *timers, uint16_t Sw, uint16_t *Sf, uint16_t Sn) {
    int peer_closed = 0;
    // Calculate min timeout from all pending timers
    double min_timeout_ms = timer_get_rto_ms();  // Cap the wait at one RTO
    if (min_timeout_ms <= 0) min_timeout_ms = 1.0;
    uint16_t earliest_seq = UINT16_MAX;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for(uint16_t i = *Sf; i < Sn; i++) {
        timer_slot_t *slot = &timers[i % Sw];
        if(slot->is_pending) {
            double elapsed_ms = (now.tv_sec - slot->send_time.tv_sec) * 1000.0 +
                               (now.tv_nsec - slot->send_time.tv_nsec) / 1e6;
            double remaining = 20.0 - elapsed_ms;  // Fixed 20ms RTO
            if(remaining < 0) remaining = 0;
            if(remaining < min_timeout_ms) {
                min_timeout_ms = remaining;
                earliest_seq = i;
            }
        }
    }

    // select() with computed timeout
    struct timeval tv;
    tv.tv_sec = (int)(min_timeout_ms / 1000.0);
    tv.tv_usec = (int)((min_timeout_ms - tv.tv_sec * 1000) * 1000);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);

    if(ret > 0) {  // one or more ACK/NAK frames arrived: drain all of them
      frame_t ack_frame = {0};
      do {
        int bytes = channel_recv(fd, (uint8_t *)&ack_frame, FRAME_SIZE);
        if(bytes <= 0) { peer_closed = 1; break; }
        {
            uint32_t expected_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&ack_frame, FRAME_SIZE - TRAILER_SIZE);
            uint32_t received_fcs = ntohl(ack_frame.trailer.fcs);

            if(expected_fcs == received_fcs) {
                uint16_t frame_seq = ntohs(ack_frame.header.frame_seq);

                // NAK handling: resend immediately + restart timer
                if(ack_frame.header.frame_type == FRAME_NAK) {
                    if((frame_seq >= *Sf) && (frame_seq < Sn)) {
                        printf("[SENDER-SR] Received NAK for frame #%d, resending\n", frame_seq);
                        mark_send_time(frame_seq);
                        channel_send(fd, (uint8_t *)&frame_store[frame_seq % Sw], FRAME_SIZE);
                        g_stats.frames_retransmitted++;
                        // Restart timer for this frame
                        clock_gettime(CLOCK_MONOTONIC, &timers[frame_seq % Sw].send_time);
                        timers[frame_seq % Sw].is_pending = true;
                    }
                }
                // ACK handling: slide window from Sf up to (not including) frame_seq
                else if(ack_frame.header.frame_type == FRAME_ACK) {
                    if((frame_seq > *Sf) && (frame_seq <= Sn)) {
                        printf("[SENDER-SR] Received ACK for frame #%d\n", frame_seq);
                        record_rtt(frame_seq - 1);
                        g_stats.acks_received++;
                        // Slide window: purge and stop timers
                        while(*Sf < frame_seq) {
                            timers[*Sf % Sw].is_pending = false;  // Stop timer for Sf
                            (*Sf)++;
                        }
                    }
                }
            }
        }
      } while(fd_readable_now(fd));
    } else if(ret == 0) {  // TIMEOUT: find and resend earliest expired frame
        if(earliest_seq != UINT16_MAX) {
            printf("[SENDER-SR] Timeout for frame #%d, resending\n", earliest_seq);
            mark_send_time(earliest_seq);
            channel_send(fd, (uint8_t *)&frame_store[earliest_seq % Sw], FRAME_SIZE);
            g_stats.frames_retransmitted++;
            // Restart timer for this frame
            clock_gettime(CLOCK_MONOTONIC, &timers[earliest_seq % Sw].send_time);
            timers[earliest_seq % Sw].is_pending = true;
        }
    }
    return peer_closed;
}

static int sender_sr_mode(int fd, uint8_t *data, size_t file_size , arq_config_t *sender_config) {
    uint16_t Sw = 1 << (sender_config->window_size - 1);  // Half space: Sw = 2^(m-1)
    uint16_t Sf = 0;
    uint16_t Sn = 0;
    size_t offset = 0;

    frame_t frame_store[256];  // Frame buffer
    timer_slot_t timers[256];  // Per-frame timer tracking
    memset(timers, 0, sizeof(timers));

    // Main send loop
    while (offset < file_size) {
        // ===== WINDOW FULL: WAIT FOR ACK/NAK =====
        if(Sn - Sf >= Sw) {
            if (sr_wait_step(fd, frame_store, timers, Sw, &Sf, Sn)) return 0;
            continue;
        }

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

        // ===== SEND FRAME & START TIMER =====
        frame_store[Sn % Sw] = frame;
        mark_send_time(Sn);
        channel_send(fd, (uint8_t *)&frame, FRAME_SIZE);
        g_stats.frames_sent++;
        printf("[SENDER-SR] Sent DATA frame #%d (offset=%zu, len=%zu)\n", Sn, offset, chunk_len);

        // Start timer for THIS frame (fix pseudocode bug: use Sn, not Sn+1)
        clock_gettime(CLOCK_MONOTONIC, &timers[Sn % Sw].send_time);
        timers[Sn % Sw].seq_num = Sn;
        timers[Sn % Sw].is_pending = true;

        // Advance sequence number and offset
        Sn++;
        offset += chunk_len;
    }

    // ===== SEND END-OF-TRANSMISSION FRAME =====
    frame_t eot_frame = {0};
    eot_frame.header.frame_type = FRAME_DATA;
    eot_frame.header.frame_seq = htons(Sn);
    eot_frame.header.frame_len = 0;  // Zero-length signals EOT
    uint32_t eot_fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&eot_frame, FRAME_SIZE - TRAILER_SIZE);
    eot_frame.trailer.fcs = htonl(eot_fcs);

    frame_store[Sn % Sw] = eot_frame;
    mark_send_time(Sn);
    channel_send(fd, (uint8_t *)&eot_frame, FRAME_SIZE);
    g_stats.frames_sent++;
    printf("[SENDER-SR] Sent EOT frame #%d\n", Sn);

    clock_gettime(CLOCK_MONOTONIC, &timers[Sn % Sw].send_time);
    timers[Sn % Sw].seq_num = Sn;
    timers[Sn % Sw].is_pending = true;

    Sn++;

    // ===== DRAIN: Wait for all frames including EOT to be acknowledged =====
    while (Sf < Sn) {
        if (sr_wait_step(fd, frame_store, timers, Sw, &Sf, Sn)) break;
    }

    return 0;
}