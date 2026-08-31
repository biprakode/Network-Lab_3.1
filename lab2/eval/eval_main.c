#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../arq/arq.h"
#include "../arq/sender.h"
#include "../arq/receiver.h"
#include "../../utils/utils.h"
#include "../channel.h"
#include "../timer.h"

// Thread arg structure
typedef struct {
    int fd;
    arq_config_t config;
} thread_arg_t;

// Sender thread wrapper
void* sender_thread_func(void* arg) {
    thread_arg_t* args = (thread_arg_t*)arg;
    sender_init(&args->config);
    int ret = sender_run(&args->config);
    free(args);
    return (void*)(intptr_t)ret;
}

// Receiver thread wrapper
void* receiver_thread_func(void* arg) {
    thread_arg_t* args = (thread_arg_t*)arg;
    receiver_init(&args->config);
    int ret = receiver_run(&args->config);
    free(args);
    return (void*)(intptr_t)ret;
}

// Generate input file
int generate_input_file(const char* filename, size_t size) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        perror("fopen input");
        return -1;
    }

    srand(42);  // Fixed seed for reproducibility
    uint8_t buffer[4096];
    size_t remaining = size;

    while (remaining > 0) {
        size_t chunk = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        for (size_t i = 0; i < chunk; i++) {
            buffer[i] = rand() & 0xFF;
        }
        fwrite(buffer, 1, chunk, f);
        remaining -= chunk;
    }

    fclose(f);
    return 0;
}

// Compare files
int files_equal(const char* file1, const char* file2, size_t expected_size) {
    uint8_t *data1 = NULL, *data2 = NULL;
    size_t size1 = 0, size2 = 0;

    data1 = read_file(file1, &size1);
    data2 = read_file(file2, &size2);

    if (!data1 || !data2 || size1 != size2 || size1 != expected_size) {
        if (data1) free(data1);
        if (data2) free(data2);
        return 0;
    }

    int result = (memcmp(data1, data2, size1) == 0) ? 1 : 0;
    free(data1);
    free(data2);
    return result;
}

int main(int argc, char* argv[]) {
    int num_trials = 5;
    if (argc > 1) {
        num_trials = atoi(argv[1]);
    }

    // Configuration
    const char* input_file = "eval/input.bin";
    const size_t input_size = 1024;

    arq_mode_t modes[] = {ARQ_SW, ARQ_GBN, ARQ_SR};
    const char* mode_names[] = {"SW", "GBN", "SR"};
    int window_bits[] = {1, 4, 4};
    double probabilities[] = {0.0};

    int num_modes = sizeof(modes) / sizeof(modes[0]);
    int num_probs = sizeof(probabilities) / sizeof(probabilities[0]);

    // Generate input file
    printf("[EVAL] Generating input file: %s (%zu bytes)\n", input_file, input_size);
    if (generate_input_file(input_file, input_size) != 0) {
        fprintf(stderr, "Failed to generate input file\n");
        return 1;
    }

    // Open CSV file
    FILE* csv = fopen("eval/EVAL_RESULTS.csv", "w");
    if (!csv) {
        perror("fopen EVAL_RESULTS.csv");
        return 1;
    }

    // Write CSV header
    fprintf(csv, "mode,window_bits,probability,trial,elapsed_sec,throughput_bps,frames_sent,");
    fprintf(csv, "frames_retransmitted,acks_received,corrupted_count,channel_frames_lost,");
    fprintf(csv, "channel_frames_corrupted,goodput_efficiency,correct\n");

    // Main sweep loop
    int total_trials = num_modes * num_probs * num_trials;
    int trial_count = 0;

    for (int m = 0; m < num_modes; m++) {
        for (int p = 0; p < num_probs; p++) {
            for (int t = 0; t < num_trials; t++) {
                trial_count++;
                printf("[TRIAL %d/%d] Mode=%s, P=%.1f, Trial=%d\n",
                       trial_count, total_trials, mode_names[m], probabilities[p], t + 1);

                // Reset and configure channel
                channel_reset_stats();
                channel_config cfg = {
                    .loss_prob = probabilities[p],
                    .corruption_prob = probabilities[p],
                    .delay_ms = 0
                };
                channel_init(cfg);
                timer_reset_stats();

                // Create socket pair
                int fds[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
                    perror("socketpair");
                    fclose(csv);
                    return 1;
                }

                // Build config structures
                char output_file[256];
                snprintf(output_file, sizeof(output_file), "eval/output_%d_%d_%d.bin", m, p, t);

                arq_config_t sender_config = {
                    .mode = modes[m],
                    .window_size = window_bits[m],
                    .socket_fd = fds[0],
                    .filename = (char*)input_file
                };

                arq_config_t receiver_config = {
                    .mode = modes[m],
                    .window_size = window_bits[m],
                    .socket_fd = fds[1],
                    .filename = output_file
                };

                // Measure time
                struct timespec start, end;
                clock_gettime(CLOCK_MONOTONIC, &start);

                // Spawn threads
                pthread_t sender_tid, receiver_tid;

                thread_arg_t* sender_arg = malloc(sizeof(thread_arg_t));
                thread_arg_t* receiver_arg = malloc(sizeof(thread_arg_t));
                sender_arg->fd = fds[0];
                sender_arg->config = sender_config;
                receiver_arg->fd = fds[1];
                receiver_arg->config = receiver_config;

                pthread_create(&receiver_tid, NULL, receiver_thread_func, receiver_arg);
                pthread_create(&sender_tid, NULL, sender_thread_func, sender_arg);

                // Wait for completion
                pthread_join(receiver_tid, NULL);
                pthread_join(sender_tid, NULL);

                clock_gettime(CLOCK_MONOTONIC, &end);

                // Close sockets
                close(fds[0]);
                close(fds[1]);

                // Compute elapsed time
                double elapsed = (end.tv_sec - start.tv_sec) +
                                (end.tv_nsec - start.tv_nsec) / 1e9;

                // Get stats
                arq_stats_t sender_stats = sender_get_stats(&sender_config);
                channel_stats ch_stats = channel_get_stats();

                // Check correctness
                int correct = files_equal(input_file, output_file, input_size);

                // Compute metrics
                double throughput_bps = (input_size * 8) / elapsed;
                uint32_t total_frames = sender_stats.frames_sent + sender_stats.frames_retransmitted;
                double goodput_efficiency = (total_frames > 0) ?
                    ((double)input_size / (total_frames * 65)) : 0.0;

                // Write CSV row
                fprintf(csv, "%s,%d,%.1f,%d,%.6f,%.2f,%u,%u,%u,%u,%lu,%lu,%.4f,%d\n",
                        mode_names[m],
                        window_bits[m],
                        probabilities[p],
                        t + 1,
                        elapsed,
                        throughput_bps,
                        sender_stats.frames_sent,
                        sender_stats.frames_retransmitted,
                        sender_stats.acks_received,
                        sender_stats.corrupted_count,
                        ch_stats.frames_lost,
                        ch_stats.frames_corrupted,
                        goodput_efficiency,
                        correct);
                fflush(csv);

                // Clean up output file
                unlink(output_file);

                printf("  → elapsed=%.3fs, goodput=%.4f, correct=%s\n",
                       elapsed, goodput_efficiency, correct ? "yes" : "NO");
            }
        }
    }

    fclose(csv);
    printf("\n[EVAL] Complete. Results written to eval/EVAL_RESULTS.csv\n");

    return 0;
}
