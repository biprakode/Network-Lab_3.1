#include <stdint.h>
#include "channel.h"
#include "protocol.h"

#include "../utils/utils.h"
#include <arpa/inet.h>
#include <stdio.h>
#include "../lab1/error.h"
#include "../lab1/scheme.h"

typedef struct { // state from one slot
    int connected; //0 after ACT_DONE or shutdown//
    int active; //mid-transmission
    int start_slot; //slot the current ACT_START landed
    int collided; //current transmission has overlapped
    uint32_t enqueue_slot; //from csma_sim_t of the START frame//
    frame_t frame; //captured on ACT_START
    result_t pending_result; //send on next tick, then reset
} chan_station_t;

chan_station_t st[256]; // Max n_stations = 256

static const csma_sim_t *frame_sim_csma(const frame_t *frame) {
    return (const csma_sim_t *)frame->payload;
}

medium_state_t medium_from_txcount(int txcount) {
    if (txcount == 0) return MEDIUM_IDLE;
    if (txcount == 1) return MEDIUM_BUSY;
    return MEDIUM_COLLISION;
}

slot_outcome_t channel_resolve_slot(const uint8_t *actions, int n, medium_state_t prev_state) {
    int tx = 0;
    int starts = 0;

    for(int i =0 ; i<n ; i++) {
        action_type_t action = (action_type_t)actions[i];
        if(action == ACT_START) {
            tx++;
            starts++;
        } else if(action == ACT_TRANSMIT || action == ACT_JAM) {
            tx++;
        }
    }

    medium_state_t medium_state = medium_from_txcount(tx);
    int new_collision_event = (medium_state == MEDIUM_COLLISION && prev_state != MEDIUM_COLLISION);

    return (slot_outcome_t){.state=medium_state , .transmitters=tx , .starts=starts , .new_collision_event=new_collision_event};
}

