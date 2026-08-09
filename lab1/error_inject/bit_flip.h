#ifndef BIT_FLIP_H
#define BIT_FLIP_H
 
#include <stdint.h>
#include <stddef.h>
 
// Flips exactly one bit in buf, addressed by a global bit index
void flip_bit(uint8_t *buf, size_t len, size_t bit_index);
 
// Returns a uniformly random valid bit index in [0, 8*len)
size_t random_bit_index(size_t len);

void inject_burst_error(uint8_t *buf, size_t len, int burst_len);
 
#endif