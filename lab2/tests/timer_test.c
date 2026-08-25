#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "../timer.h"

// Helper macro: sleep_ms(milliseconds)
#define sleep_ms(ms) do { \
    struct timespec req = {0}; \
    req.tv_nsec = (long)(ms * 1e6); \
    nanosleep(&req, NULL); \
} while(0)

typedef struct {
    int passed;
    int failed;
} test_result;

test_result results = {0, 0};

void assert_equals(long long actual, long long expected, const char *msg) {
    if (actual == expected) {
        results.passed++;
    } else {
        results.failed++;
        printf("  ❌ FAIL: %s (expected %lld, got %lld)\n", msg, expected, actual);
    }
}

void assert_approx(double actual, double expected, double tolerance, const char *msg) {
    double diff = fabs(actual - expected);
    if (diff <= tolerance) {
        results.passed++;
    } else {
        results.failed++;
        printf("  ❌ FAIL: %s (expected ~%.4f, got %.4f, diff %.4f)\n",
               msg, expected, actual, diff);
    }
}

void assert_true(int condition, const char *msg) {
    if (condition) {
        results.passed++;
    } else {
        results.failed++;
        printf("  ❌ FAIL: %s\n", msg);
    }
}

void print_test_header(const char *test_name) {
    printf("\n%s\n", test_name);
}

void print_test_footer(void) {
    if (results.failed == 0) {
        printf("  ✅ PASS\n");
    } else {
        printf("  ⚠️  %d failed, %d passed\n", results.failed, results.passed);
    }
}

void test_timer_init(void) {
    print_test_header("Test 1: Timer Initialization");

    timer_config config = {50.0, 0};  // 50ms RTO, no EMA
    timer_init(config);

    timer_stats stats = timer_get_stats();
    assert_equals(stats.windows_started, 0, "windows_started = 0");
    assert_equals(stats.windows_expired, 0, "windows_expired = 0");
    assert_equals(stats.frames_started, 0, "frames_started = 0");
    assert_equals(stats.frames_expired, 0, "frames_expired = 0");

    double rto = timer_get_rto_ms();
    assert_approx(rto, 50.0, 0.1, "RTO matches config (fixed mode)");

    print_test_footer();
}

void test_window_timer_start_stop(void) {
    print_test_header("Test 2: Window Timer Start/Stop");

    timer_config config = {50.0, 0};
    timer_init(config);
    timer_reset_stats();

    timer_start_window();
    timer_stats stats = timer_get_stats();
    assert_equals(stats.windows_started, 1, "windows_started incremented");

    int expired = timer_window_expired();
    assert_equals(expired, 0, "timer not expired immediately");

    timer_stop_window();
    expired = timer_window_expired();
    assert_equals(expired, 0, "timer returns 0 after stop");

    print_test_footer();
}

void test_window_timer_idempotent(void) {
    print_test_header("Test 3: Window Timer Idempotent (Multiple Starts)");

    timer_config config = {50.0, 0};
    timer_init(config);
    timer_reset_stats();

    timer_start_window();

    sleep_ms(5);

    timer_start_window();  // Call again - should NOT restart

    timer_stats stats = timer_get_stats();
    assert_equals(stats.windows_started, 1, "windows_started = 1 (not 2)");

    print_test_footer();
}

void test_window_timer_expiry(void) {
    print_test_header("Test 4: Window Timer Expiry Detection");

    timer_config config = {20.0, 0};  // 20ms RTO
    timer_init(config);
    timer_reset_stats();

    timer_start_window();

    // Check before timeout
    sleep_ms(5);  // 5ms
    int expired = timer_window_expired();
    assert_equals(expired, 0, "not expired at 5ms");

    // Wait for timeout
    sleep_ms(20);  // 20ms more (total 25ms > 20ms RTO)
    expired = timer_window_expired();
    assert_equals(expired, 1, "expired after RTO elapses");

    timer_stats stats = timer_get_stats();
    assert_equals(stats.windows_expired, 1, "windows_expired incremented");

    print_test_footer();
}

void test_frame_timer_lifecycle(void) {
    print_test_header("Test 5: Frame Timer Lifecycle (Start/Stop)");

    timer_config config = {50.0, 0};
    timer_init(config);
    timer_reset_stats();

    uint16_t seq = 100;

    timer_start_frame(seq);
    timer_stats stats = timer_get_stats();
    assert_equals(stats.frames_started, 1, "frames_started incremented");

    int expired = timer_frame_expired(seq);
    assert_equals(expired, 0, "frame timer not expired immediately");

    timer_stop_frame(seq);
    expired = timer_frame_expired(seq);
    assert_equals(expired, 0, "frame timer returns 0 after stop");

    print_test_footer();
}

void test_frame_timer_expiry(void) {
    print_test_header("Test 6: Frame Timer Expiry Detection");

    timer_config config = {20.0, 0};
    timer_init(config);
    timer_reset_stats();

    uint16_t seq = 42;
    timer_start_frame(seq);

    sleep_ms(5);  // 5ms
    int expired = timer_frame_expired(seq);
    assert_equals(expired, 0, "frame not expired at 5ms");

    sleep_ms(20);  // 20ms more
    expired = timer_frame_expired(seq);
    assert_equals(expired, 1, "frame expired after RTO");

    timer_stats stats = timer_get_stats();
    assert_equals(stats.frames_expired, 1, "frames_expired incremented");

    print_test_footer();
}

