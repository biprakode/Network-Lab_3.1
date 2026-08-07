#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../error.h"

int main(void) {
    printf("--- 16-bit Internet Checksum Tests ---\n\n");

    /* Test 1: Empty buffer (len=0) */
    printf("Test 1: Empty buffer\n");
    uint8_t empty[] = {};
    uint16_t chk_empty = checksum16_compute(empty, 0);
    assert(chk_empty == 0xFFFF);  /* Complement of 0 is all ones */
    int verify_empty = checksum16_verify(empty, 0, chk_empty);
    assert(verify_empty == 1);
    printf("  [PASS] Empty buffer handled correctly (checksum = 0xFFFF)\n\n");

    /* Test 2: Single byte */
    printf("Test 2: Single byte\n");
    uint8_t single[] = { 0xFF };
    uint16_t chk_single = checksum16_compute(single, 1);
    int verify_single = checksum16_verify(single, 1, chk_single);
    assert(verify_single == 1);
    printf("  [PASS] Single byte checksum works\n\n");

    /* Test 3: Two bytes (complete word) */
    printf("Test 3: Two bytes (word-aligned)\n");
    uint8_t two_bytes[] = { 0x12, 0x34 };
    uint16_t chk_two = checksum16_compute(two_bytes, 2);
    int verify_two = checksum16_verify(two_bytes, 2, chk_two);
    assert(verify_two == 1);
    printf("  [PASS] Two-byte checksum works\n\n");

    /* Test 4: Odd-length buffer (3 bytes) */
    printf("Test 4: Odd-length buffer (3 bytes)\n");
    uint8_t odd[] = { 0xAA, 0xBB, 0xCC };
    uint16_t chk_odd = checksum16_compute(odd, 3);
    int verify_odd = checksum16_verify(odd, 3, chk_odd);
    assert(verify_odd == 1);
    printf("  [PASS] Odd-length buffer handled\n\n");

    /* Test 5: All zeros */
    printf("Test 5: All zeros pattern\n");
    uint8_t zeros[8] = { 0 };
    uint16_t chk_zeros = checksum16_compute(zeros, 8);
    int verify_zeros = checksum16_verify(zeros, 8, chk_zeros);
    assert(verify_zeros == 1);
    printf("  [PASS] All-zeros pattern handled\n\n");

    /* Test 6: All ones */
    printf("Test 6: All ones pattern\n");
    uint8_t ones[8];
    memset(ones, 0xFF, 8);
    uint16_t chk_ones = checksum16_compute(ones, 8);
    int verify_ones = checksum16_verify(ones, 8, chk_ones);
    assert(verify_ones == 1);
    printf("  [PASS] All-ones pattern handled\n\n");

    /* Test 7: Known test vector (RFC 1071) */
    printf("Test 7: Known test vector\n");
    uint8_t test_vec[] = { 0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
                           0x40, 0x06, 0x00, 0x00, 0xac, 0x10, 0x0a, 0x63,
                           0xac, 0x10, 0x0a, 0x0c };
    uint16_t chk_vec = checksum16_compute(test_vec, sizeof(test_vec));
    int verify_vec = checksum16_verify(test_vec, sizeof(test_vec), chk_vec);
    assert(verify_vec == 1);
    printf("  [PASS] Known test vector passed\n\n");

    /* Test 8: Bit-flip detection */
    printf("Test 8: Bit-flip detection (multi-bit flips)\n");
    uint8_t data[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    uint16_t chk = checksum16_compute(data, sizeof(data));

    for (int bit = 0; bit < 16; bit++) {
        uint8_t tampered[sizeof(data)];
        memcpy(tampered, data, sizeof(data));
        tampered[bit / 8] ^= (1 << (bit % 8));
        int should_fail = checksum16_verify(tampered, sizeof(tampered), chk);
        assert(should_fail == 0);
    }
    printf("  [PASS] All single-bit flips detected\n\n");

    /* Test 9: Two-bit flip in same word */
    printf("Test 9: Two-bit flip detection (same word)\n");
    uint8_t data2[] = { 0xFF, 0xFF, 0x00, 0x00 };
    uint16_t chk2 = checksum16_compute(data2, sizeof(data2));
    uint8_t tampered2[sizeof(data2)];
    memcpy(tampered2, data2, sizeof(data2));
    tampered2[0] ^= 0x03;  // Flip bits 0 and 1
    int should_fail2 = checksum16_verify(tampered2, sizeof(tampered2), chk2);
    assert(should_fail2 == 0);
    printf("  [PASS] Two-bit flip in same word detected\n\n");

    /* Test 10: Two-bit flip in different words */
    printf("Test 10: Two-bit flip detection (different words)\n");
    uint8_t data3[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    uint16_t chk3 = checksum16_compute(data3, sizeof(data3));
    uint8_t tampered3[sizeof(data3)];
    memcpy(tampered3, data3, sizeof(data3));
    tampered3[0] ^= 0x01;  // Flip bit in first word
    tampered3[2] ^= 0x01;  // Flip bit in second word
    int should_fail3 = checksum16_verify(tampered3, sizeof(tampered3), chk3);
    assert(should_fail3 == 0);
    printf("  [PASS] Two-bit flip in different words detected\n\n");

    printf("All checksum tests passed!\n");
    return 0;
}
