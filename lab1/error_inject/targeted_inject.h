#ifndef TARGETED_INJECT_H
#define TARGETED_INJECT_H
 
#include <stdint.h>
#include <stddef.h>
#include "error.h"   /* for crc_gen */
#include <stdlib.h>
#include <stdio.h>

 
/* Checksum-blind, CRC-catches. Flips the same bit-column in two
 * distinct 16-bit words within [buf, buf+len). Requires len >= 4
 * (at least two full words to pick from — assert this). */
void inject_compensating_error(uint8_t *buf, size_t len);
 
/* CRC-blind, checksum-catches. XORs the FULL generator polynomial
 * pattern — including the implicit leading term — into buf, starting
 * at byte 0 bit 7 (fixed, not randomized — see design note in the .c
 * file for why). Requires len >= ceil((params.degree + 1) / 8) bytes. */
void inject_generator_multiple_error(uint8_t *buf, size_t len, crc_gen params);
 
#endif