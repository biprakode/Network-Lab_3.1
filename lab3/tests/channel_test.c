#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include "../channel.h"
#include "../protocol.h"
#include "../../utils/utils.h"
#include "../../lab1/scheme.h"

/* ------------------------------------------------------------------ */
/* tiny assert framework, same shape as lab2/tests                    */

typedef struct { int passed; int failed; } test_result_t;
static test_result_t results = {0, 0};

static void assert_true(int cond, const char *msg) {
    if (cond) { results.passed++; }
    else { results.failed++; printf("  FAIL: %s\n", msg); }
}

static void assert_eq_u64(uint64_t actual, uint64_t expected, const char *msg) {
    if (actual == expected) { results.passed++; }
    else { results.failed++; printf("  FAIL: %s (expected %llu, got %llu)\n",
                                     msg, (unsigned long long)expected, (unsigned long long)actual); }
}

static void assert_approx(double actual, double expected, double tol, const char *msg) {
    double diff = actual - expected;
    if (diff < 0) diff = -diff;
    if (diff <= tol) { results.passed++; }
    else { results.failed++; printf("  FAIL: %s (expected ~%.4f, got %.4f)\n", msg, expected, actual); }
}

static void header(const char *name) { printf("\n%s\n", name); }
static void footer(void) {
    if (results.failed == 0) printf("  PASS\n");
    else printf("  %d failed so far\n", results.failed);
}

/* ------------------------------------------------------------------ */
/* Part 1: pure resolver tests (no sockets) — tests 1-9               */

static void test_medium_from_txcount(void) {
    header("Test 1: medium_from_txcount");
    assert_true(medium_from_txcount(0) == MEDIUM_IDLE, "0 tx -> IDLE");
    assert_true(medium_from_txcount(1) == MEDIUM_BUSY, "1 tx -> BUSY");
    assert_true(medium_from_txcount(2) == MEDIUM_COLLISION, "2 tx -> COLLISION");
    assert_true(medium_from_txcount(5) == MEDIUM_COLLISION, "5 tx -> COLLISION");
    footer();
}

static void test_all_idle(void) {
    header("Test 2: all ACT_IDLE -> IDLE, transmitters 0");
    uint8_t actions[4] = {ACT_IDLE, ACT_IDLE, ACT_IDLE, ACT_IDLE};
    slot_outcome_t o = channel_resolve_slot(actions, 4, MEDIUM_IDLE);
    assert_true(o.state == MEDIUM_IDLE, "state IDLE");
    assert_eq_u64(o.transmitters, 0, "transmitters == 0");
    footer();
}

static void test_one_start(void) {
    header("Test 3: one ACT_START -> BUSY, starts == 1");
    uint8_t actions[3] = {ACT_START, ACT_IDLE, ACT_IDLE};
    slot_outcome_t o = channel_resolve_slot(actions, 3, MEDIUM_IDLE);
    assert_true(o.state == MEDIUM_BUSY, "state BUSY");
    assert_eq_u64(o.starts, 1, "starts == 1");
    assert_eq_u64(o.transmitters, 1, "transmitters == 1");
    footer();
}

static void test_one_transmit(void) {
    header("Test 4: one ACT_TRANSMIT (mid-frame keepalive) -> BUSY");
    uint8_t actions[2] = {ACT_TRANSMIT, ACT_IDLE};
    slot_outcome_t o = channel_resolve_slot(actions, 2, MEDIUM_BUSY);
    assert_true(o.state == MEDIUM_BUSY, "state BUSY");
    assert_eq_u64(o.starts, 0, "starts == 0 (not a fresh START)");
    footer();
}

static void test_start_plus_transmit_collision(void) {
    header("Test 5: START + TRANSMIT -> COLLISION, transmitters == 2");
    uint8_t actions[2] = {ACT_START, ACT_TRANSMIT};
    slot_outcome_t o = channel_resolve_slot(actions, 2, MEDIUM_BUSY);
    assert_true(o.state == MEDIUM_COLLISION, "state COLLISION");
    assert_eq_u64(o.transmitters, 2, "transmitters == 2");
    footer();
}

