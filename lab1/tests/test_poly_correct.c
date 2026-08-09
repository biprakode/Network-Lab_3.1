#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab1/frame.h"
#include "lab1/error.h"

// Manually implement bit flipping to understand the pattern
void inject_poly_burst(uint8_t *buf, size_t len, uint32_t poly, int degree) {
    // Inject the polynomial as a burst error
    // Polynomial has (degree + 1) bits including implicit leading 1
    
    // For CRC-8 with poly=0xD5, degree=8:
    // Full polynomial = 1 followed by the 8 bits of 0xD5
    // = 1 1010101 = 0xD5 (wait, let me compute this correctly)
    
    // 0xD5 = 1101 0101 in binary
    // With implicit leading 1: 1 1101 0101
    
    // Let's use byte 0, bit 0 as start position
    // Flip bits corresponding to polynomial coefficients
    
    for (int bit_pos = 0; bit_pos <= degree; bit_pos++) {
        int is_set;
        
        if (bit_pos == 0) {
            // Leading coefficient (implicit 1)
            is_set = 1;
        } else {
            // Coefficient from position (degree - bit_pos)
            is_set = (poly >> (degree - bit_pos)) & 1;
        }
        
        if (is_set) {
            int byte_idx = bit_pos / 8;
            int bit_idx = 7 - (bit_pos % 8);  // MSB first
            buf[byte_idx] ^= (1U << bit_idx);
        }
    }
}

int main() {
    uint8_t buf[60];
    memset(buf, 0xAA, sizeof(buf));
    
    crc_gen crc8 = {0xD5, 8};
    
    uint32_t crc_orig = crc_compute(buf, sizeof(buf), crc8);
    printf("Original CRC-8: 0x%02X\n", crc_orig & 0xFF);
    
    // Inject polynomial burst
    uint8_t buf2[60];
    memcpy(buf2, buf, sizeof(buf));
    inject_poly_burst(buf2, sizeof(buf2), crc8.poly, crc8.degree);
    
    uint32_t crc_after = crc_compute(buf2, sizeof(buf2), crc8);
    printf("CRC after polynomial burst injection: 0x%02X\n", crc_after & 0xFF);
    
    if (crc_orig == crc_after) {
        printf("✓ UNDETECTABLE - CRC unchanged!\n");
    } else {
        printf("✗ DETECTED - CRC changed\n");
    }
    
    // Show which bits were flipped
    printf("\nFlipped bits (polynomial pattern for CRC-8):\n");
    printf("0xD5 = 0b11010101 with implicit leading 1 = 0b110101010\n");
    printf("Bytes 0-1 after XOR with polynomial:\n");
    for (int i = 0; i < 2; i++) {
        printf("  byte[%d]: 0x%02X\n", i, buf2[i]);
    }
    
    return 0;
}
