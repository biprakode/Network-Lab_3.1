#ifndef LAB3_PROTOCOL_H
#define LAB3_PROTOCOL_H

#include <stdint.h>
#include "frame.h"

typedef enum {
    MEDIUM_IDLE = 0,
    MEDIUM_BUSY = 1,
    MEDIUM_COLLISION = 2
} medium_state_t;

typedef enum { // channel status
    ACT_IDLE = 0, 
    ACT_START = 1, 
    ACT_TRANSMIT = 2,
    ACT_JAM = 3,
    ACT_DONE = 4 
} action_type_t;

typedef enum { // result of last transmission
    RES_NONE = 0,
    RES_SUCCESS = 1,
    RES_COLLISION = 2
} result_t;

#pragma pack(push,1)
typedef struct {
    uint32_t slot_frame_len; // t_frame
    uint32_t max_slots;
    uint8_t  detect_collisions;
    uint8_t  station_id;
    uint8_t  _pad[2];
} config_msg_t;

typedef struct {
    uint32_t slot; // slot index
    uint8_t  medium_state; // medium_state_t, as of slot-1 (carrier sense)
    uint8_t  last_result;  // result_t for this station's just-ended attempt
    uint8_t  should_stop; //exit experiment
    uint8_t  _pad;
} tick_msg_t;

typedef struct {
    uint32_t slot;
    uint8_t  action; // action_type_t
    uint8_t  _pad[3];
    frame_t  frame;
} action_msg_t;

typedef struct {
    uint32_t enqueue_slot; // sim-only: slot this frame became head-of-line
    uint32_t frame_uid; // sim-only: unique id
} csma_sim_t; // 8 bytes


#pragma pack(pop)



#endif // LAB3_PROTOCOL_H