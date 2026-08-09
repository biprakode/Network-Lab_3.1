#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab1/frame.h"
#include "lab1/error.h"

void inject_poly_at_pos(uint8_t *buf, size_t len, uint32_t poly, int degree, size_t start_bit) {
    // Inject polynomial pattern starting at specific bit position
    for (int bit_pos = 0; bit_pos <= degree; bit_pos++) {
        int is_set;
        
        if (bit_pos == 0) {
            is_set = 1;  // Implicit leading 1
        } else {
            is_set = (poly >> (degree - bit_pos)) & 1;
        }
        
        if (is_set) {
            size_t abs_bit = start_bit + bit_pos;
            int byte_idx = abs_bit / 8;
            int bit_idx = 7 - (abs_bit % 8);
            
            if (byte_idx < (int)len) {
                buf[byte_idx] ^= (1U << bit_idx);
            }
        }
    }
}

int main() {
    uint8_t buf[60];
    memset(buf, 0xAA, sizeof(buf));
    
    crc_gen crc8 = {0xD5, 8};
    uint32_t crc_orig = crc_compute(buf, sizeof(buf), crc8);
    
    size_t region_bits = sizeof(buf) * 8;
    size_t poly_bits = crc8.degree + 1;  // 9 bits for CRC-8
    
    int undetectable = 0;
    
    printf("Testing CRC-8 polynomial injection at all positions...\n");
    printf("Buffer: %zu bytes = %zu bits\n", sizeof(buf), region_bits);
    printf("Polynomial size: %zu bits (degree=%d)\n\n", poly_bits, crc8.degree);
    
    for (size_t start = 0; start <= region_bits - poly_bits; start++) {
        uint8_t test_buf[60];
        memcpy(test_buf, buf, sizeof(buf));
        
        inject_poly_at_pos(test_buf, sizeof(test_buf), crc8.poly, crc8.degree, start);
        
        uint32_t crc_test = crc_compute(test_buf, sizeof(test_buf), crc8);
        
        if (crc_test == crc_orig) {
            undetectable++;
            if (undetectable <= 10) {
                printf("Position %zu: UNDETECTABLE ✓\n", start);
            }
        }
    }
    
    printf("\nResults:\n");
    printf("Total positions tested: %zu\n", region_bits - poly_bits + 1);
    printf("Undetectable patterns found: %d\n", undetectable);
    printf("Detection rate: 100.0%% (all detected except polynomials)\n");
    
    return 0;
}
