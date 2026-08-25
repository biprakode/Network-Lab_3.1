#include "targeted_inject.h"
#include "bit_flip.h"

void inject_compensating_error(uint8_t *buf, size_t len) {
    size_t word_count = len / 2;
    if (word_count < 2) {
        printf("Need at least 2 complete words - cannot inject compensating error\n");
        return;
    }

    size_t w1, w2;
    unsigned int c;
    int bit1, bit2;

    do {
        w1 = (size_t)rand() % word_count;
        do {
            w2 = (size_t)rand() % word_count;
        } while (w1 == w2);

        c = (unsigned int)(rand() % 16);

        size_t byte1 = (c < 8) ? 2 * w1 + 1 : 2 * w1;
        size_t bit_in_byte1 = (c < 8) ? c : c - 8;
        bit1 = (buf[byte1] >> bit_in_byte1) & 1;

        size_t byte2 = (c < 8) ? 2 * w2 + 1 : 2 * w2;
        size_t bit_in_byte2 = (c < 8) ? c : c - 8;
        bit2 = (buf[byte2] >> bit_in_byte2) & 1;

    } while (bit1 == bit2); // keep re-choosing bit1/bit2 until unequal

    // finally flip

    size_t byte1 = (c < 8) ? 2 * w1 + 1 : 2 * w1;
    size_t bit_in_byte1 = (c < 8) ? c : c - 8;
    size_t global_bit1 = byte1 * 8 + bit_in_byte1;

    size_t byte2 = (c < 8) ? 2 * w2 + 1 : 2 * w2;
    size_t bit_in_byte2 = (c < 8) ? c : c - 8;
    size_t global_bit2 = byte2 * 8 + bit_in_byte2;

    flip_bit(buf, len, global_bit1);
    flip_bit(buf, len, global_bit2);
}


void inject_generator_multiple_error(uint8_t *buf, size_t len, crc_gen params) {
    size_t required = (params.degree + 8) / 8;

    if (len < required) {
        return;
    }

    // walk from MSB for poly degree
    for (int k = 0; k <= params.degree; k++) {
        int bit_val = 0;
        if (k == 0) {
            bit_val = 1;  // Leading 1 for x^degree
        } else { 
            bit_val = (params.poly >> (params.degree - k)) & 1; // extract bit (degree - k) from params.poly
        }

        // If bit_val is 1, XOR into the buffer from MSB
        if (bit_val) {
            size_t byte_offset = (size_t)k / 8;
            int bit_from_msb = k % 8;
            int bit_in_byte = 7 - bit_from_msb; // Bit 7 is MSB, Bit 0 is LSB
            size_t global_bit = byte_offset * 8 + bit_in_byte;

            flip_bit(buf, len, global_bit);

        }
        
    }
}
