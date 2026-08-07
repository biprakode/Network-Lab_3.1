#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../scheme.h"
#include "../error.h"

int main(void) {
    printf("--- Starting Comprehensive Error Detection Tests ---\n\n");

    /* ==========================================
     * 1. Test 16-bit Internet Checksum
     * ========================================== */
    printf("Testing 16-bit Checksum (via scheme dispatcher)...\n");
    
    uint8_t msg_chk[] = { 'H', 'e', 'l', 'l', 'o', 0x00 };
    size_t len_chk = sizeof(msg_chk);

    scheme_t s_chk = parse_scheme("checksum");
    assert(s_chk == SCHEME_CHECKSUM16);

    uint32_t computed_chk = compute_fcs(s_chk, msg_chk, len_chk);
    
    // Confirm untouched codeword verifies successfully
    int verify_ok = verify_fcs(s_chk, msg_chk, len_chk, computed_chk);
    assert(verify_ok == 1);
    printf("  [PASS] Untouched checksum verification succeeded.\n");

    // Confirm single-bit flip causes verification to fail
    uint8_t tampered_chk[sizeof(msg_chk)];
    memcpy(tampered_chk, msg_chk, len_chk);
    tampered_chk[0] ^= 0x01; // Flip a single bit

    int verify_bad = verify_fcs(s_chk, tampered_chk, len_chk, computed_chk);
    assert(verify_bad == 0);
    printf("  [PASS] Single-bit flip correctly rejected by checksum verify.\n\n");

    /* ==========================================
     * 2. Test CRC Schemes (Degrees: 8, 10, 16, 32)
     * ========================================== */
    printf("Testing CRC Schemes (8, 10, 16, 32)...\n");

    struct {
        const char *name;
        scheme_t scheme;
    } crc_tests[] = {
        { "crc8", SCHEME_CRC8 },
        { "crc10", SCHEME_CRC10 },
        { "crc16", SCHEME_CRC16 },
        { "crc32", SCHEME_CRC32 }
    };

    // Test buffer containing sample bytes
    uint8_t crc_data[] = { 0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD, 0xEF, 0x01 };
    size_t crc_len = sizeof(crc_data);

    for (size_t i = 0; i < sizeof(crc_tests) / sizeof(crc_tests[0]); i++) {
        scheme_t scheme = crc_tests[i].scheme;
        printf("  Testing %s:\n", crc_tests[i].name);

        scheme_t parsed = parse_scheme(crc_tests[i].name);
        assert(parsed == scheme);

        uint32_t computed_crc = compute_fcs(scheme, crc_data, crc_len);

        // Confirm untouched data verifies successfully
        int check_val = verify_fcs(scheme, crc_data, crc_len, computed_crc);
        assert(check_val == 1);
        printf("    - [PASS] Untouched %s verified successfully.\n", crc_tests[i].name);

        // Confirm single-bit flip causes verification failure
        uint8_t tampered_data[sizeof(crc_data)];
        memcpy(tampered_data, crc_data, crc_len);
        tampered_data[0] ^= 0x01; // Flip a bit in the data stream

        int check_bad = verify_fcs(scheme, tampered_data, crc_len, computed_crc);
        assert(check_bad == 0);
        printf("    - [PASS] Single-bit flip correctly rejected for %s.\n", crc_tests[i].name);
    }

    printf("\nAll test vectors and dispatcher checks passed successfully!\n");
    return 0;
}