static void test_new_collision_event_from_idle(void) {
    header("Test 6: two STARTs, prev_state IDLE -> new_collision_event == 1");
    uint8_t actions[2] = {ACT_START, ACT_START};
    slot_outcome_t o = channel_resolve_slot(actions, 2, MEDIUM_IDLE);
    assert_true(o.state == MEDIUM_COLLISION, "state COLLISION");
    assert_true(o.new_collision_event == 1, "new_collision_event == 1");
    footer();
}

static void test_new_collision_event_continuing(void) {
    header("Test 7: two STARTs, prev_state COLLISION -> new_collision_event == 0");
    uint8_t actions[2] = {ACT_START, ACT_START};
    slot_outcome_t o = channel_resolve_slot(actions, 2, MEDIUM_COLLISION);
    assert_true(o.state == MEDIUM_COLLISION, "state COLLISION");
    assert_true(o.new_collision_event == 0, "new_collision_event == 0 (same contiguous period)");
    footer();
}

static void test_jam_counts(void) {
    header("Test 8: JAM counts toward transmitters (JAM + TRANSMIT -> COLLISION)");
    uint8_t actions[2] = {ACT_JAM, ACT_TRANSMIT};
    slot_outcome_t o = channel_resolve_slot(actions, 2, MEDIUM_COLLISION);
    assert_true(o.state == MEDIUM_COLLISION, "state COLLISION");
    assert_eq_u64(o.transmitters, 2, "transmitters == 2");
    footer();
}

static void test_done_and_idle_never_raise_transmitters(void) {
    header("Test 9: ACT_DONE / ACT_IDLE never raise transmitters");
    uint8_t actions[3] = {ACT_DONE, ACT_IDLE, ACT_DONE};
    slot_outcome_t o = channel_resolve_slot(actions, 3, MEDIUM_IDLE);
    assert_true(o.state == MEDIUM_IDLE, "state IDLE");
    assert_eq_u64(o.transmitters, 0, "transmitters == 0");
    assert_eq_u64(o.starts, 0, "starts == 0");
    footer();
}

/* ------------------------------------------------------------------ */
/* Part 2: socket integration tests — tests 10-14                     */
/* Each test forks N mock "station" processes that speak the real     */
/* wire protocol over loopback TCP, while THIS process plays the      */
/* channel by calling channel_serve() directly.                       */

typedef struct {
    uint32_t slot;
    uint8_t  medium_state;
    uint8_t  last_result;
} tick_info_t;

/* returns 1 if this was the station's final message (it just sent ACT_DONE) */
typedef int (*decide_fn)(tick_info_t tick, action_msg_t *out, void *ctx);

static frame_t make_frame(uint32_t enqueue_slot) {
    frame_t f;
    memset(&f, 0, sizeof f);
    csma_sim_t *meta = (csma_sim_t *)f.payload;
    meta->enqueue_slot = htonl(enqueue_slot);
    meta->frame_uid = htonl(0);
    uint32_t fcs = compute_fcs(SCHEME_CHECKSUM16, (uint8_t *)&f, FRAME_SIZE - TRAILER_SIZE);
    f.trailer.fcs = htonl(fcs);
    return f;
}

static int connect_retry(uint16_t port) {
    for (int i = 0; i < 200; i++) {
        int fd = tcp_connect("127.0.0.1", port);
        if (fd >= 0) return fd;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 5000000 };
        nanosleep(&ts, NULL);
    }
    return -1;
}

/* child process: speak the protocol until should_stop or decide() says done.
 * bad_slot_offset != 0 is only used by the desync test (14). */
static void run_mock_station(uint16_t port, decide_fn decide, void *ctx, uint32_t bad_slot_offset) {
    int fd = connect_retry(port);
    if (fd < 0) _exit(1);

    config_msg_t cfg;
    if (recv_all(fd, &cfg, sizeof cfg) != (ssize_t)sizeof cfg) { close_conn(fd); _exit(1); }

    for (;;) {
        tick_msg_t tick;
        if (recv_all(fd, &tick, sizeof tick) != (ssize_t)sizeof tick) break;
        if (tick.should_stop) break;

        tick_info_t info = { ntohl(tick.slot), tick.medium_state, tick.last_result };
        action_msg_t act;
        memset(&act, 0, sizeof act);

        int done = decide(info, &act, ctx);
        act.slot = htonl(info.slot + bad_slot_offset);

        if (send_all(fd, &act, sizeof act) != (ssize_t)sizeof act) break;
        if (done) break;
    }
    close_conn(fd);
    _exit(0);
}

