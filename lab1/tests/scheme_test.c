#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../scheme.h"

int main(void) {
    printf("--- Scheme Dispatcher Tests ---\n\n");

    /* Test 1: Parse valid scheme strings */
    printf("Test 1: Parse valid scheme strings\n");
    assert(parse_scheme("checksum") == SCHEME_CHECKSUM16);
    assert(parse_scheme("crc8") == SCHEME_CRC8);
    assert(parse_scheme("crc10") == SCHEME_CRC10);
    assert(parse_scheme("crc16") == SCHEME_CRC16);
    assert(parse_scheme("crc32") == SCHEME_CRC32);
    printf("  [PASS] All valid schemes parsed correctly\n\n");

    /* Test 2: Compute FCS using each scheme */
    printf("Test 2: Compute FCS with each scheme\n");
    uint8_t test_data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    size_t len = 5;

    uint32_t fcs_checksum = compute_fcs(SCHEME_CHECKSUM16, test_data, len);
    uint32_t fcs_crc8 = compute_fcs(SCHEME_CRC8, test_data, len);
    uint32_t fcs_crc10 = compute_fcs(SCHEME_CRC10, test_data, len);
    uint32_t fcs_crc16 = compute_fcs(SCHEME_CRC16, test_data, len);
    uint32_t fcs_crc32 = compute_fcs(SCHEME_CRC32, test_data, len);

    printf("  Computed FCS values:\n");
    printf("    Checksum16: 0x%04x\n", fcs_checksum & 0xFFFF);
    printf("    CRC-8:      0x%02x\n", fcs_crc8 & 0xFF);
    printf("    CRC-10:     0x%03x\n", fcs_crc10 & 0x3FF);
    printf("    CRC-16:     0x%04x\n", fcs_crc16 & 0xFFFF);
    printf("    CRC-32:     0x%08x\n", fcs_crc32);
    printf("  [PASS] All schemes computed FCS\n\n");

    /* Test 3: Verify FCS with each scheme */
    printf("Test 3: Verify FCS with each scheme\n");
    assert(verify_fcs(SCHEME_CHECKSUM16, test_data, len, fcs_checksum) == 1);
    assert(verify_fcs(SCHEME_CRC8, test_data, len, fcs_crc8) == 1);
    assert(verify_fcs(SCHEME_CRC10, test_data, len, fcs_crc10) == 1);
    assert(verify_fcs(SCHEME_CRC16, test_data, len, fcs_crc16) == 1);
    assert(verify_fcs(SCHEME_CRC32, test_data, len, fcs_crc32) == 1);
    printf("  [PASS] All schemes verify correctly\n\n");

    /* Test 4: Scheme isolation - different schemes should detect tampering */
    printf("Test 4: Scheme independence\n");
    uint8_t tampered[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    tampered[0] ^= 0x01;  /* Flip a bit */

    assert(verify_fcs(SCHEME_CHECKSUM16, tampered, len, fcs_checksum) == 0);
    assert(verify_fcs(SCHEME_CRC8, tampered, len, fcs_crc8) == 0);
    assert(verify_fcs(SCHEME_CRC10, tampered, len, fcs_crc10) == 0);
    assert(verify_fcs(SCHEME_CRC16, tampered, len, fcs_crc16) == 0);
    assert(verify_fcs(SCHEME_CRC32, tampered, len, fcs_crc32) == 0);
    printf("  [PASS] All schemes detect tampering independently\n\n");

    /* Test 5: Each scheme detects tampering in different positions */
    printf("Test 5: Tampering detection at various positions\n");
    for (int pos = 0; pos < 5; pos++) {
        uint8_t data_copy[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
        uint32_t fcs = compute_fcs(SCHEME_CRC32, data_copy, len);

        data_copy[pos] ^= 0x01;
        assert(verify_fcs(SCHEME_CRC32, data_copy, len, fcs) == 0);
    }
    printf("  [PASS] Tampering detected at all byte positions\n\n");

    /* Test 6: Large data buffer */
    printf("Test 6: Large data buffer (256 bytes)\n");
    uint8_t large_data[256];
    for (int i = 0; i < 256; i++) {
        large_data[i] = (uint8_t)i;
    }

    uint32_t fcs_large_chk = compute_fcs(SCHEME_CHECKSUM16, large_data, 256);
    uint32_t fcs_large_crc32 = compute_fcs(SCHEME_CRC32, large_data, 256);

    assert(verify_fcs(SCHEME_CHECKSUM16, large_data, 256, fcs_large_chk) == 1);
    assert(verify_fcs(SCHEME_CRC32, large_data, 256, fcs_large_crc32) == 1);

    /* Tamper large data */
    large_data[128] ^= 0x80;
    assert(verify_fcs(SCHEME_CHECKSUM16, large_data, 256, fcs_large_chk) == 0);
    assert(verify_fcs(SCHEME_CRC32, large_data, 256, fcs_large_crc32) == 0);
    printf("  [PASS] Large buffer handled correctly\n\n");

    /* Test 7: Multiple bit flips */
    printf("Test 7: Multiple bit flip detection\n");
    uint8_t data_multi[] = { 0xFF, 0xAA, 0x55, 0x00 };
    uint32_t fcs_multi = compute_fcs(SCHEME_CRC32, data_multi, 4);

    uint8_t tampered_multi[] = { 0xFF, 0xAA, 0x55, 0x00 };
    tampered_multi[0] ^= 0x0F;  /* Flip 4 bits in first byte */
    tampered_multi[2] ^= 0x03;  /* Flip 2 bits in third byte */

    assert(verify_fcs(SCHEME_CRC32, tampered_multi, 4, fcs_multi) == 0);
    printf("  [PASS] Multiple bit flips detected\n\n");

    /* Test 8: Verify that swapping schemes rejects FCS */
    printf("Test 8: Scheme mismatch detection\n");
    uint8_t data_scheme[] = { 0xAB, 0xCD };
    uint32_t fcs_with_crc16 = compute_fcs(SCHEME_CRC16, data_scheme, 2);
    uint32_t fcs_with_chk = compute_fcs(SCHEME_CHECKSUM16, data_scheme, 2);

    /* Using CRC16 FCS with CRC32 should fail */
    assert(verify_fcs(SCHEME_CRC32, data_scheme, 2, fcs_with_crc16) == 0);
    /* Using checksum FCS with CRC8 should fail */
    assert(verify_fcs(SCHEME_CRC8, data_scheme, 2, fcs_with_chk) == 0);
    printf("  [PASS] Mismatched schemes properly rejected\n\n");

    printf("All scheme dispatcher tests passed!\n");
    return 0;
}
