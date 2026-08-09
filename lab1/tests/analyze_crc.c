#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// Simplified CRC-8 analysis
// Polynomial 0xD5 = 0b11010101 (degree 8)

int main() {
    // Theory: For CRC with generator polynomial P(x) of degree r,
    // an error burst of length r+1 is undetected if and only if 
    // the error pattern E(x) is divisible by P(x)
    
    // CRC-8: P(x) = x^8 + x^6 + x^4 + x^2 + x + 1 (0xD5)
    // Theoretical undetectable error bursts of length 9:
    // - Must be of form: E(x) = P(x) * Q(x) for some polynomial Q(x)
    // - Since P(x) has degree 8, and E(x) has length 9 (degree 8),
    //   Q(x) must have degree 0 (constant = 1)
    // - So only E(x) = P(x) itself is undetectable!
    
    // But P(x) = 0xD5 = 0b11010101 occupies 9 bits (with leading 1)
    // In a message of 480 bits, this specific pattern at ANY position
    // would be undetectable
    
    // The probability of hitting exactly 0b110101010 (9 bits) at random
    // position is roughly 1/512 per position, or about 480/512 ≈ 0.94
    // positions where it could occur, each with 1/512 probability
    
    printf("CRC-8 Analysis (Polynomial 0xD5 = 0x11010101)\n");
    printf("Buffer size: 480 bits\n");
    printf("Burst length: 9 bits (r+1)\n\n");
    
    printf("Theory: Only the generator polynomial itself (0xD5 = 0b110101010)\n");
    printf("can form an undetectable 9-bit burst error.\n\n");
    
    printf("Probability of hit per 9-bit position: 1/512\n");
    printf("Number of possible 9-bit positions in 480 bits: 472 (480-9+1)\n");
    printf("Expected hits per 100k trials: 472/512 * 100k ≈ 92 hits\n");
    printf("But we got 0 hits in 100k trials.\n\n");
    
    printf("Hypothesis: Random bursts at random positions don't align with\n");
    printf("the 0b110101010 pattern. Need to test at ALL positions systematically.\n");
    
    return 0;
}
