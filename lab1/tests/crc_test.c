#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../error.h"
#include "../scheme.h"

int main(void) {
    printf("--- CRC Implementation Tests (8, 10, 16, 32) ---\n\n");

    /* Helper function to test a specific CRC degree */
    struct crc_test_case {
        const char *name;
        crc_gen params;
        uint8_t data[32];
        size_t len;
    };

    crc_gen crc8_params = { .poly = 0xD5, .degree = 8 };
    crc_gen crc10_params = { .poly = 0x233, .degree = 10 };
    crc_gen crc16_params = { .poly = 0x8005, .degree = 16 };
    crc_gen crc32_params = { .poly = 0x04C11DB7, .degree = 32 };

    /* Test 1: Empty data */
    printf("Test 1: Empty data for all CRC variants\n");
    uint8_t empty[] = {};
    uint32_t crc8_empty = crc_compute(empty, 0, crc8_params);
    uint32_t crc10_empty = crc_compute(empty, 0, crc10_params);
    uint32_t crc16_empty = crc_compute(empty, 0, crc16_params);
    uint32_t crc32_empty = crc_compute(empty, 0, crc32_params);
    assert(crc_verify(empty, 0, crc8_params, crc8_empty) == 1);
    assert(crc_verify(empty, 0, crc10_params, crc10_empty) == 1);
    assert(crc_verify(empty, 0, crc16_params, crc16_empty) == 1);
    assert(crc_verify(empty, 0, crc32_params, crc32_empty) == 1);
    printf("  [PASS] All CRCs handle empty data\n\n");

    /* Test 2: Single byte for each CRC */
    printf("Test 2: Single byte for each CRC variant\n");
    uint8_t single[] = { 0x42 };
    uint32_t crc8_single = crc_compute(single, 1, crc8_params);
    uint32_t crc10_single = crc_compute(single, 1, crc10_params);
    uint32_t crc16_single = crc_compute(single, 1, crc16_params);
    uint32_t crc32_single = crc_compute(single, 1, crc32_params);
    assert(crc_verify(single, 1, crc8_params, crc8_single) == 1);
    assert(crc_verify(single, 1, crc10_params, crc10_single) == 1);
    assert(crc_verify(single, 1, crc16_params, crc16_single) == 1);
    assert(crc_verify(single, 1, crc32_params, crc32_single) == 1);
    printf("  [PASS] All CRCs handle single byte\n\n");

    /* Test 3: "123456789" test string (standard CRC test) */
    printf("Test 3: Standard test string '123456789'\n");
    uint8_t std_test[] = "123456789";
    size_t std_len = 9;
    uint32_t crc8_std = crc_compute(std_test, std_len, crc8_params);
    uint32_t crc10_std = crc_compute(std_test, std_len, crc10_params);
    uint32_t crc16_std = crc_compute(std_test, std_len, crc16_params);
    uint32_t crc32_std = crc_compute(std_test, std_len, crc32_params);
    assert(crc_verify(std_test, std_len, crc8_params, crc8_std) == 1);
    assert(crc_verify(std_test, std_len, crc10_params, crc10_std) == 1);
    assert(crc_verify(std_test, std_len, crc16_params, crc16_std) == 1);
    assert(crc_verify(std_test, std_len, crc32_params, crc32_std) == 1);
    printf("  [PASS] Standard test string verified for all CRCs\n");
    printf("    CRC-8:  0x%02x\n", crc8_std & 0xFF);
    printf("    CRC-10: 0x%03x\n", crc10_std & 0x3FF);
    printf("    CRC-16: 0x%04x\n", crc16_std & 0xFFFF);
    printf("    CRC-32: 0x%08x\n\n", crc32_std);

    /* Test 4: All zeros */
    printf("Test 4: All zeros pattern\n");
    uint8_t zeros[16] = { 0 };
    uint32_t crc8_zeros = crc_compute(zeros, 16, crc8_params);
    uint32_t crc16_zeros = crc_compute(zeros, 16, crc16_params);
    uint32_t crc32_zeros = crc_compute(zeros, 16, crc32_params);
    assert(crc_verify(zeros, 16, crc8_params, crc8_zeros) == 1);
    assert(crc_verify(zeros, 16, crc16_params, crc16_zeros) == 1);
    assert(crc_verify(zeros, 16, crc32_params, crc32_zeros) == 1);
    printf("  [PASS] All-zeros pattern verified\n\n");

    /* Test 5: All ones */
    printf("Test 5: All ones pattern\n");
    uint8_t ones[16];
    memset(ones, 0xFF, 16);
    uint32_t crc8_ones = crc_compute(ones, 16, crc8_params);
    uint32_t crc16_ones = crc_compute(ones, 16, crc16_params);
    uint32_t crc32_ones = crc_compute(ones, 16, crc32_params);
    assert(crc_verify(ones, 16, crc8_params, crc8_ones) == 1);
    assert(crc_verify(ones, 16, crc16_params, crc16_ones) == 1);
    assert(crc_verify(ones, 16, crc32_params, crc32_ones) == 1);
    printf("  [PASS] All-ones pattern verified\n\n");

    /* Test 6: Alternating bits */
    printf("Test 6: Alternating bit patterns\n");
    uint8_t alt_aa[] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
    uint8_t alt_55[] = { 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };

    uint32_t crc32_aa = crc_compute(alt_aa, 8, crc32_params);
    uint32_t crc32_55 = crc_compute(alt_55, 8, crc32_params);

    assert(crc_verify(alt_aa, 8, crc32_params, crc32_aa) == 1);
    assert(crc_verify(alt_55, 8, crc32_params, crc32_55) == 1);
    printf("  [PASS] Alternating bit patterns verified\n\n");

    /* Test 7: Single bit flip detection for each CRC */
    printf("Test 7: Single-bit flip detection\n");
    uint8_t data[] = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };  // "Hello"
    uint32_t crc8 = crc_compute(data, 5, crc8_params);
    uint32_t crc16 = crc_compute(data, 5, crc16_params);
    uint32_t crc32 = crc_compute(data, 5, crc32_params);

    for (int bit = 0; bit < 40; bit++) {
        uint8_t tampered[5];
        memcpy(tampered, data, 5);
        tampered[bit / 8] ^= (1 << (bit % 8));

        if (bit < 40) {  /* Test first 40 bits exhaustively */
            assert(crc_verify(tampered, 5, crc8_params, crc8) == 0);
            assert(crc_verify(tampered, 5, crc16_params, crc16) == 0);
            assert(crc_verify(tampered, 5, crc32_params, crc32) == 0);
        }
    }
    printf("  [PASS] All single-bit flips detected in CRC-8, CRC-16, CRC-32\n\n");

    /* Test 8: Verify different CRCs produce different results */
    printf("Test 8: Different CRC variants produce different results\n");
    uint8_t test_data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint32_t crc8_v = crc_compute(test_data, 4, crc8_params);
    uint32_t crc10_v = crc_compute(test_data, 4, crc10_params);
    uint32_t crc16_v = crc_compute(test_data, 4, crc16_params);
    uint32_t crc32_v = crc_compute(test_data, 4, crc32_params);

    assert(crc8_v != crc10_v);
    assert(crc8_v != crc16_v);
    assert(crc8_v != crc32_v);
    assert(crc10_v != crc16_v);
    assert(crc10_v != crc32_v);
    assert(crc16_v != crc32_v);
    printf("  [PASS] All CRC variants produce unique results\n\n");

    printf("All CRC tests passed!\n");
    return 0;
}
