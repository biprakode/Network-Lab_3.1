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

static const char* CSV_PATH = "eval/EVAL_RESULTS.csv";
static const char* CSV_HEADER =
    "mode,window_bits,probability,trial,elapsed_sec,throughput_bps,frames_sent,"
    "frames_retransmitted,acks_received,corrupted_count,channel_frames_lost,"
    "channel_frames_corrupted,goodput_efficiency,correct,"
    "rtt_mean_ms,rtt_min_ms,rtt_max_ms,rtt_samples\n";

static const char* INPUT_FILE = "eval/input.bin";
static const size_t INPUT_SIZE = 384;

static arq_mode_t MODES[] = {ARQ_SW, ARQ_GBN, ARQ_SR};
static const char* MODE_NAMES[] = {"SW", "GBN", "SR"};
static int WINDOW_BITS[] = {1, 4, 4};

// Run one transfer and append one CSV row. Returns 0 on success.
static int run_trial(FILE* csv, int m, double prob, int trial_no) {
    channel_reset_stats();
    channel_config cfg = {.loss_prob = prob, .corruption_prob = prob, .delay_ms = 0};
    channel_init(cfg);
    timer_reset_stats();

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        return -1;
    }

    char output_file[256];
    snprintf(output_file, sizeof(output_file), "eval/output_%d_%.0f_%d.bin", m, prob * 10, trial_no);

    arq_config_t sender_config = {.mode = MODES[m], .window_size = WINDOW_BITS[m], .socket_fd = fds[0], .filename = (char*)INPUT_FILE};
    arq_config_t receiver_config = {.mode = MODES[m], .window_size = WINDOW_BITS[m], .socket_fd = fds[1], .filename = output_file};

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_t sender_tid, receiver_tid;
    thread_arg_t* sender_arg = malloc(sizeof(thread_arg_t));
    thread_arg_t* receiver_arg = malloc(sizeof(thread_arg_t));
    sender_arg->fd = fds[0];
    sender_arg->config = sender_config;
    receiver_arg->fd = fds[1];
    receiver_arg->config = receiver_config;

    pthread_create(&receiver_tid, NULL, receiver_thread_func, receiver_arg);
    pthread_create(&sender_tid, NULL, sender_thread_func, sender_arg);
    pthread_join(receiver_tid, NULL);
    // The receiver has delivered everything and hung up. Close its socket end so
    // the sender sees EOF and stops waiting for a final ACK that may be lost.
    close(fds[1]);
    pthread_join(sender_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    close(fds[0]);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    arq_stats_t s = sender_get_stats(&sender_config);
    channel_stats ch = channel_get_stats();
    int correct = files_equal(INPUT_FILE, output_file, INPUT_SIZE);

    double throughput_bps = (INPUT_SIZE * 8) / elapsed;
    uint32_t total_frames = s.frames_sent + s.frames_retransmitted;
    double goodput = (total_frames > 0) ? ((double)INPUT_SIZE / (total_frames * 65)) : 0.0;
    double rtt_mean = (s.rtt_samples > 0) ? (s.rtt_sum_ms / s.rtt_samples) : 0.0;

    fprintf(csv, "%s,%d,%.1f,%d,%.6f,%.2f,%u,%u,%u,%u,%lu,%lu,%.4f,%d,%.4f,%.4f,%.4f,%u\n",
            MODE_NAMES[m], WINDOW_BITS[m], prob, trial_no, elapsed, throughput_bps,
            s.frames_sent, s.frames_retransmitted, s.acks_received, s.corrupted_count,
            ch.frames_lost, ch.frames_corrupted, goodput, correct,
            rtt_mean, s.rtt_min_ms, s.rtt_max_ms, s.rtt_samples);
    fflush(csv);
    unlink(output_file);

    printf("  %s p=%.1f t=%d -> %.3fs goodput=%.4f correct=%s\n",
           MODE_NAMES[m], prob, trial_no, elapsed, goodput, correct ? "yes" : "NO");
    return 0;
}

static int ensure_input(void) {
    FILE* f = fopen(INPUT_FILE, "rb");
    if (f) { fclose(f); return 0; }
    printf("[EVAL] Generating input file: %s (%zu bytes)\n", INPUT_FILE, INPUT_SIZE);
    return generate_input_file(INPUT_FILE, INPUT_SIZE);
}

int main(int argc, char* argv[]) {
    // Single combo mode: eval_main one <mode_idx 0..2> <prob> <trial_no>
    if (argc >= 5 && strcmp(argv[1], "one") == 0) {
        if (ensure_input() != 0) return 1;
        int m = atoi(argv[2]);
        double prob = atof(argv[3]);
        int trial_no = atoi(argv[4]);
        int have_file = 0;
        FILE* probe = fopen(CSV_PATH, "rb");
        if (probe) { have_file = 1; fclose(probe); }
        FILE* csv = fopen(CSV_PATH, "a");
        if (!csv) { perror("fopen csv"); return 1; }
        if (!have_file) fputs(CSV_HEADER, csv);
        int rc = run_trial(csv, m, prob, trial_no);
        fclose(csv);
        return rc == 0 ? 0 : 1;
    }

    // Full sweep mode: eval_main [num_trials]
    int num_trials = 2;
    if (argc > 1) num_trials = atoi(argv[1]);

    if (ensure_input() != 0) { fprintf(stderr, "Failed to generate input file\n"); return 1; }

    FILE* csv = fopen(CSV_PATH, "w");
    if (!csv) { perror("fopen csv"); return 1; }
    fputs(CSV_HEADER, csv);

    double probabilities[] = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5};
    int num_modes = (int)(sizeof(MODES) / sizeof(MODES[0]));
    int num_probs = (int)(sizeof(probabilities) / sizeof(probabilities[0]));

    for (int m = 0; m < num_modes; m++)
        for (int p = 0; p < num_probs; p++)
            for (int t = 1; t <= num_trials; t++)
                run_trial(csv, m, probabilities[p], t);

    fclose(csv);
    printf("\n[EVAL] Complete. Results written to %s\n", CSV_PATH);

    return 0;
}