int channel_serve(const channel_params_t *params, channel_metrics_t *out) {
    int listen_fd = tcp_listen(params->port , params->n_stations);
    if(listen_fd < 0) return -1;

    int conn[256];

    for(int id = 0; id<params->n_stations ; id++) {
        conn[id] = tcp_accept(listen_fd);
        if(conn[id] < 0) return -1;

        config_msg_t cfg;
        cfg.slot_frame_len = htonl((uint32_t)params->slot_frame_len);
        cfg.max_slots = htonl((uint32_t)params->max_slots);
        cfg.detect_collisions = (uint8_t)params->detect_collisions;
        cfg.station_id = (uint8_t)id;
        cfg._pad[0] = cfg._pad[1] = 0;

        if(send_all(conn[id] , &cfg , sizeof(cfg)) != (sizeof(cfg))) {
            return -1;
        }

        st[id].connected = 1;
        st[id].active = 0;
        st[id].pending_result = RES_NONE;
    }
    close_conn(listen_fd);

    uint32_t slot = 0;
    medium_state_t prev_state = MEDIUM_IDLE;
    int n_connected = params->n_stations;
    channel_metrics_t metrics = {0};
    uint8_t actions_buf[256]; // buffer for action type

    action_msg_t acts[256]; // buffer for action msg
    
    while(slot < (uint32_t)params->max_slots && n_connected > 0) {
        // send tick message to all connected stations
        for(int id = 0; id<params->n_stations ; id++) {
            if(st[id].connected) {
                tick_msg_t tick_msg;
                tick_msg.slot = htonl(slot);
                tick_msg.medium_state = (uint8_t)prev_state;
                tick_msg.last_result = (uint8_t)st[id].pending_result;
                tick_msg.should_stop = 0;
                tick_msg._pad = 0;

                if(send_all(conn[id] , &tick_msg , sizeof(tick_msg)) != (sizeof(tick_msg))) {
                    return -1;
                }

                st[id].pending_result = RES_NONE; // nothing pending
            }
        }

        // listen action of each station
        for(int id = 0; id<params->n_stations ; id++) {
            if(st[id].connected) {
                if(recv_all(conn[id] , &(acts[id]) , sizeof(action_msg_t)) != sizeof(action_msg_t)) {
                    return -1;
                }
                
                uint32_t station_slot = ntohl(acts[id].slot);
                if(station_slot != slot) {
                    fprintf(stderr, "channel: desync, station %d sent slot %u, expected %u\n", id, station_slot, slot);
                    return -1;
                }

                actions_buf[id] = acts[id].action; // channel statis - idle/start/trasmit/jam/done
            }
        }


        slot_outcome_t outcome = channel_resolve_slot(actions_buf , params->n_stations , prev_state);
        medium_state_t state = outcome.state;
        metrics.total_slots++;

        if (state == MEDIUM_IDLE) {
            metrics.idle_slots++;
        } else if (state == MEDIUM_BUSY) {
            metrics.busy_slots++;
        } else {
            metrics.collision_slots++;
            if(outcome.new_collision_event) metrics.collision_events++;
        }

        for(int id = 0; id<params->n_stations ; id++) { // parse each state action into st
            if(st[id].connected) {
                action_type_t action_type = (action_type_t)actions_buf[id];

                if(action_type == ACT_START) {
                    st[id].active = 1;
                    st[id].start_slot = slot;
                    st[id].collided = 0;
                    st[id].frame = acts[id].frame;
                    st[id].enqueue_slot = ntohl(frame_sim_csma(&st[id].frame)->enqueue_slot);
                    metrics.attempts++;
                }

                else if(action_type == ACT_DONE) {
                    if (st[id].active == 1) return -1; // leaving mid transmission 
                    st[id].connected = 0;
                    n_connected--;
                }
            }
        }

        if(state == MEDIUM_COLLISION) { // if this slot was collision
            for(int id = 0; id<params->n_stations ; id++) {
            if(st[id].connected && st[id].active) {
                    if(actions_buf[id] == ACT_START || actions_buf[id] == ACT_TRANSMIT || actions_buf[id] == ACT_JAM) {
                        st[id].collided++;
                    }
                }
            }
        }

        // end any transmission which is getting over in this slot
        for(int id = 0; id<params->n_stations ; id++) {
            if(st[id].connected && st[id].active == 1) {
                action_type_t action = actions_buf[id];

                if(action == ACT_JAM) {
                    st[id].active = 0;
                    st[id].pending_result = RES_COLLISION;
                    metrics.collided_attempts += 1;
                    continue;
                }

                uint32_t frame_age = slot - st[id].start_slot + 1;

                if(frame_age >= params->slot_frame_len) {
                    if(st[id].collided) {

                        st[id].pending_result = RES_COLLISION;
                        metrics.collided_attempts++;
                    } else {

                        if(verify_fcs(SCHEME_CHECKSUM16, (uint8_t *)&st[id].frame, FRAME_SIZE - TRAILER_SIZE , ntohl(st[id].frame.trailer.fcs))) {

                            metrics.frames_delivered++;
                            metrics.sum_delay_slots += (st[id].start_slot + params->slot_frame_len - st[id]. enqueue_slot);
                            st[id].pending_result = RES_SUCCESS;
                        }else{

                            metrics.fcs_failures += 1;
                            st[id].pending_result = RES_COLLISION;
                        }
                    }

                    st[id].active = 0;
                }
            }
        }

        prev_state = state;
        slot++;
    }

    //shutdown all stations
    for(int id = 0; id<params->n_stations ; id++) {
        // send stop tick to all connected stations
        if(st[id].connected) {
            tick_msg_t tick_msg;
            tick_msg.slot = htonl(slot);
            tick_msg.medium_state = (uint8_t)prev_state;
            tick_msg.last_result = RES_NONE;
            tick_msg.should_stop = 1;
            tick_msg._pad = 0;

            send_all(conn[id], &tick_msg, sizeof(tick_msg_t)); 
            close_conn(conn[id]);
        }
    }

    metrics.throughput = (double)(metrics.frames_delivered * params->slot_frame_len) / (double) metrics.total_slots;

    if (metrics.frames_delivered > 0) {
        metrics.avg_delay_slots = (double) metrics.sum_delay_slots / metrics.frames_delivered;
    } else {
        metrics.avg_delay_slots = 0.0; // to avoid dividebyzero
    }


    *out = metrics;
    return 0;

}