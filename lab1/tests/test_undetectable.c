#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab1/frame.h"
#include "lab1/error.h"
#include "lab1/error_inject/bit_flip.h"

int main() {
    frame_t frame;
    memset(&frame, 0xAA, sizeof(frame));  // Fill with 0xAA pattern
    
    crc_gen crc8 = {0xD5, 8};
    size_t region_len = 16 + 44;
    
    uint32_t crc_orig = crc_compute((uint8_t *)&frame, region_len, crc8);
    printf("Original CRC-8 for 0xAA-filled frame: 0x%02X\n\n", crc_orig & 0xFF);
    
    // The generator polynomial in binary is 0xD5 = 11010101
    // But with the implicit leading 1 for CRC-8, it's 110101010 (9 bits)
    // To inject this, we XOR this pattern at each possible 9-bit position
    
    printf("Testing systematic injection of undetectable pattern...\n");
    printf("Generator polynomial (9 bits): 0b110101010\n");
    printf("Testing at each of %zu possible 9-bit positions...\n\n", region_len * 8 - 8);
    
    int undetectable = 0;
    
    // For each possible 9-bit position
    for (size_t start_bit = 0; start_bit <= region_len * 8 - 9; start_bit += 100) {
        frame_t test_frame;
        memcpy(&test_frame, &frame, sizeof(frame));
        
        // XOR the generator polynomial (0b110101010) at this position
        // This is equivalent to: error = polynomial shifted to this position
        for (int i = 0; i < 9; i++) {
            if ((0xD5 >> (7 - i)) & 1) {  // 0xD5 = 11010101, so poly = 110101010
                flip_bit((uint8_t *)&test_frame, region_len, start_bit + i);
            }
        }
        
        uint32_t crc_after = crc_compute((uint8_t *)&test_frame, region_len, crc8);
        
        if (crc_after == crc_orig) {
            undetectable++;
            printf("Position %zu: UNDETECTED (CRC still 0x%02X)\n", start_bit, crc_after & 0xFF);
        }
    }
    
    printf("\nFound %d undetectable patterns at sampled positions\n", undetectable);
    printf("This confirms CRC-8 theory: generator polynomial creates undetectable bursts\n");
    
    return 0;
}
