# Lab 2: ARQ Protocols — Quick Start

## Building

```bash
make            # Build unit tests
make eval       # Build evaluation harness
make test       # Run unit tests
make run-eval   # Run full evaluation sweep (takes ~10-30 minutes for 90 trials)
```

## Evaluation Harness

```bash
./eval/eval_main              # Run 5 trials per mode/probability combo (default)
./eval/eval_main 10           # Run 10 trials per combo (90 trials total → ~30 mins)
./eval/eval_main 1            # Quick sanity check (18 trials → ~2 mins)
```

Outputs to `eval/EVAL_RESULTS.csv`:
- **mode**: ARQ mode (SW=Stop-and-Wait, GBN=Go-Back-N, SR=Selective Repeat)
- **window_bits**: m where window size is 2^m (SW=1, GBN=4, SR=4)
- **probability**: loss/corruption rate (0.0 to 0.5)
- **trial**: trial number (1 to N)
- **elapsed_sec**: wall-clock runtime
- **throughput_bps**: application-level bits per second
- **frames_sent**: initial transmissions
- **frames_retransmitted**: timeout retransmits
- **acks_received**: valid ACKs processed
- **corrupted_count**: corrupted frames detected (FCS mismatch)
- **channel_frames_lost**: frames dropped by simulated channel
- **channel_frames_corrupted**: frames bit-flipped by simulated channel
- **goodput_efficiency**: useful_bytes / (total_frames × frame_size)
- **correct**: 1 if output matches input, 0 otherwise

## Architecture at a Glance

```
utils/ (TCP/file I/O)
    ↓
channel/ (loss/corruption/delay simulation)
    ↓
timer/ (RTO management)
    ↓
arq/ {sender, receiver} ← dispatched by mode (ARQ_SW, ARQ_GBN, ARQ_SR)
```

**Key structures**:
- `arq_config_t`: mode, window_size, socket_fd, filename
- `arq_stats_t`: frames_sent, frames_retransmitted, acks_received, corrupted_count, total_time
- `channel_config`: loss_prob, corruption_prob, delay_ms

## Protocol Modes

| Mode | Window Formula | Receiver Behavior |
|------|---|---|
| **Stop-and-Wait** | Sw=1 | Expects in-order, sends ACK per frame |
| **Go-Back-N** | Sw=2^m-1 (m=4→15) | Discards out-of-order, sends cumulative ACK |
| **Selective Repeat** | Sw=2^(m-1) (m=4→8) | Buffers out-of-order, sends per-frame ACK |

All modes use:
- **16-bit sequence space** (full, no wraparound constraints for test sizes)
- **Fixed 100ms RTO** (retransmission timeout)
- **Cumulative/per-frame ACKs** (protocol-dependent)
- **NAK support** (SR only, for immediate resend on corruption)

## Test Suite

- **arq_test.c** (15 tests): Frame structure, FCS, byte order, sequence toggle
- **channel_test.c** (7 tests): Empirical loss/corruption/delay rates
- **timer_test.c** (11 tests): Window timer, per-frame timer, timeout logic

All pass with `make test`.

## Report

Architecture details, protocol descriptions, and evaluation insights are in:
- `REPORT.md` (full write-up)
- `eval/EVAL_INSIGHTS.md` (aggregate analysis of CSV results, written after runs)

## Frame Format

All modes use the same 65-byte frame (inherited from Lab 1):
```
[HEADER 17B][PAYLOAD 44B][TRAILER 4B]
  ↓
  frame_type (byte 0): 0=DATA, 1=ACK, 2=NAK
  frame_seq[2]: sequence number (16-bit, network order)
  frame_len[2]: payload bytes used (0 = end-of-transmission)
  frame_src/dst[6 each]: unused/arbitrary
  ↓
  Payload: 44 bytes (user data for DATA frames; zeroed for ACK/NAK)
  ↓
  FCS[4]: CRC-16 over header+payload (network order)
```

Computed using `SCHEME_CHECKSUM16` from lab1/scheme.c.
