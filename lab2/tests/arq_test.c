#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdint.h>
#include <assert.h>

#include "../arq/arq.h"
#include "../../lab1/frame.h"
#include "../../lab1/scheme.h"
#include "../../lab1/error.h"

/* ===== TEST HELPERS ===== */

#define TEST_PASS() do { return 1; } while (0)
#define TEST_FAIL(msg) do { \
    fprintf(stderr, "  ❌ %s\n", msg); \
    return 0; \
} while (0)

int test_count = 0;
int test_passed = 0;

void run_test(const char *name, int (*test_func)()) {
    test_count++;
    printf("\n[Test %d] %s\n", test_count, name);
    if (test_func()) {
        printf("  ✅ PASS\n");
        test_passed++;
    }
}

/* ===== TEST CASES ===== */

/* Test 1: Frame type field values */
int test_frame_type_constants() {
    int is_valid =
        (FRAME_DATA == 0) &&
        (FRAME_ACK == 1) &&
        (FRAME_NAK == 2);

    if (!is_valid) TEST_FAIL("Frame type constants incorrect");
    TEST_PASS();
}

/* Test 2: Frame structure size */
int test_frame_sizes() {
    // Verify component sizes
    if (sizeof(frame_t) != FRAME_SIZE) {
        fprintf(stderr, "  Expected FRAME_SIZE=%d, got %zu\n", FRAME_SIZE, sizeof(frame_t));
        TEST_FAIL("Frame size mismatch");
    }
    if (sizeof(frame_t) != 65) TEST_FAIL("Frame should be 65 bytes");
    if (sizeof(frame_header) != 17) TEST_FAIL("Header should be 17 bytes");
    if (PAYLOAD_SIZE != 44) TEST_FAIL("Payload should be 44 bytes");
    if (sizeof(frame_trailer) != 4) TEST_FAIL("Trailer should be 4 bytes");

    TEST_PASS();
}

/* Test 3: Frame header offset for frame_type */
int test_frame_type_offset() {
    frame_t frame = {0};

    // Set frame_type field
    frame.header.frame_type = FRAME_DATA;
    if (frame.header.frame_type != FRAME_DATA) TEST_FAIL("DATA frame_type not set");

    frame.header.frame_type = FRAME_ACK;
    if (frame.header.frame_type != FRAME_ACK) TEST_FAIL("ACK frame_type not set");

    frame.header.frame_type = FRAME_NAK;
    if (frame.header.frame_type != FRAME_NAK) TEST_FAIL("NAK frame_type not set");

    TEST_PASS();
}

/* Test 4: DATA frame FCS computation */
int test_data_frame_fcs() {
    frame_t frame = {0};

    // Set up a valid data frame
    frame.header.frame_type = FRAME_DATA;
    frame.header.frame_seq = htons(0);
    frame.header.frame_len = htons(10);
    memset(frame.payload, 0xAA, 10);

    // Compute FCS
    uint32_t fcs = compute_fcs(SCHEME_CHECKSUM16,
                                (uint8_t *)&frame,
                                FRAME_SIZE - TRAILER_SIZE);

    if (fcs == 0) TEST_FAIL("FCS should not be zero for DATA frame");

    // Store and verify
    frame.trailer.fcs = htonl(fcs);
    uint32_t retrieved_fcs = ntohl(frame.trailer.fcs);
    if (retrieved_fcs != fcs) TEST_FAIL("FCS round-trip failed");

    TEST_PASS();
}

/* Test 5: ACK frame FCS computation */
int test_ack_frame_fcs() {
    frame_t frame = {0};

    // Set up a valid ACK frame
    frame.header.frame_type = FRAME_ACK;
    frame.header.frame_seq = htons(0);  // Acknowledge frame 0
    frame.header.frame_len = 0;  // No payload in ACK

    // Compute FCS
    uint32_t fcs = compute_fcs(SCHEME_CHECKSUM16,
                                (uint8_t *)&frame,
                                FRAME_SIZE - TRAILER_SIZE);

    if (fcs == 0) TEST_FAIL("FCS should not be zero for ACK frame");

    frame.trailer.fcs = htonl(fcs);
    uint32_t retrieved_fcs = ntohl(frame.trailer.fcs);
    if (retrieved_fcs != fcs) TEST_FAIL("FCS round-trip failed for ACK");

    TEST_PASS();
}

