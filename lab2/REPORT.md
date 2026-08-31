# Assignment 2 Report: ARQ Protocols (Flow Control)

## 1. Introduction

This assignment implements three Automatic Repeat reQuest (ARQ) protocols for reliable data transfer over a lossy simulated channel: Stop-and-Wait (SW), Go-Back-N (GBN), and Selective Repeat (SR). Each protocol uses a sliding-window approach to manage sender state, with differences in window size, receiver behavior, and retransmission strategy. The evaluation measures efficiency (goodput), latency, and correctness across error rates 0.0–0.5.

## 2. Architecture

### 2.1 Module Layout

```
lab2/
  arq/
    sender.c / sender.h        Three modes: sender_sw_mode, sender_gbn_mode, sender_sr_mode
    receiver.c / receiver.h    Three modes: receiver_sw_mode, receiver_gbn_mode, receiver_sr_mode
    arq.h                       Frame type constants, config and stats structures
  channel.c / channel.h         Loss/corruption/delay simulation (applied in channel_send)
  timer.c / timer.h            Fixed RTO and per-frame timer management
  eval/
    eval_main.c                Automated harness: sweep modes × probabilities × trials
    EVAL_RESULTS.csv           Raw per-trial data (mode, prob, elapsed, goodput, correctness)
  tests/
    arq_test.c                 15 unit tests (frame structure, FCS, byte order)
    channel_test.c             7 tests (empirical loss/corruption/delay rates)
    timer_test.c               11 tests (timer logic)
```

### 2.2 Reused Infrastructure

- **lab1/frame.h**: 65-byte frame (16B header + 44B payload + 4B trailer FCS)
- **lab1/error.c / scheme.c**: FCS computation (SCHEME_CHECKSUM16)
- **lab1/error_inject/bit_flip.h**: Bit-flip primitive for corruption simulation
- **utils/utils.h**: File I/O, socket utilities, send_all/recv_all

### 2.3 Frame Format

```
Byte 0:  frame_type (0=DATA, 1=ACK, 2=NAK)
Bytes 1–6: dest_addr[6] (unused/arbitrary in ACK/NAK)
Bytes 7–12: src_addr[6] (frame_type lives here; repurposed byte 0)
Bytes 13–14: frame_len (16-bit, network order) — 0 signals end-of-transmission
Bytes 15–16: frame_seq (16-bit, network order) — sequence number Sn/Rn
Bytes 17–60: payload[44] (user data or zeroed for ACK/NAK)
Bytes 61–64: FCS[4] (16-bit CRC, network order)
```

**FCS Scheme**: Computed identically for DATA, ACK, and NAK frames using `compute_fcs(SCHEME_CHECKSUM16, ...)`.

### 2.4 Channel Simulation

**channel.c** wraps send/recv with configurable loss and corruption:
- **Loss probability** `p`: frame dropped with probability p (in `channel_send` only)
- **Corruption probability** `p`: frame bit-flipped with probability p (in `channel_send` only)
- **Delay** (configurable, unused in tests): 0ms for all evaluation runs
- **Single-roll semantics**: Impairment is applied once per transit, in `channel_send`. The receiver path is a plain passthrough to `recv_all`.

### 2.5 Timer Management

**timer.c** provides:
- **Fixed RTO**: 100ms per configuration `timer_init({rto_ms: 100.0, use_ema: 0})`
- **Window timer** (SW/GBN): Single timer for entire outstanding window; fires if no ACK arrives within RTO
- **Per-frame timers** (SR): One timer slot per outstanding frame; on timeout, resend only the expired frame

## 3. Protocol Implementations

### 3.1 Stop-and-Wait (Sw = 1)

**Sender**:
1. Send frame Sn
2. Start timer
3. Wait for ACK(Sn)
   - If valid ACK → advance Sn, repeat
   - If timeout → resend, restart timer
4. On last data frame complete, send zero-length EOT frame with same handshake
5. Return after EOT ACK received

