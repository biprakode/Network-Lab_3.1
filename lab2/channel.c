#define _POSIX_C_SOURCE 200809L
#include "channel.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../utils/utils.h"
#include "../lab1/error_inject/bit_flip.h"

static channel_config c_config;
static channel_stats c_stat = {0};

double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

void channel_init(channel_config config) {
    c_config = config;
    srand(time(NULL));
}

int channel_send(int fd, const uint8_t *data, int len) {
    if (rand_double() < c_config.loss_prob) {
        c_stat.frames_lost++;
        printf("[CHANNEL] Frame lost while sending\n");
        return -1;
    }

    uint8_t *frame_copy = malloc(len); // only apply corruption to frame_copy
    if (!frame_copy) {
        perror("malloc");
        return -1;
    }
    memcpy(frame_copy, data, len);

    if (rand_double() < c_config.corruption_prob) {
        c_stat.frames_corrupted++;
        size_t bit_index = random_bit_index(len);
        flip_bit(frame_copy, len, bit_index);
        printf("[CHANNEL] Frame corrupted at bit %zu (byte %zu, bit %zu)\n", bit_index, bit_index / 8, bit_index % 8);
    }

    // c nanosleep
    if (c_config.delay_ms > 0) { 
        struct timespec req = {0};
        req.tv_nsec = (long)(c_config.delay_ms * 1e6);
        nanosleep(&req, NULL);
    }

    c_stat.frames_sent++;
    int result = send_all(fd, frame_copy, len);
    free(frame_copy);
    return result;
}

int channel_recv(int fd, uint8_t *buffer, int len) {
    int bytes_recv = recv_all(fd, buffer, len);
    if (bytes_recv < 0) {
        return bytes_recv;
    }

    if (rand_double() < c_config.loss_prob) {
        c_stat.frames_lost++;
        printf("[CHANNEL] Frame lost while receiving\n");
        return -1;
    }

    if (rand_double() < c_config.corruption_prob) {
        c_stat.frames_corrupted++;
        size_t bit_index = random_bit_index(len);
        flip_bit(buffer, len, bit_index);
        printf("[CHANNEL] Frame corrupted at bit %zu (byte %zu, bit %zu)\n", bit_index, bit_index / 8, bit_index % 8);
    }

    if (c_config.delay_ms > 0) {
        struct timespec req = {0};
        req.tv_nsec = (long)(c_config.delay_ms * 1e6);
        nanosleep(&req, NULL);
    }

    return bytes_recv;
}

channel_stats channel_get_stats(void) {
    return c_stat;
}

void channel_reset_stats(void) {
    memset(&c_stat, 0, sizeof(c_stat));
}