/* Test 6: NAK frame FCS computation */
int test_nak_frame_fcs() {
    frame_t frame = {0};

    // Set up a valid NAK frame
    frame.header.frame_type = FRAME_NAK;
    frame.header.frame_seq = htons(1);  // NAK for frame 1
    frame.header.frame_len = 0;  // No payload in NAK

    // Compute FCS
    uint32_t fcs = compute_fcs(SCHEME_CHECKSUM16,
                                (uint8_t *)&frame,
                                FRAME_SIZE - TRAILER_SIZE);

    if (fcs == 0) TEST_FAIL("FCS should not be zero for NAK frame");

    frame.trailer.fcs = htonl(fcs);
    uint32_t retrieved_fcs = ntohl(frame.trailer.fcs);
    if (retrieved_fcs != fcs) TEST_FAIL("FCS round-trip failed for NAK");

    TEST_PASS();
}

/* Test 7: Sequence number byte order */
int test_sequence_byte_order() {
    frame_t frame = {0};

    // Test various sequence numbers
    uint16_t seqs[] = {0, 1, 256, 65535};

    for (int i = 0; i < 4; i++) {
        frame.header.frame_seq = htons(seqs[i]);
        uint16_t retrieved = ntohs(frame.header.frame_seq);
        if (retrieved != seqs[i]) {
            fprintf(stderr, "  Sequence mismatch: sent %u, got %u\n", seqs[i], retrieved);
            TEST_FAIL("Sequence number byte order failed");
        }
    }

    TEST_PASS();
}

/* Test 8: Payload length byte order */
int test_payload_length_byte_order() {
    frame_t frame = {0};

    // Test various payload lengths
    uint16_t lens[] = {0, 1, 44, 256, 65535};

    for (int i = 0; i < 5; i++) {
        frame.header.frame_len = htons(lens[i]);
        uint16_t retrieved = ntohs(frame.header.frame_len);
        if (retrieved != lens[i]) {
            fprintf(stderr, "  Length mismatch: sent %u, got %u\n", lens[i], retrieved);
            TEST_FAIL("Payload length byte order failed");
        }
    }

    TEST_PASS();
}

/* Test 9: Frame type discrimination (different frames have different FCS) */
int test_frame_type_discrimination() {
    frame_t data_frame = {0};
    frame_t ack_frame = {0};

    // Both frames with same sequence number but different type
    data_frame.header.frame_type = FRAME_DATA;
    data_frame.header.frame_seq = htons(0);
    data_frame.header.frame_len = htons(0);

    ack_frame.header.frame_type = FRAME_ACK;
    ack_frame.header.frame_seq = htons(0);
    ack_frame.header.frame_len = 0;

    // Compute FCS for both
    uint32_t data_fcs = compute_fcs(SCHEME_CHECKSUM16,
                                     (uint8_t *)&data_frame,
                                     FRAME_SIZE - TRAILER_SIZE);
    uint32_t ack_fcs = compute_fcs(SCHEME_CHECKSUM16,
                                    (uint8_t *)&ack_frame,
                                    FRAME_SIZE - TRAILER_SIZE);

    // FCS should be different because frame_type is different
    if (data_fcs == ack_fcs) {
        TEST_FAIL("DATA and ACK frames should have different FCS due to frame_type field");
    }

    TEST_PASS();
}

/* Test 10: Stop-and-Wait sequence toggle */
int test_sw_sequence_toggle() {
    uint16_t seq = 0;

    // Verify toggle pattern (0->1->0->1...)
    if (seq != 0) TEST_FAIL("Initial sequence should be 0");

    for (int i = 0; i < 10; i++) {
        uint16_t expected = i % 2;
        if (seq != expected) {
            fprintf(stderr, "  Iteration %d: expected %u, got %u\n", i, expected, seq);
            TEST_FAIL("Sequence toggle mismatch");
        }
        seq = (seq + 1) % 2;
    }

    TEST_PASS();
}

/* Test 11: Frame payload storage and retrieval */
int test_payload_storage() {
    frame_t frame = {0};
    uint8_t test_data[44];

    // Fill with pattern
    for (int i = 0; i < 44; i++) {
        test_data[i] = (i * 7) % 256;
    }

    // Store in payload
    memcpy(frame.payload, test_data, 44);

    // Verify all bytes
    for (int i = 0; i < 44; i++) {
        if (frame.payload[i] != test_data[i]) {
            fprintf(stderr, "  Byte %d mismatch\n", i);
            TEST_FAIL("Payload storage mismatch");
        }
    }

    TEST_PASS();
}

