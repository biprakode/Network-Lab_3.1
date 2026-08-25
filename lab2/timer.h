#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <time.h>

typedef struct {
    double rto_ms; // retransmission timeout
    int use_ema; // adaptive RTO
} timer_config;

typedef struct {
    int in_use;
    uint64_t send_time_ns; // when frame was sent
    uint16_t seq_num; // seq no. of frame
} timer_slot;

void timer_init(timer_config config);

// Get current time in nanoseconds (monotonic clock)
uint64_t timer_now_ns(void);

// Stop-and-Wait & Go-Back-N (single window timer)
void timer_start_window(void);
void timer_stop_window(void);
int timer_window_expired(void);

// Selective Repeat (per-frame timers)
// Start timer for a specific frame
void timer_start_frame(uint16_t seq_num);

// Stop timer for a specific frame
void timer_stop_frame(uint16_t seq_num);

// Check if frame's timer has expired
int timer_frame_expired(uint16_t seq_num);

// Get timeout remaining for frame (in ms), 0 if expired or not running
double timer_frame_remaining_ms(uint16_t seq_num);

// Get earliest deadline across all active frames
double timer_earliest_deadline_ms(void);

// Adaptive RTO (optional
void timer_record_rtt(double rtt_ms);

// Get current RTO (fixed or EMA-adapted)
double timer_get_rto_ms(void);

// Stats & Reset
typedef struct {
    uint64_t windows_started;
    uint64_t windows_expired;
    uint64_t frames_started;
    uint64_t frames_expired;
    double avg_rtt_ms;
    double current_rto_ms;
} timer_stats;

timer_stats timer_get_stats(void);
void timer_reset_stats(void);

#endif