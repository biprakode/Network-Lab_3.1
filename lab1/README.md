# Network Frame Transmission with Error Detection

A complete implementation of network frame transmission with multiple error detection schemes (checksum and CRC variants).

## Building the Project

```bash
# Compile all binaries
make

# Compile sender and receiver only
make sender receiver

# Run all tests
make test

# Clean build artifacts
make clean
```

## Running the System

### Terminal 1: Start the Receiver
```bash
./receiver output.txt <scheme>
```

### Terminal 2: Send Data
```bash
./sender data.txt <scheme>
```

### Supported Schemes
- `checksum` — 16-bit Internet Checksum (RFC 1071)
- `crc8`     — 8-bit Cyclic Redundancy Check
- `crc10`    — 10-bit Cyclic Redundancy Check
- `crc16`    — 16-bit Cyclic Redundancy Check
- `crc32`    — 32-bit Cyclic Redundancy Check

## Example Usage

### Terminal 1: Receiver (wait for connection)
```bash
$ ./receiver output.txt crc32
```

### Terminal 2: Sender (connect and transmit)
```bash
$ ./sender data.txt crc32
sent seq=0 len=44 fcs=0x57dcba0b
sent seq=1 len=44 fcs=0x12345678
... (more frames)
```

### Terminal 1: Receiver output
```
seq=0 ACCEPTED len=44
seq=1 ACCEPTED len=44
... (all frames accepted)
sender closed connection - done
summary: accepted=5 rejected=0
```

### Verify Transfer
```bash
$ diff data.txt output.txt
# No output means files are identical ✓
```

## Protocol Details

### Frame Structure
```
[HEADER 16B] [PAYLOAD 44B] [TRAILER 4B] = 64 bytes total

Header:
  - Source MAC: 6 bytes
  - Dest MAC: 6 bytes  
  - Frame length: 2 bytes (in network byte order)
  - Sequence number: 2 bytes (in network byte order)

Payload:
  - User data: up to 44 bytes

Trailer:
  - FCS (Frame Check Sequence): 4 bytes (in network byte order)
```

### Transmission Flow

1. **Sender reads input file** (`data.txt`)
2. **Chunks data into 44-byte payloads** (last frame may be smaller)
3. **Builds frames** with header, payload, and sequence number
4. **Computes FCS** over (header + payload)
5. **Sends frame** to receiver on localhost:8080
6. **Receiver listens** for incoming frames
7. **Receiver verifies FCS** against computed FCS
8. **Writes verified payloads** to output file (`output.txt`)
9. **Reports statistics**: frames accepted/rejected

## Error Detection Capabilities

### 16-bit Checksum
- Detects all single-bit errors
- Detects all two-bit errors in different words
- May miss some multi-bit errors
- Fast, minimal overhead

### CRC-8/10/16/32
- Detects all single-bit errors
- Detects all burst errors up to polynomial degree
- Polynomial-specific error patterns
- Stronger guarantee than checksum
- Degree determines coverage: CRC-32 > CRC-16 > CRC-10 > CRC-8

## Test Suite

Comprehensive test coverage in `tests/`:

| Test File | Coverage |
|-----------|----------|
| `error_test.c` | Integration tests (checksum + all CRC variants) |
| `checksum_test.c` | 10 detailed checksum tests (edge cases, patterns, bit flips) |
| `crc_test.c` | 8 CRC variant tests (empty data, standard vectors, uniqueness) |
| `scheme_test.c` | 8 dispatcher tests (parsing, independence, tampering) |
| `boundary_test.c` | 10 boundary tests (exhaustive Hamming distance) |

Run tests:
```bash
make test
```

## Known Limitations

- Receiver hardcoded to listen on `127.0.0.1:8080`
- Only supports TCP (no packet loss simulation)
- No flow control or congestion handling
- Each frame uses the same scheme (configured at startup)
- FCS includes frame header (for payload integrity + header integrity)

## Implementation Details

### Files Modified/Created

**Core Implementation:**
- `error.c` / `error.h` — Checksum and CRC algorithms
- `scheme.c` / `scheme.h` — Dispatcher for multiple schemes
- `frame.h` — Frame structure definitions
- `sender.c` — Transmitter program
- `receiver.c` — Receiver program
- `utils/utils.c` — Network I/O utilities

**Tests:**
- `tests/error_test.c` — Main integration tests
- `tests/checksum_test.c` — Checksum-specific tests
- `tests/crc_test.c` — CRC-specific tests
- `tests/scheme_test.c` — Dispatcher tests
- `tests/boundary_test.c` — Edge case and boundary tests

**Build:**
- `Makefile` — Build rules for all targets

## Troubleshooting

### "Connection refused" error
- Make sure receiver is started first in Terminal 1
- Allow 1 second for receiver to bind to port
- Check that port 8080 is not in use: `lsof -i :8080`

### "Failed to connect" error
- Receiver crashed or not running
- Try starting receiver again before sender

### Files don't match after transfer
- This should not happen if FCS verification passes
- If rejected frames occur, it indicates data corruption
- Run tests to verify error detection is working

### All frames rejected
- Verify both sender/receiver use the same scheme
- Check that `data.txt` exists and is readable
- Verify frame structure matches expectations

## Performance Notes

- Checksum: Fastest, minimal overhead (~0.1% of data)
- CRC-8/10: Fast, low overhead
- CRC-16: Medium speed, standard choice
- CRC-32: Slight overhead, best error detection

For most applications, **CRC-32** is recommended for best error detection.