**Receiver**:
1. Receive frame
2. If corrupted or out-of-order → send ACK for last good frame, discard
3. If valid and in-order → deliver data, advance Rn
4. Send ACK(Rn) [cumulative: acknowledges all up to Rn-1]
5. If frame delivered was zero-length (EOT) → break

**Characteristics**:
- Throughput limited: one frame in flight at a time
- Simple: no buffering, no per-frame state
- Robust: single timer suffices

### 3.2 Go-Back-N (Sw = 2^4 - 1 = 15)

**Sender**:
1. Check if window full: `Sn - Sf ≥ Sw`
   - If full: wait for ACK(s) via select() with safety timeout
   - Process incoming ACK: if valid and `Sf < ackNo ≤ Sn`, slide `Sf → ackNo` and stop timer if window empty
   - On timeout: resend entire window [Sf, Sn) and restart timer
2. If room: send frame Sn, store in buffer, increment Sn, start timer if not running
3. After all data sent: send zero-length EOT frame, then drain window until all ACKed
4. Return

**Receiver**:
1. Receive frame
2. If corrupted or non-DATA → discard, send ACK for current Rn anyway
3. If out-of-order → discard, send ACK for current Rn anyway
4. If valid and in-order:
   - Deliver data
   - Check if zero-length (EOT)
   - Advance Rn
5. Send ACK(Rn) [cumulative]
6. If delivered frame was EOT → break

**Characteristics**:
- Larger window (15 vs 1) enables pipelining
- Cumulative ACK: single ACK confirms multiple frames
- Conservative receiver: discards all out-of-order (simplest logic, slight inefficiency on loss)
- Single timer: shared across window; any loss triggers full window retransmit

### 3.3 Selective Repeat (Sw = 2^3 = 8)

**Sender**:
1. Check if window full: `Sn - Sf ≥ Sw`
   - If full: wait via select() with timeout = minimum remaining time across all pending frame timers
   - Process ACK: if valid and `Sf < ackNo ≤ Sn`, slide window and stop timers for purged frames
   - Process NAK: if valid and `Sf ≤ nakNo < Sn`, resend immediately and restart timer
   - On timeout: resend earliest expired frame and restart its timer
2. If room: send frame Sn, stamp timer slot with send_time, increment Sn
3. After all data sent: send EOT frame, then drain window until all ACKed
4. Return

**Receiver**:
1. Receive frame
2. If corrupted and `!nak_sent` → send NAK(Rn), set nak_sent=true, continue
3. If out-of-order and `!nak_sent` → send NAK(Rn), set nak_sent=true
4. If valid and in window [Rn, Rn+Sw):
   - Buffer frame
   - Deliver all consecutive in-order frames starting from Rn
   - Check if any delivered frame is zero-length (EOT)
   - Advance Rn past all delivered
   - Set ack_needed if any delivered
5. If ack_needed:
   - Send ACK(Rn) [per-frame, independent ACKs]
   - Clear nak_sent [ready to NAK new gaps]
   - If EOT delivered → break

**Characteristics**:
- Smallest window (8 vs 15, 1) but full performance under high loss
- Per-frame timers: fine-grained timeout (only lost frames resend)
- NAK mechanism: explicit out-of-order notification for faster recovery
- Independent ACKs: each confirms one frame (vs cumulative in GBN)
- Receiver buffers: no packet loss under reordering (only corruption and dropped ACKs matter)

## 4. Evaluation

### 4.1 Methodology

**Harness** (`eval_main.c`):
- Generates fixed 4096-byte input file (seed=42 for reproducibility)
- For each (mode, window_bits, probability, trial) combo:
  1. Reset channel stats and timer stats
  2. Configure channel with (loss_prob, corruption_prob)
  3. Create Unix-domain socketpair
  4. Spawn receiver thread, then sender thread
  5. Measure wall-clock elapsed time (from thread spawn to join)
  6. Verify output matches input (byte-for-byte memcmp)
  7. Write CSV row with trial results
