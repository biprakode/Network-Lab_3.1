#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab1/frame.h"
#include "lab1/error.h"
#include "lab1/error_inject/bit_flip.h"

int main() {
    // Create a frame with known payload
    frame_t frame;
    memset(&frame, 0, sizeof(frame));
    memset(frame.payload, 0xAA, sizeof(frame.payload));
    
    // Test CRC-8
    crc_gen crc8 = {0xD5, 8};
    
    size_t region_len = 16 + 44;  // HEADER + PAYLOAD
    
    // Compute original CRC
    uint32_t crc_orig = crc_compute((uint8_t *)&frame, region_len, crc8);
    printf("Original CRC-8: 0x%02X\n", crc_orig & 0xFF);
    
    // Inject burst of 9 bits at various positions and track detection rate
    int misses = 0, catches = 0;
    
    for (int trial = 0; trial < 100000; trial++) {
        frame_t test_frame;
        memcpy(&test_frame, &frame, sizeof(frame));
        
        // Inject 9-bit burst (r+1 where r=8)
        inject_burst_error((uint8_t *)&test_frame, region_len, 9);
        
        // Check if detected
        uint32_t crc_corrupted = crc_compute((uint8_t *)&test_frame, region_len, crc8);
        
        if (crc_corrupted == crc_orig) {
            misses++;
            if (misses <= 5) {
                printf("MISS at trial %d: CRC stayed 0x%02X\n", trial, crc_corrupted & 0xFF);
            }
        } else {
            catches++;
        }
    }
    
    printf("\nResults: %d catches, %d misses (%.2f%% detection)\n", 
           catches, misses, 100.0 * catches / (catches + misses));
    printf("Expected theoretical miss rate: ~0.39%% (about 391 misses per 100k)\n");
    
    return 0;
}
