#include "bit_flip.h"
#include <stdlib.h>

void flip_bit(uint8_t *buf, size_t len, size_t bit_index) {
    int byte_index = bit_index / 8;
    int bit_in_byte = bit_index % 8;

    if (byte_index < (int)len) {
        buf[byte_index] ^= (1U << bit_in_byte);
    }
}

size_t random_bit_index(size_t len) {
    return rand() % (8 * len);
}

void inject_burst_error(uint8_t *buf, size_t len, int burst_len) {
    size_t total_bits = len * 8;
    if (burst_len <= 0 || (size_t)burst_len > total_bits) {
        return;
    }

    //random valid start bit offset so the burst fits
    size_t max_start_bit = total_bits - (size_t)burst_len;
    size_t start_bit = (size_t)rand() % (max_start_bit + 1);
    size_t end_bit   = start_bit + burst_len - 1;

    flip_bit(buf, len, start_bit); // first bit flipped
    flip_bit(buf, len, end_bit); // last bit flipped


    for (size_t bit = start_bit + 1; bit < end_bit; bit++) {
        flip_bit(buf, len, bit);
    }
}