/* ---- decide() implementations ---- */

typedef struct {
    int started;
    int wait_for_idle;   /* only START once medium_state == MEDIUM_IDLE   */
    int jam_after_start; /* send exactly one ACT_JAM right after starting */
    int jam_pending;
} contend_ctx_t;

static int decide_contend(tick_info_t tick, action_msg_t *out, void *ctxp) {
    contend_ctx_t *ctx = ctxp;
    if (!ctx->started) {
        if (ctx->wait_for_idle && tick.medium_state != MEDIUM_IDLE) {
            out->action = ACT_IDLE;
            return 0;
        }
        out->action = ACT_START;
        out->frame = make_frame(tick.slot);
        ctx->started = 1;
        ctx->jam_pending = ctx->jam_after_start;
        return 0;
    }
    if (ctx->jam_pending) {
        out->action = ACT_JAM;
        ctx->jam_pending = 0;
        return 0;
    }
    if (tick.last_result != RES_NONE) {
        out->action = ACT_DONE;
        return 1;
    }
    out->action = ACT_TRANSMIT;
    return 0;
}

typedef struct {
    int enqueue_slot;
    uint32_t start_at_slot;
    int started;
} delay_ctx_t;

static int decide_delay(tick_info_t tick, action_msg_t *out, void *ctxp) {
    delay_ctx_t *ctx = ctxp;
    if (!ctx->started) {
        if (tick.slot < ctx->start_at_slot) {
            out->action = ACT_IDLE;
            return 0;
        }
        out->action = ACT_START;
        out->frame = make_frame((uint32_t)ctx->enqueue_slot);
        ctx->started = 1;
        return 0;
    }
    if (tick.last_result != RES_NONE) {
        out->action = ACT_DONE;
        return 1;
    }
    out->action = ACT_TRANSMIT;
    return 0;
}

static int decide_idle_forever(tick_info_t tick, action_msg_t *out, void *ctxp) {
    (void)tick; (void)ctxp;
    out->action = ACT_IDLE;
    return 0;
}

/* ---- test 10: forced simultaneous collision ---- */

static void test_forced_collision(void) {
    header("Test 10: 2 stations START together -> both collide, nothing delivered");
    uint16_t port = 19310;

    contend_ctx_t a = {0}, b = {0};
    pid_t pa = fork();
    if (pa == 0) run_mock_station(port, decide_contend, &a, 0);
    pid_t pb = fork();
    if (pb == 0) run_mock_station(port, decide_contend, &b, 0);

    channel_params_t params = { .port = port, .n_stations = 2, .slot_frame_len = 4,
                                 .detect_collisions = 0, .max_slots = 20, .seed = 0, .sink_path = NULL };
    channel_metrics_t m;
    int rc = channel_serve(&params, &m);
    waitpid(pa, NULL, 0);
    waitpid(pb, NULL, 0);

    assert_true(rc == 0, "channel_serve returns 0");
    assert_eq_u64(m.frames_delivered, 0, "frames_delivered == 0");
    assert_eq_u64(m.collision_events, 1, "collision_events == 1 (one contiguous period)");
    footer();
}

/* ---- test 11: staggered, non-overlapping ---- */

static void test_staggered_no_collision(void) {
    header("Test 11: A starts first and finishes, then B starts -> both delivered, no collision");
    uint16_t port = 19311;

    contend_ctx_t a = {0};                 /* starts immediately */
    contend_ctx_t b = {0}; b.wait_for_idle = 1; /* only starts once medium senses IDLE */

    pid_t pa = fork();
    if (pa == 0) run_mock_station(port, decide_contend, &a, 0);
    pid_t pb = fork();
    if (pb == 0) run_mock_station(port, decide_contend, &b, 0);

    channel_params_t params = { .port = port, .n_stations = 2, .slot_frame_len = 4,
                                 .detect_collisions = 0, .max_slots = 30, .seed = 0, .sink_path = NULL };
    channel_metrics_t m;
    int rc = channel_serve(&params, &m);
    waitpid(pa, NULL, 0);
    waitpid(pb, NULL, 0);

    assert_true(rc == 0, "channel_serve returns 0");
    assert_eq_u64(m.frames_delivered, 2, "frames_delivered == 2");
    assert_eq_u64(m.collision_events, 0, "collision_events == 0");
    double expected_throughput = (2.0 * params.slot_frame_len) / (double)m.total_slots;
    assert_approx(m.throughput, expected_throughput, 1e-9, "throughput == 2*T_frame/total_slots");
    footer();
}