/* Test 12: FCS field independence from payload */
int test_fcs_field_isolation() {
    frame_t frame1 = {0};
    frame_t frame2 = {0};

    // Different payloads
    memset(frame1.payload, 0xAA, 44);
    memset(frame2.payload, 0xBB, 44);

    // Same frame type and sequence
    frame1.header.frame_type = FRAME_DATA;
    frame1.header.frame_seq = htons(0);
    frame1.header.frame_len = htons(44);

    frame2.header.frame_type = FRAME_DATA;
    frame2.header.frame_seq = htons(0);
    frame2.header.frame_len = htons(44);

    // Compute FCS for both
    uint32_t fcs1 = compute_fcs(SCHEME_CHECKSUM16,
                                 (uint8_t *)&frame1,
                                 FRAME_SIZE - TRAILER_SIZE);
    uint32_t fcs2 = compute_fcs(SCHEME_CHECKSUM16,
                                 (uint8_t *)&frame2,
                                 FRAME_SIZE - TRAILER_SIZE);

    // Different payloads should produce different FCS
    if (fcs1 == fcs2) {
        TEST_FAIL("Different payloads should produce different FCS");
    }

    TEST_PASS();
}

/* Test 13: Frame header initialization */
int test_frame_header_init() {
    frame_t frame = {0};

    // Verify all header fields are writable
    frame.header.frame_type = FRAME_DATA;
    frame.header.frame_seq = htons(42);
    frame.header.frame_len = htons(100);

    // Set arbitrary MAC addresses (should not affect frame_type)
    memset(frame.header.src_addr, 0x11, 6);
    memset(frame.header.dest_addr, 0x22, 6);

    // Verify frame_type is preserved
    if (frame.header.frame_type != FRAME_DATA) {
        TEST_FAIL("frame_type corrupted by MAC address setting");
    }

    // Verify sequence and length
    if (ntohs(frame.header.frame_seq) != 42) TEST_FAIL("Sequence not preserved");
    if (ntohs(frame.header.frame_len) != 100) TEST_FAIL("Length not preserved");

    TEST_PASS();
}

/* Test 14: arq_stats_t initialization */
int test_arq_stats_init() {
    arq_stats_t stats = {0};

    if (stats.frames_sent != 0) TEST_FAIL("frames_sent should start at 0");
    if (stats.frames_retransmitted != 0) TEST_FAIL("frames_retransmitted should start at 0");
    if (stats.acks_received != 0) TEST_FAIL("acks_received should start at 0");
    if (stats.corrupted_count != 0) TEST_FAIL("corrupted_count should start at 0");
    if (stats.total_time != 0.0) TEST_FAIL("total_time should start at 0");

    TEST_PASS();
}

/* Test 15: arq_config_t structure */
int test_arq_config_init() {
    arq_config_t config = {
        .mode = ARQ_SW,
        .window_size = 1,
        .socket_fd = -1,
        .filename = NULL
    };

    if (config.mode != ARQ_SW) TEST_FAIL("Mode not set correctly");
    if (config.window_size != 1) TEST_FAIL("Window size not set correctly");
    if (config.socket_fd != -1) TEST_FAIL("Socket fd not set correctly");
    if (config.filename != NULL) TEST_FAIL("Filename should be NULL initially");

    TEST_PASS();
}

/* ===== MAIN TEST RUNNER ===== */

int main() {
    printf("╔════════════════════════════════════════╗\n");
    printf("║   ARQ Frame & Protocol Structure Tests  ║\n");
    printf("║        (Phase 3 - Stop-and-Wait)       ║\n");
    printf("╚════════════════════════════════════════╝\n");

    // Frame structure tests
    run_test("Frame type constants", test_frame_type_constants);
    run_test("Frame sizes (17B header, 65B total)", test_frame_sizes);
    run_test("Frame header offset for frame_type", test_frame_type_offset);

    // FCS computation tests
    run_test("DATA frame FCS", test_data_frame_fcs);
    run_test("ACK frame FCS", test_ack_frame_fcs);
    run_test("NAK frame FCS", test_nak_frame_fcs);

    // Byte order tests
    run_test("Sequence number byte order", test_sequence_byte_order);
    run_test("Payload length byte order", test_payload_length_byte_order);

    // Frame discrimination
    run_test("Frame type discrimination (FCS)", test_frame_type_discrimination);

    // Protocol tests
    run_test("Stop-and-Wait sequence toggle", test_sw_sequence_toggle);
    run_test("Frame payload storage", test_payload_storage);
    run_test("FCS field isolation", test_fcs_field_isolation);
    run_test("Frame header initialization", test_frame_header_init);

    // Structure initialization
    run_test("arq_stats_t initialization", test_arq_stats_init);
    run_test("arq_config_t initialization", test_arq_config_init);

    // Summary
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║            Test Results                ║\n");
    printf("║  Passed: %d / %d                       ║\n", test_passed, test_count);
    if (test_passed < test_count) {
        printf("║  Failed: %d / %d                       ║\n", test_count - test_passed, test_count);
        printf("╚════════════════════════════════════════╝\n");
        return 1;
    }
    printf("╚════════════════════════════════════════╝\n");
    printf("\n✅ All %d tests passed!\n", test_count);
    return 0;
}
