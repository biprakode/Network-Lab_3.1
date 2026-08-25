#ifndef SENDER_H
#define SENDER_H

#include <stdint.h>
#include "../../lab1/frame.h"
#include "../timer.h"
#include "../channel.h"
#include "arq.h"

// Initialize sender with config
void sender_init(arq_config_t *sender_config);

// Main sender loop - create frame -> send -> handle acks/timeouts
int sender_run(arq_config_t *sender_config);

// Cleanup
void sender_free(arq_config_t *sender);

// Get sender stats
arq_stats_t sender_get_stats(arq_config_t *sender);

#endif