/* ---- test 12: delay arithmetic ---- */

static void test_delay_math(void) {
    header("Test 12: enqueue at slot 3, START at slot 5, T_frame 8 -> delay == 10");
    uint16_t port = 19312;

    delay_ctx_t d = { .enqueue_slot = 3, .start_at_slot = 5, .started = 0 };
    pid_t p = fork();
    if (p == 0) run_mock_station(port, decide_delay, &d, 0);

    channel_params_t params = { .port = port, .n_stations = 1, .slot_frame_len = 8,
                                 .detect_collisions = 0, .max_slots = 30, .seed = 0, .sink_path = NULL };
    channel_metrics_t m;
    int rc = channel_serve(&params, &m);
    waitpid(p, NULL, 0);

    assert_true(rc == 0, "channel_serve returns 0");
    assert_eq_u64(m.frames_delivered, 1, "frames_delivered == 1");
    assert_eq_u64(m.sum_delay_slots, 10, "sum_delay_slots == 5 + 8 - 3 == 10");
    assert_approx(m.avg_delay_slots, 10.0, 1e-9, "avg_delay_slots == 10");
    footer();
}

/* ---- test 13: CSMA/CD early abort ---- */

static void test_csma_cd_abort(void) {
    header("Test 13: both START then both JAM one slot later -> both RES_COLLISION, no full T_frame occupied");
    uint16_t port = 19313;

    contend_ctx_t a = {0}, b = {0};
    a.jam_after_start = 1;
    b.jam_after_start = 1;

    pid_t pa = fork();
    if (pa == 0) run_mock_station(port, decide_contend, &a, 0);
    pid_t pb = fork();
    if (pb == 0) run_mock_station(port, decide_contend, &b, 0);

    channel_params_t params = { .port = port, .n_stations = 2, .slot_frame_len = 8,
                                 .detect_collisions = 1, .max_slots = 20, .seed = 0, .sink_path = NULL };
    channel_metrics_t m;
    int rc = channel_serve(&params, &m);
    waitpid(pa, NULL, 0);
    waitpid(pb, NULL, 0);

    assert_true(rc == 0, "channel_serve returns 0");
    assert_eq_u64(m.frames_delivered, 0, "frames_delivered == 0");
    assert_eq_u64(m.collided_attempts, 2, "collided_attempts == 2 (both aborted via JAM)");
    footer();
}

/* ---- test 14: desync guard ---- */

static void test_desync_guard(void) {
    header("Test 14: a station echoes the wrong slot number -> channel_serve returns -1");
    uint16_t port = 19314;

    pid_t p = fork();
    if (p == 0) run_mock_station(port, decide_idle_forever, NULL, 1 /* always off by one */);

    channel_params_t params = { .port = port, .n_stations = 1, .slot_frame_len = 4,
                                 .detect_collisions = 0, .max_slots = 20, .seed = 0, .sink_path = NULL };
    channel_metrics_t m;
    int rc = channel_serve(&params, &m);
    waitpid(p, NULL, 0);

    assert_true(rc == -1, "channel_serve returns -1 on desync");
    footer();
}

/* ------------------------------------------------------------------ */

int main(void) {
    test_medium_from_txcount();
    test_all_idle();
    test_one_start();
    test_one_transmit();
    test_start_plus_transmit_collision();
    test_new_collision_event_from_idle();
    test_new_collision_event_continuing();
    test_jam_counts();
    test_done_and_idle_never_raise_transmitters();

    test_forced_collision();
    test_staggered_no_collision();
    test_delay_math();
    test_csma_cd_abort();
    test_desync_guard();

    printf("\n=====================================\n");
    printf("Total: %d passed, %d failed\n", results.passed, results.failed);
    printf("=====================================\n");
    return results.failed == 0 ? 0 : 1;
}
