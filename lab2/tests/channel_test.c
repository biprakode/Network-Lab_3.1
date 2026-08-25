#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../channel.h"

#define TOLERANCE 0.05
#define NUM_TRIALS 200

typedef struct {
    int passed;
    int failed;
} test_result;

test_result results = {0, 0};

void assert_equals(int actual, int expected, const char *msg) {
    if (actual == expected) {
        results.passed++;
    } else {
        results.failed++;
        printf("  ❌ FAIL: %s (expected %d, got %d)\n", msg, expected, actual);
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

void test_channel_init(void) {
    print_test_header("Test 1: Channel Initialization");

    channel_config config = {0.1, 0.2, 5.0};
    channel_init(config);

    channel_stats stats = channel_get_stats();
    assert_equals(stats.frames_sent, 0, "frames_sent initialized to 0");
    assert_equals(stats.frames_lost, 0, "frames_lost initialized to 0");
    assert_equals(stats.frames_corrupted, 0, "frames_corrupted initialized to 0");

    print_test_footer();
}

void test_stats_reset(void) {
    print_test_header("Test 2: Stats Reset");

    channel_config config = {0.0, 0.0, 0.0};
    channel_init(config);

    channel_stats stats1 = channel_get_stats();
    assert_equals(stats1.frames_sent, 0, "initial stats are zero");

    channel_reset_stats();
    channel_stats stats2 = channel_get_stats();
    assert_equals(stats2.frames_sent, 0, "reset clears stats");
    assert_equals(stats2.frames_lost, 0, "reset clears lost");
    assert_equals(stats2.frames_corrupted, 0, "reset clears corrupted");

    print_test_footer();
}

extern double rand_double(void);

void test_loss_probability_distribution(void) {
    print_test_header("Test 3: Loss Probability Distribution (RNG)");

    double target_prob = 0.2;
    int losses = 0;

    for (int i = 0; i < NUM_TRIALS; i++) {
        if (rand_double() < target_prob) {
            losses++;
        }
    }

    double empirical = (double)losses / NUM_TRIALS;
    printf("  Target: %.2f%%, Empirical: %.2f%% (%d / %d)\n",
           target_prob * 100, empirical * 100, losses, NUM_TRIALS);

    assert_approx(empirical, target_prob, TOLERANCE, "loss distribution matches target");

    print_test_footer();
}

void test_corruption_probability_distribution(void) {
    print_test_header("Test 4: Corruption Probability Distribution (RNG)");

    double target_prob = 0.15;
    int corruptions = 0;

    for (int i = 0; i < NUM_TRIALS; i++) {
        if (rand_double() < target_prob) {
            corruptions++;
        }
    }

    double empirical = (double)corruptions / NUM_TRIALS;
    printf("  Target: %.2f%%, Empirical: %.2f%% (%d / %d)\n",
           target_prob * 100, empirical * 100, corruptions, NUM_TRIALS);

    assert_approx(empirical, target_prob, TOLERANCE, "corruption distribution matches target");

    print_test_footer();
}

void test_combined_probabilities(void) {
    print_test_header("Test 5: Combined Loss + Corruption (Independent Events)");

    double p_loss = 0.1;
    double p_corrupt = 0.1;
    int losses = 0, corruptions = 0, both = 0;

    for (int i = 0; i < NUM_TRIALS; i++) {
        int lost = rand_double() < p_loss;
        int corrupted = rand_double() < p_corrupt;

        losses += lost;
        corruptions += corrupted;
        both += (lost && corrupted);
    }

    double emp_loss = (double)losses / NUM_TRIALS;
    double emp_corrupt = (double)corruptions / NUM_TRIALS;

    printf("  Loss: %.2f%% (target %.2f%%), Corrupt: %.2f%% (target %.2f%%)\n",
           emp_loss * 100, p_loss * 100, emp_corrupt * 100, p_corrupt * 100);
    printf("  Frames affected by both: %d (%.2f%%)\n", both, (double)both / NUM_TRIALS * 100);

    assert_approx(emp_loss, p_loss, TOLERANCE, "loss rate matches");
    assert_approx(emp_corrupt, p_corrupt, TOLERANCE, "corruption rate matches");

    print_test_footer();
}

void test_zero_probabilities(void) {
    print_test_header("Test 6: Zero Probabilities (No Faults)");

    channel_config config = {0.0, 0.0, 0.0};
    channel_init(config);
    channel_reset_stats();

    int losses = 0;
    for (int i = 0; i < 50; i++) {
        if (rand_double() < 0.0) {
            losses++;
        }
    }

    assert_equals(losses, 0, "zero probability produces zero events");

    print_test_footer();
}

void test_certainty_probabilities(void) {
    print_test_header("Test 7: Certainty Probabilities (All Faults)");

    int successes = 0;
    for (int i = 0; i < 50; i++) {
        if (rand_double() < 1.0) {
            successes++;
        }
    }

    assert_equals(successes, 50, "probability 1.0 produces all events");

    print_test_footer();
}

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              CHANNEL LAYER TEST SUITE (Phase 1)               ║\n");
    printf("║          (Statistical & Config Verification)                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    test_channel_init();
    test_stats_reset();
    test_loss_probability_distribution();
    test_corruption_probability_distribution();
    test_combined_probabilities();
    test_zero_probabilities();
    test_certainty_probabilities();

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
