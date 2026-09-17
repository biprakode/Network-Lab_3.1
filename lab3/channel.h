#ifndef LAB3_CHANNEL_H
#define LAB3_CHANNEL_H

#include <stdint.h>
#include "protocol.h"

typedef struct {
    uint16_t port;
    int n_stations;
    int slot_frame_len; // T_frame in slots, >= 2
    int detect_collisions; // 1 = CSMA/CD run, 0 = plain CSMA
    int max_slots; // hard run cap
    unsigned seed;  // reserved: noisy-medium mode; 0 = off 
    const char *sink_path;
} channel_params_t;

typedef struct {
    uint64_t total_slots;
    uint64_t idle_slots;
    uint64_t busy_slots;
    uint64_t collision_slots;
    uint64_t collision_events; // # contiguous COLLISION periods       
    uint64_t collided_attempts; // # station-transmissions lost to collision
    uint64_t attempts; // # ACT_START seen    
    uint64_t frames_delivered; // # successful transmissions   
    uint64_t fcs_failures; // delivered but FCS bad (0 unless noisy
    uint64_t sum_delay_slots; // Σ (start_slot + T_frame - enqueue_slot) over successes
    double throughput; // frames_delivered * T_frame / total_slots
    double avg_delay_slots; // sum_delay_slots / frames_delivered   
} channel_metrics_t;


typedef struct {
    medium_state_t state;
    int transmitters;  // count of START + TRANSMIT + JAM this slot
    int starts;  // count of ACT_START this slot     
    int new_collision_event; // 1 iff prev_state != COLLISION && state == COLLISION
} slot_outcome_t;

medium_state_t medium_from_txcount(int txcount);

// actions: array of action_type_t values (one per still-connected station)
slot_outcome_t channel_resolve_slot(const uint8_t *actions, int n, medium_state_t prev_state);

int channel_serve(const channel_params_t *params, channel_metrics_t *out);


#endif // LAB3_CHANNEL_H