void test_multiple_frame_timers(void) {
    print_test_header("Test 7: Multiple Frame Timers (Independent)");

    timer_config config = {30.0, 0};
    timer_init(config);
    timer_reset_stats();

    // Start 3 frames at different times
    timer_start_frame(10);
    sleep_ms(5);   // 5ms

    timer_start_frame(11);
    sleep_ms(5);   // 5ms

    timer_start_frame(12);
    // At this point: frame 10 has 10ms elapsed, frame 11 has 5ms, frame 12 has 0ms

    int exp10 = timer_frame_expired(10);
    int exp11 = timer_frame_expired(11);
    int exp12 = timer_frame_expired(12);

    assert_equals(exp10, 0, "frame 10 not expired yet");
    assert_equals(exp11, 0, "frame 11 not expired yet");
    assert_equals(exp12, 0, "frame 12 not expired yet");

    sleep_ms(30);  // 30ms more (total: frame 10 = 40ms, frame 11 = 35ms, frame 12 = 30ms)
    exp10 = timer_frame_expired(10);
    exp11 = timer_frame_expired(11);
    exp12 = timer_frame_expired(12);

    assert_equals(exp10, 1, "frame 10 expired (40ms > 30ms)");
    assert_equals(exp11, 1, "frame 11 expired (35ms > 30ms)");
    assert_equals(exp12, 1, "frame 12 expired (30ms >= 30ms)");

    print_test_footer();
}

void test_frame_remaining_ms(void) {
    print_test_header("Test 8: Frame Remaining Time Calculation");

    timer_config config = {50.0, 0};
    timer_init(config);
    timer_reset_stats();

    uint16_t seq = 99;
    timer_start_frame(seq);

    sleep_ms(10);  // 10ms
    double remaining = timer_frame_remaining_ms(seq);
    printf("  Remaining time after 10ms: %.2f ms\n", remaining);
    assert_approx(remaining, 40.0, 5.0, "remaining ≈ 40ms");

    sleep_ms(30);  // 30ms more
    remaining = timer_frame_remaining_ms(seq);
    printf("  Remaining time after 40ms: %.2f ms\n", remaining);
    assert_approx(remaining, 10.0, 5.0, "remaining ≈ 10ms");

    sleep_ms(20);  // 20ms more (total 60ms > 50ms)
    remaining = timer_frame_remaining_ms(seq);
    assert_equals((long long)remaining, 0, "remaining = 0 after expiry");

    print_test_footer();
}

void test_earliest_deadline_ms(void) {
    print_test_header("Test 9: Earliest Deadline Computation");

    timer_config config = {100.0, 0};
    timer_init(config);
    timer_reset_stats();

    timer_start_frame(10);
    double deadline1 = timer_earliest_deadline_ms();
    printf("  Deadline with 1 frame: %.2f ms\n", deadline1);
    assert_true(deadline1 > 0 && deadline1 <= 100.0, "deadline is positive and <= RTO");

    sleep_ms(10);  // 10ms
    timer_start_frame(20);

    double deadline2 = timer_earliest_deadline_ms();
    printf("  Deadline with 2 frames (frame 10 older): %.2f ms\n", deadline2);
    assert_true(deadline2 < deadline1, "deadline decreased (frame 10 closer to expiry)");

    print_test_footer();
}

void test_adaptive_rto_ema(void) {
    print_test_header("Test 10: Adaptive RTO with EMA");

    timer_config config = {100.0, 1};  // 100ms fixed, but with EMA enabled
    timer_init(config);
    timer_reset_stats();

    // Record 3 RTT samples: 20ms, 30ms, 40ms
    timer_record_rtt(20.0);
    timer_record_rtt(30.0);
    timer_record_rtt(40.0);

    double rto = timer_get_rto_ms();
    printf("  RTO after EMA samples: %.2f ms\n", rto);
    // EMA: 20, then 0.9*20 + 0.1*30 = 21, then 0.9*21 + 0.1*40 = 22.9
    // RTO = 2 * avg_rtt ≈ 2 * 22.9 = 45.8ms
    assert_approx(rto, 45.0, 10.0, "RTO adapted to 2 * avg_RTT");

    print_test_footer();
}

void test_stats_reset(void) {
    print_test_header("Test 11: Stats Reset");

    timer_config config = {50.0, 0};
    timer_init(config);

    timer_start_window();
    timer_stop_window();
    sleep_ms(60);
    timer_window_expired();

    timer_stats stats = timer_get_stats();
    assert_true(stats.windows_started > 0 || stats.windows_expired > 0, "stats accumulated");

    timer_reset_stats();
    stats = timer_get_stats();
    assert_equals(stats.windows_started, 0, "windows_started reset");
    assert_equals(stats.windows_expired, 0, "windows_expired reset");
    assert_equals(stats.frames_started, 0, "frames_started reset");
    assert_equals(stats.frames_expired, 0, "frames_expired reset");

    print_test_footer();
}

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              TIMER MODULE TEST SUITE (Phase 2)                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    test_timer_init();
    test_window_timer_start_stop();
    test_window_timer_idempotent();
    test_window_timer_expiry();
    test_frame_timer_lifecycle();
    test_frame_timer_expiry();
    test_multiple_frame_timers();
    test_frame_remaining_ms();
    test_earliest_deadline_ms();
    test_adaptive_rto_ema();
    test_stats_reset();

    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                      TEST SUMMARY                              ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");

    printf("Total assertions: %d\n", results.passed + results.failed);
    printf("✅ Passed: %d\n", results.passed);
    printf("❌ Failed: %d\n\n", results.failed);

    if (results.failed == 0) {
        printf("🎉 All tests PASSED!\n\n");
        return 0;
    } else {
        printf("⚠️  Some tests FAILED. Review output above.\n\n");
        return 1;
    }
}
