#ifndef RECEIVER_H
#define RECEIVER_H

#include <stdint.h>
#include "../../lab1/frame.h"
#include "../timer.h"
#include "../channel.h"
#include "arq.h"

// Initialize receiver with config
void receiver_init(arq_config_t *receiver_config);

// Main receiver loop - receive frame -> verify -> deliver data -> send ACK
int receiver_run(arq_config_t *receiver_config);

// Cleanup
void receiver_free(arq_config_t *receiver);

// Get receiver stats
arq_stats_t receiver_get_stats(arq_config_t *receiver);

#endif