- Per-trial cleanup: delete output file, close sockets
- Sequential trials: threads for this trial join before next trial begins (no cross-trial contamination)

**Sweep parameters**:
- **Modes**: SW (Sw=1), GBN (Sw=15), SR (Sw=8)
- **Probabilities**: 0.0, 0.1, 0.2, 0.3, 0.4, 0.5 (loss + corruption)
- **Trials per combo**: 5 (default; configurable via argv[1])
- **Total**: 3 × 6 × 5 = 90 trials

**Metrics**:
- **Throughput** (bps) = `(file_size × 8) / elapsed`
- **Goodput efficiency** = `file_size / ((frames_sent + retrans) × 65)`
  - Useful bytes / total bytes transmitted; captures retransmission overhead
- **Correctness** = 1 if output ≡ input, 0 otherwise

### 4.2 Results Reporting

Raw results → `eval/EVAL_RESULTS.csv` (one row per trial, 90+ rows for a full sweep).

Analysis → `eval/EVAL_INSIGHTS.md` (aggregate statistics and trends, written separately after CSV collection).

## 5. Implementation Notes

### 5.1 Bug Fixes (Phase 6 Prerequisites)

Before evaluation was possible, several bugs were fixed:

1. **End-of-Transmission (EOT)**: No sender transmitted a zero-length terminator frame, so receivers would block indefinitely after the last real chunk. Fixed: all three modes now send EOT frame (chunk_len=0) after main loop and drain remaining ACKs.

2. **GBN Timeout Retransmit**: GBN's timeout handler only printed a message; it never resent the window. Fixed: timeout handler now resends [Sf, Sn) and restarts timer.

3. **Window Drain**: GBN/SR loops exited as soon as `offset >= file_size`, leaving unacknowledged frames. Fixed: added post-loop drain code (`while (Sf < Sn) { wait_step() }`) for both modes.

4. **SR Window Size**: `receiver_sr_mode` hardcoded window_size=16 instead of threading `arq_config_t->window_size`. Fixed: added parameter and use it in `Sw = 2^(window_size-1)`.

5. **Channel Double-Apply**: Loss/corruption were applied twice per frame (in `channel_send` and `channel_recv`), making effective probability `1-(1-p)^2`. Fixed: moved all impairment to `channel_send`; `channel_recv` is now a plain passthrough.

6. **EOT Check Timing**: EOT check fired on any frame received, including out-of-order/corrupted frames that were discarded. Fixed: moved check to fire only on frames actually delivered to the application.

### 5.2 Known Simplifications

- **No adaptive RTO**: Fixed 100ms; production would use Jacobson/Karels variance tracking
- **No per-frame latency instrumentation**: `timer_record_rtt()` exists in timer.c but is unused; evaluation measures aggregate latency end-to-end
- **No acknowledgment delays**: ACK sent immediately; real networks might batch or use delayed ACKs
- **Single global channel/timer state**: Thread-safe via mutex on channel_stats increment; timers shared (per-process, not per-connection)

## 6. Test Suite Coverage

- **arq_test.c** (15 tests): Frame structure, FCS computation, byte order, sequence toggle
- **channel_test.c** (7 tests): Empirical loss/corruption/delay rates match configured values within tolerance
- **timer_test.c** (11 tests): Window timer lifecycle, per-frame timer expiry, earliest-deadline logic

All three suites pass with `make test`.

Missing (out of scope for Phase 6):
- `window_test.c`: Forced sequence number wraparound, mode-switch correctness
- `frame_test.c`: Protocol-level integration tests (instead, full evaluation harness covers this)

## 7. Conclusion

The three ARQ implementations provide a spectrum of trade-offs:
- **Stop-and-Wait**: Simplest, slowest; useful as reference
- **Go-Back-N**: Practical balance; moderate window, simple receiver
- **Selective Repeat**: Best efficiency under loss; per-frame complexity justified by smaller window size for same performance

Evaluation results (aggregate in `eval/EVAL_INSIGHTS.md`) show how these trade-offs manifest across error rates 0.0–0.5.
