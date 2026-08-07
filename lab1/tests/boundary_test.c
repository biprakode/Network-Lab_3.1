#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../error.h"
#include "../scheme.h"

int main(void) {
    printf("--- Boundary and Edge Case Tests ---\n\n");

    /* Test 1: Minimum sizes */
    printf("Test 1: Minimum data sizes\n");

    uint8_t one_byte[] = { 0x42 };
    uint16_t chk1 = checksum16_compute(one_byte, 1);
    assert(checksum16_verify(one_byte, 1, chk1) == 1);

    uint32_t crc1_8 = crc_compute(one_byte, 1, (crc_gen){.poly = 0xD5, .degree = 8});
    assert(crc_verify(one_byte, 1, (crc_gen){.poly = 0xD5, .degree = 8}, crc1_8) == 1);

    uint32_t crc1_16 = crc_compute(one_byte, 1, (crc_gen){.poly = 0x8005, .degree = 16});
    assert(crc_verify(one_byte, 1, (crc_gen){.poly = 0x8005, .degree = 16}, crc1_16) == 1);

    printf("  [PASS] Single byte handled for checksum and CRCs\n\n");

    /* Test 2: Maximum uint8 values */
    printf("Test 2: Maximum byte values\n");
    uint8_t max_bytes[] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint16_t chk_max = checksum16_compute(max_bytes, 4);
    assert(checksum16_verify(max_bytes, 4, chk_max) == 1);

    uint32_t crc_max = crc_compute(max_bytes, 4, (crc_gen){.poly = 0x8005, .degree = 16});
    assert(crc_verify(max_bytes, 4, (crc_gen){.poly = 0x8005, .degree = 16}, crc_max) == 1);
    printf("  [PASS] Maximum byte values handled\n\n");

    /* Test 3: Minimum uint8 values (zeros) */
    printf("Test 3: Minimum byte values (all zeros)\n");
    uint8_t min_bytes[] = { 0x00, 0x00, 0x00, 0x00 };
    uint16_t chk_min = checksum16_compute(min_bytes, 4);
    assert(checksum16_verify(min_bytes, 4, chk_min) == 1);

    uint32_t crc_min = crc_compute(min_bytes, 4, (crc_gen){.poly = 0x8005, .degree = 16});
    assert(crc_verify(min_bytes, 4, (crc_gen){.poly = 0x8005, .degree = 16}, crc_min) == 1);
    printf("  [PASS] Minimum byte values handled\n\n");

    /* Test 4: Odd vs Even length buffers */
    printf("Test 4: Odd-length buffers\n");
    uint8_t odd3[] = { 0x11, 0x22, 0x33 };
    uint8_t odd5[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
    uint8_t odd7[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };

    uint16_t chk_odd3 = checksum16_compute(odd3, 3);
    uint16_t chk_odd5 = checksum16_compute(odd5, 5);
    uint16_t chk_odd7 = checksum16_compute(odd7, 7);

    assert(checksum16_verify(odd3, 3, chk_odd3) == 1);
    assert(checksum16_verify(odd5, 5, chk_odd5) == 1);
    assert(checksum16_verify(odd7, 7, chk_odd7) == 1);
    printf("  [PASS] Odd-length buffers (3, 5, 7 bytes) handled\n\n");

    /* Test 5: Even-length buffers */
    printf("Test 5: Even-length buffers\n");
    uint8_t even2[] = { 0x11, 0x22 };
    uint8_t even4[] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t even8[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };

    uint16_t chk_even2 = checksum16_compute(even2, 2);
    uint16_t chk_even4 = checksum16_compute(even4, 4);
    uint16_t chk_even8 = checksum16_compute(even8, 8);

    assert(checksum16_verify(even2, 2, chk_even2) == 1);
    assert(checksum16_verify(even4, 4, chk_even4) == 1);
    assert(checksum16_verify(even8, 8, chk_even8) == 1);
    printf("  [PASS] Even-length buffers (2, 4, 8 bytes) handled\n\n");

    /* Test 6: Sequential byte patterns */
    printf("Test 6: Sequential byte patterns\n");
    uint8_t sequential[16];
    for (int i = 0; i < 16; i++) {
        sequential[i] = i & 0xFF;
    }

    uint16_t chk_seq = checksum16_compute(sequential, 16);
    assert(checksum16_verify(sequential, 16, chk_seq) == 1);

    uint32_t crc_seq = crc_compute(sequential, 16, (crc_gen){.poly = 0x8005, .degree = 16});
    assert(crc_verify(sequential, 16, (crc_gen){.poly = 0x8005, .degree = 16}, crc_seq) == 1);
    printf("  [PASS] Sequential byte patterns handled\n\n");

    /* Test 7: Repeating byte patterns */
    printf("Test 7: Repeating byte patterns\n");
    uint8_t repeat_aa[32];
    memset(repeat_aa, 0xAA, 32);
    uint8_t repeat_55[32];
    memset(repeat_55, 0x55, 32);

    uint16_t chk_aa = checksum16_compute(repeat_aa, 32);
    uint16_t chk_55 = checksum16_compute(repeat_55, 32);
    assert(checksum16_verify(repeat_aa, 32, chk_aa) == 1);
    assert(checksum16_verify(repeat_55, 32, chk_55) == 1);
    printf("  [PASS] Repeating patterns (0xAA, 0x55) handled\n\n");

    /* Test 8: CRC with different polynomial degrees */
    printf("Test 8: CRC with different degree polynomials\n");
    uint8_t test[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE };

    crc_gen crc8 = { .poly = 0xD5, .degree = 8 };
    crc_gen crc10 = { .poly = 0x233, .degree = 10 };
    crc_gen crc16 = { .poly = 0x8005, .degree = 16 };
    crc_gen crc32 = { .poly = 0x04C11DB7, .degree = 32 };

    uint32_t c8 = crc_compute(test, 6, crc8);
    uint32_t c10 = crc_compute(test, 6, crc10);
    uint32_t c16 = crc_compute(test, 6, crc16);
    uint32_t c32 = crc_compute(test, 6, crc32);

    assert(crc_verify(test, 6, crc8, c8) == 1);
    assert(crc_verify(test, 6, crc10, c10) == 1);
    assert(crc_verify(test, 6, crc16, c16) == 1);
    assert(crc_verify(test, 6, crc32, c32) == 1);
    printf("  [PASS] All CRC degree polynomials work\n\n");

    /* Test 9: Verify results for all schemes with same data */
    printf("Test 9: All schemes on identical data\n");
    uint8_t common_data[] = { 0x45, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00 };

    uint32_t fcs_chk = compute_fcs(SCHEME_CHECKSUM16, common_data, 8);
    uint32_t fcs_c8 = compute_fcs(SCHEME_CRC8, common_data, 8);
    uint32_t fcs_c10 = compute_fcs(SCHEME_CRC10, common_data, 8);
    uint32_t fcs_c16 = compute_fcs(SCHEME_CRC16, common_data, 8);
    uint32_t fcs_c32 = compute_fcs(SCHEME_CRC32, common_data, 8);

    assert(verify_fcs(SCHEME_CHECKSUM16, common_data, 8, fcs_chk) == 1);
    assert(verify_fcs(SCHEME_CRC8, common_data, 8, fcs_c8) == 1);
    assert(verify_fcs(SCHEME_CRC10, common_data, 8, fcs_c10) == 1);
    assert(verify_fcs(SCHEME_CRC16, common_data, 8, fcs_c16) == 1);
    assert(verify_fcs(SCHEME_CRC32, common_data, 8, fcs_c32) == 1);
    printf("  [PASS] All schemes compute and verify on same data\n\n");

    /* Test 10: Hamming distance of 1 (single-bit flip) */
    printf("Test 10: Single-bit flip Hamming distance (exhaustive)\n");
    uint8_t hamming_data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    uint32_t fcs_ham = compute_fcs(SCHEME_CRC32, hamming_data, 4);

    int detections = 0;
    for (int byte = 0; byte < 4; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t tampered[4];
            memcpy(tampered, hamming_data, 4);
            tampered[byte] ^= (1 << bit);

            if (verify_fcs(SCHEME_CRC32, tampered, 4, fcs_ham) == 0) {
                detections++;
            }
        }
    }
    assert(detections == 32);  /* All 32 single-bit flips should be detected */
    printf("  [PASS] All 32 single-bit flips detected\n\n");

    printf("All boundary and edge case tests passed!\n");
    return 0;
}
