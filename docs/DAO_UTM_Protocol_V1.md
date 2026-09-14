# DAO Windows ↔ Linux Protocol V1

Linux is the TCP server and remains the owner of control, sequencing, safety, and local test recording. The default listening port is **45550**. One monitoring client is supported. Loss of that client never stops a test in V1.

## Framing

All integers and IEEE-754 binary64 values use network byte order (big endian). C/C++ object memory is never transmitted directly.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `DAUT` (`0x44415554`) |
| 4 | 2 | Protocol version (`1`) |
| 6 | 2 | Message type |
| 8 | 4 | Flags (reserved, zero in V1) |
| 12 | 4 | Payload length, maximum 1 MiB |
| 16 | 4 | Sequence number |
| 20 | 4 | Request ID |

The decoder retains incomplete TCP data and emits zero or more complete frames. Invalid magic/length closes the connection. Version mismatch is rejected during HELLO. Unknown request types receive `COMMAND_NACK/UNSUPPORTED_MESSAGE`; unknown response/event types may be ignored by a client.

## Message IDs

| ID | Name | ID | Name |
|---:|---|---:|---|
| 1 | HELLO | 2 | HELLO_ACK |
| 3 | HEARTBEAT | 10 | MACHINE_STATUS |
| 11 | LIVE_DATA | 20 | LOAD_RECIPE |
| 21 | COMMIT_RECIPE | 22 | START_TEST |
| 23 | STOP_TEST | 24 | ACK_RESET |
| 30 | COMMAND_ACK | 31 | COMMAND_NACK |
| 40 | TEST_STARTED | 41 | STEP_CHANGED |
| 42 | TEST_COMPLETE | 43 | TEST_ABORTED |
| 44 | FAULT_EVENT | 50 | GET_TEST_DATA |
| 51 | TEST_DATA_BEGIN | 52 | TEST_DATA_CHUNK |
| 53 | TEST_DATA_END | | |

`LIVE_DATA` is published every 100 ms (10 Hz); events are queued immediately. Physical units are N, mm, mm/min, and monotonic microseconds. LIVE_DATA retains force, raw displacement, compliance compensation, corrected displacement, extensometer, machine/sequence state, step, running state, and sample flags.

Commands use request → validation → ACK/NACK with the original request ID. Recipe transfer maps to the existing Linux load → validate → pending → commit flow; the committed sequence is the immutable active snapshot. Data retrieval uses BEGIN → one or more CHUNK → END. Chunk bytes are an explicit bulk format and never native structs.

The server thread owns all socket I/O. Control-side publication is a fixed-size double-buffer snapshot; no socket, file, JSON, or heap-heavy serialization is performed in the 2 ms control loop.

## Compliance metadata for test results

Result metadata must carry test ID, start/end time, sample rate, profile name/version, force calibration scale metadata, compliance enabled state, curve version, and either its canonical `(forceN,deformationMm)` points or a stable hash. Samples retain raw and corrected displacement. This additive internal contract is represented by `UtmAnalysisSample`; the existing engine ABI is unchanged.
