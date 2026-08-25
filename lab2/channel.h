#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdio.h>
#include <stdint.h>
#include "../lab1/frame.h"

typedef struct {
    double loss_prob; 
    double corruption_prob; // bit flip
    double delay_ms;
} channel_config;

void channel_init(channel_config config);

int channel_send(int fd , const uint8_t *data , int len);
int channel_recv(int fd, uint8_t *buffer, int len);

typedef struct {
    uint64_t frames_sent;
    uint64_t frames_lost;
    uint64_t frames_corrupted;
} channel_stats;

channel_stats channel_get_stats(void);
void channel_reset_stats(void);


#endif