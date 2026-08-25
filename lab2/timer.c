#define _POSIX_C_SOURCE 200809L
#include "timer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static timer_config t_config;
static timer_stats t_stats = {0};

// for S&W and GBN
static uint64_t t_window_start_time = 0;
static int t_window_running = 0;

#define MAX_SLOTS 65536 // 2^16 possible timers
static timer_slot t_slots[MAX_SLOTS] = {0};

// For adaptive RTO (EMA)
static double t_avg_rtt_ms = 0.0;
static int t_rtt_samples = 0;

uint64_t timer_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void timer_init(timer_config config) {
    t_config = config;
    memset(t_slots, 0, sizeof(t_slots));
    t_window_running = 0;
    t_window_start_time = 0;
}

void timer_start_window(void) {
    // Idempotent start
    if (!t_window_running) {
        t_window_start_time = timer_now_ns();  // Capture current time in nanoseconds
        t_window_running = 1;                   // Mark as active
        t_stats.windows_started++;              // Increment counter for stats
    }
    // already running
}

void timer_stop_window(void) {
    if (t_window_running) {
        t_window_running = 0;  // Mark as inactive
    }
}

int timer_window_expired(void) {
    // Check if (elapsed time >= RTO) return 1 if expired

    if (!t_window_running) {
        return 0;
    }

    uint64_t now_ns = timer_now_ns();
    uint64_t elapsed_ns = now_ns - t_window_start_time;

    // Convert RTO from milliseconds to nanoseconds
    uint64_t rto_ns = (uint64_t)(t_config.rto_ms * 1e6);

    if (elapsed_ns >= rto_ns) {
        t_stats.windows_expired++;
        return 1; // timer fired
    }

    return 0;  //not yet expired
}

void timer_start_frame(uint16_t seq_num) {
    if(!t_slots[seq_num].in_use) {
        t_slots[seq_num].in_use = 1;
        t_slots[seq_num].send_time_ns = timer_now_ns();
        t_slots[seq_num].seq_num = seq_num;
        t_stats.frames_started++;
    }
}

void timer_stop_frame(uint16_t seq_num) {
    if(t_slots[seq_num].in_use) {
        t_slots[seq_num].in_use = 0;
    }
}

int timer_frame_expired(uint16_t seq_num) {
    if(!t_slots[seq_num].in_use) {
        return 0;
    }

    uint64_t now_ns = timer_now_ns();
    uint64_t elapsed_ns = now_ns - t_slots[seq_num].send_time_ns;
    uint64_t rto_ns = (uint64_t)(t_config.rto_ms * 1e6);

    if (elapsed_ns >= rto_ns) {
        t_stats.frames_expired++;
        return 1; // timer fired
    }

    return 0;    
}

double timer_frame_remaining_ms(uint16_t seq_num) {
    if(!t_slots[seq_num].in_use) {
        return 0;
    }

    uint64_t now_ns = timer_now_ns();
    uint64_t elapsed_ns = now_ns - t_slots[seq_num].send_time_ns;
    uint64_t rto_ns = (uint64_t)(t_config.rto_ms * 1e6);

    double remaining_ns = (rto_ns > elapsed_ns) ? (rto_ns - elapsed_ns) : 0;
    return remaining_ns / 1e6;  // Convert ns to ms
}

//loop over all timers and find min deadline to expire
double timer_earliest_deadline_ms(void) {
    double min_deadline = t_config.rto_ms;
    for(int i = 0 ; i<MAX_SLOTS ; i++) {
        if(!t_slots[i].in_use) continue;

        uint64_t now_ns = timer_now_ns();
        double deadline = (t_slots[i].send_time_ns + (uint64_t)t_config.rto_ms * 1e6 - now_ns) / 1e6;
        min_deadline = (deadline < min_deadline) ? deadline : min_deadline;
    }
    return (min_deadline > 0.0) ? min_deadline : 0.0;
}

void timer_record_rtt(double rtt_ms) {
    if (t_rtt_samples == 0) {
        t_avg_rtt_ms = rtt_ms;  // First sample
    } else {
        t_avg_rtt_ms = 0.9 * t_avg_rtt_ms + 0.1 * rtt_ms;
    }
    t_rtt_samples++;
}


double timer_get_rto_ms(void) {
    return t_config.use_ema ? 2*t_avg_rtt_ms : t_config.rto_ms;
}

timer_stats timer_get_stats(void) {
    return t_stats;
}

void timer_reset_stats(void) {
    memset(&t_stats, 0, sizeof(t_stats));
}