# Linux F2 FD transport foundation — L7b-2b-1

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** Accepted predecessor E2 is Studio
`fd18decf6b509254fe104fa7b280cf02765f2296`; its live firmware and reference-wire
evidence remains in the [E2 record](x1-f2-interoperability.md). Firmware authority
remains the accepted UART v1.0 / ERR-001 contract at
`92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8`. No firmware is modified.

## Implementation and ownership

`plugins/first-party/devices/x1/f2-posix/fd-transport.{hpp,cpp}` is a separate
Linux-only private module. `tests/pty/x1-f2` builds it only under BUILD_TESTING
on Linux. It is uninstalled and unlinked from the X1 production plugin, daemon,
projected fixture and portable Host/codec. No new activation, device selector,
path-based opening, termios, pinmux or baud policy exists in the module.

Constructor entry transfers ownership of an already-open duplex byte FD,
including constructor failure. It adds O_NONBLOCK and FD_CLOEXEC. Ownership
requires exclusive I/O and flag control; callers must not mutate shared open-file
description flags through aliases after ownership transfer. Only tests
create `/dev/pts/` endpoints and set raw terminal mode (VMIN=1, VTIME=0). These
terminal assumptions are a caller prerequisite, not hidden transport policy.
The transport adds one eventfd and one joined I/O worker. The worker exclusively
owns FD I/O and the existing Studio incremental Parser. Host continues to own
session, request/retry, controller correlation, heartbeat and execution fencing.
Studio daemon authority, publications, recording and replay are unchanged.

One queue/state mutex protects admission, offsets, fixed rings, response slots,
cancellation epochs and diagnostics. poll, reads, condition waits and joins do
not hold it. A single nonblocking write of at most 163 bytes holds it to establish
the first-byte admission boundary with STOP. No lock is held waiting for line or
peer progress. A lifecycle mutex serializes close/join; worker and Host operations
never acquire it. No detached thread, callback, image buffer or image copy exists.

Caller owns the transport object and must quiesce Host and its heartbeat worker
before destruction. Concurrent close with pending consumers is supported; object
destruction while callers still access it is not. close is idempotent, wakes all
consumers, joins worker, then closes supplied FD and eventfd exactly once. Linux
close is not retried on EINTR, avoiding accidental closure of a reused descriptor.

## Scheduling, framing and bounds

| Storage | Capacity / behavior |
| --- | --- |
| Ordinary TX ring | Eight complete Wire values; prompt unavailable on saturation |
| Current TX | One frame and offset; at most 163 bytes |
| STOP TX | One reserved pending slot; priority at frame boundary |
| KEEPALIVE TX | One reserved pending slot; before queued ordinary work |
| Ordinary responses / events | Eight frames each, separate rings |
| STOP / KEEPALIVE responses | One dedicated slot each; identical duplicates coalesce, conflicting overflow fails explicitly |
| Read scratch / parser | 1,024 bytes / existing bounded 161-byte COBS collector |
| Diagnostics | Fixed numeric fields, saturating uint64 counters; no raw history/log queue |

submit validates the complete delimited F2a envelope, CRC and REQUEST direction.
STOP and KEEPALIVE must have their frozen special shapes. Unknown and EVENT-only
IDs submitted as REQUEST remain opaque and transmissible. Accepted bytes are
preserved exactly. Successful submit means local bounded admission only.
The worker handles short I/O, EINTR, EAGAIN/EWOULDBLOCK, poll readiness, HUP/ERR/NVAL
and EOF. A continuous 64-interruption I/O failure is reported rather than retried
without bound. Reads are limited to one 1,024-byte batch per scheduling turn.

STOP admission cancels queued ordinary and heartbeat frames, including a selected
frame whose first byte has not been written. New ordinary/heartbeat admissions
are refused while STOP is pending/transmitting. A partly written frame finishes
without interleaving, followed by STOP before any remaining traffic. This bounds
the serialization dependency to the remaining bytes of one frame; it does not
bound kernel/peer progress. Congestion or disconnect can prevent delivery.
Already transmitted kernel bytes cannot be withdrawn. The existing Host fence
prevents old callers from creating fresh enabling work after cancellation.

STOP transmission and reception do not require ordinary reads, heartbeat waits
or event capacity. Queued stale heartbeat work is canceled by STOP. Heartbeat
admission/response uses separate storage; controller lease meaning remains in
Host/controller, never in the I/O scheduler.

RX bytes pass through the unchanged Parser. Arbitrary fragments, adjacent frames,
boot noise, bad CRC/COBS/version/header and oversize spans retain accepted rejection
and delimiter recovery. A poll timeout is armed only while a partial frame needs
the accepted 50 ms idle expiration; otherwise idle worker waits indefinitely.
The worker transfers Parser counters into saturating diagnostics after each byte.
Typed validation stays in Host, including ERR-001's narrow unknown negative reply
exception. receive matches class/message/session/request exactly; unrelated replies
are discarded with a diagnostic. EVENTS have separate reception.

Response/event overflow latches unavailable for that consumer lane: existing
buffered records never become a falsely complete result after known loss. Ordinary
admissions fail closed after ordinary RX loss; fresh transport is required.
STOP/heartbeat remain independent. No authoritative slot is silently overwritten.

## Deadlines, interruption and disconnect

receive/event use total steady-clock deadlines; expired deadlines poll queued state
without waiting. Timeout means no matching reply was established, not nonexecution.
interrupt wakes ordinary/heartbeat/event waits and records per-lane epochs, including
the submit-to-read registration window. An interrupt preceding a fresh successful
submit does not poison a new transaction. Event reads consume a pending interrupt
once. STOP reads ignore interruption. Cancellation is checked before queued success.

Unexpected HUP, ERR, NVAL, EOF or I/O failure latches unavailable, clears pending
TX/RX opportunities and wakes all consumers. Local close returns closed. Neither
path fabricates STOP acknowledgement. There is no reconnect: a fresh object has
empty buffers and a fresh Host requires discovery/session/configuration/ARM again.

### R1 — asynchronous transaction abandonment

Independent review of `0c771627f9c42f396584c5d46b52658a6c9ec44a` identified
L2B1-REV-001: logical Host fencing alone did not remove previously admitted FD
frames. An abandoned RUN or CLAIM could still start transmission after Host had
reported failure. That commit's five remote jobs passed; this correctness correction
keeps L7b-2b-1 IMPLEMENTED / AWAITING INDEPENDENT REVIEW.

Host now dispatches priority containment STOP before returning an ambiguous
enabling-transaction failure. An ambiguous ordinary reconciliation failure during
execution also contains its authority, including queued heartbeat renewals.
Retryable receive timeouts retain the same session,
request ID and complete wire; only final abandonment fences authority and cancels
unstarted TX through STOP. The private Transport contract makes this STOP admission
requirement explicit, using the existing submit/receive/event/interrupt methods.
No interface method, public ABI or wire format changes. The existing FD cancellation
and partial-frame scheduler remain unchanged; synchronous byte transports have no
unstarted TX backlog to withdraw.

Host's control mutex orders generation inhibition, heartbeat deactivation and
bounded STOP submission against other Host dispatch. Transport's queue mutex
orders that submission against its first kernel byte. Lock order is Host control
then transport queue; the I/O worker never acquires Host locks. No response wait,
queue drain or kernel progress wait occurs under either dispatch gate. Enabling
ACK admission rechecks the generation fence after receive, including cancellation
after a validated ACK arrives but before Host admits it.

Local admission, first accepted kernel byte, complete kernel frame submission,
controller ACK and controller execution remain separate facts. A started frame
cannot be withdrawn: it finishes, followed by STOP before queued retries. Its
controller outcome remains ambiguous until actual controller evidence is received.
If the line remains blocked or disconnects, STOP delivery is not established.
Containment does not consume the STOP response slot, avoiding interference with a
concurrent explicit cleanup, and does not extend the failed call's deadline.
The original exception remains primary; stop_error reports TIMEOUT while the
containment ACK is unestablished, UNAVAILABLE on failed submission, or a separate
cleanup error. Explicit stop/abort can subsequently establish a matching software
ACK. Old cleanup completion cannot overwrite a newer generation's diagnostic.
None of these facts establishes electrical/optical OFF.

Start now contains failures throughout CLAIM/CONFIGURE/ARM/RUN. An unconfirmed
CLAIM clears the local session/ARM/execution association and invalidates ordinary
sequencing; its last attempted session token remains separately retained so a
fresh attempt cannot silently reuse it. Boot identity is retained and the CLAIM
ACK must echo the expected boot. An unstarted CLAIM is canceled; a partial CLAIM
may reach the controller before STOP and leave an inhibited MCU-local session.
That uncertainty does not grant local authority: recovery requires fresh
discovery and a fresh CLAIM, followed by CONFIGURE/ARM. Confirmed execution
identities remain available for causal terminal evidence. Normal heartbeat lease
expiry policy and exact consumed rejection/request-ID semantics remain unchanged.

The new `x1-f2-pty-abandonment` test uses the actual Host, FD transport, PTY and
accepted simulation controller. Per-instance one-shot byte barriers select CLAIM
or RUN, without callbacks or physical activation. Receive barriers wrap genuine
transport outcomes and release before teardown.

| R1 tests | Evidence |
| --- | --- |
| T01 | Three queued RUN attempts exhausted; original TIMEOUT, all unstarted retries canceled, no MCU execution after barrier release |
| T02 | Three bytes of RUN committed; exactly one complete RUN followed by STOP, no interleaving/retry execution, exact stopped terminal identities and zero completed pulses after later clock advance |
| T03 | Queued and partial CLAIM timeouts; no confirmed local session, no late queued claim; explicit fresh-session recovery and wrong-boot CLAIM ACK rejection |
| T04 | Barriers before receive registration and after actual CLAIM/ARM/RUN ACK receipt; external STOP independently completes, failed start remains fenced, no duplicate containment or post-fence RUN |
| T05 | Pending STOP under a held write barrier, actual PTY HUP, and distinct controller cleanup failure; initiating TIMEOUT preserved and uncertainty/error reported separately |
| T06 | Existing discovery, retry deduplication, heartbeat, priority, terminal recovery, F2/E1/E2, daemon artifacts and replay regressions retained; failed ordinary reconciliation also cancels queued heartbeat authority |

```bash
cmake --build build/l2 --target mantis-x1-f2-pty-abandonment-tests --parallel 4
ctest --test-dir build/l2 -R '^x1-f2-pty-abandonment$' --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-pty(-abandonment)?$' \
  --repeat until-fail:100 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-f2-pty(-abandonment)?$' \
  --repeat until-fail:100 --output-on-failure
```

R1 adds one retained uint64 session token in Host and fixed fields only in the
compile-time FD test seam. It adds no queue, worker, hot-path allocation or image
copy. The original package's measured results below remain historical; R1 final
verification and refreshed measurements are recorded separately below.

## PTY verification

The joined test peer receives actual PTY bytes through an independent Parser and
the unchanged accepted Link framing ingress. Link owns simulated controller
semantics; its responses/events serialize to bytes, fragment across the PTY and
pass through the transport's independent Parser before Host sees them. Synthetic
manual time, loss and known-response corruption use existing test-only mechanisms.
No native command invocation bypasses the byte stream.

A private per-instance compile-time test seam limits actual syscall chunk sizes,
injects EINTR/EAGAIN and pauses writes at explicit barriers. It has no arbitrary
callback, global injection state or physical selector. Real PTY tests remain active.
The real backpressure regression also fills kernel PTY buffering until EAGAIN or
suppressed POLLOUT, then verifies honest pending STOP failure on disconnect.

| Tests | Coverage |
| --- | --- |
| T01–T04 | Owned PTY FD/raw assumptions, exact framed requests/responses, byte/irregular fragmentation, adjacent frames and maximum 163-byte envelope |
| T05–T09 | Noise/CRC/COBS/oversize/idle recovery, correlation/direction, deadline polling, actual Host strict known replies and unknown/EVENT-only REQUESTs |
| T10–T11 | Actual Host discovery/CLAIM/CONFIGURE/ARM/RUN, terminal/event and lost-event lookup, exact lost-ACK retransmission with one execution |
| T12–T16 | STOP cancellation of queued RUN/heartbeat, partial-write fence, blocked ordinary Host receive, independent heartbeat, saturated TX/RX/event rings and counters |
| T17–T21 | Deadline boundaries, before/registered/arrived cancellation barriers, STOP exception, peer loss, concurrent idempotent close, descriptor counts and fresh lifetimes |
| T22–T23 | Repeated concurrency/resource checks and reproducible throughput/latency/allocation/idle measurements |
| T24 | Existing full F2/E1/E2/L7a/L3–L6, daemon artifacts/replay, ABI/catalog and storage goldens |

### Reproduction

Reuse the [E2 configurations/dependencies](x1-f2-interoperability.md#reproduction-and-validation).
The new PTY targets need no firmware checkout, root privileges or physical device.

```bash
cmake --build build/l2 --target mantis-x1-f2-pty-tests mantis-x1-f2-pty-performance --parallel 4
ctest --test-dir build/l2 -R '^x1-f2-(pty|posix-boundaries)' --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-pty$' --repeat until-fail:100 --output-on-failure
build/l2/bin/mantis-x1-f2-pty-performance
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-f2-pty$' --repeat until-fail:100 --output-on-failure
```

### Initial package local validation — 2026-10-11

| Configuration/check | Result |
| --- | --- |
| Linux x86_64 Studio ON, firmware access disabled | Complete configure/build; 134/134 CTests PASS |
| Linux x86_64 Studio OFF, exact-SHA optional E2 live enabled | Complete configure/build; 132/132 CTests PASS |
| Linux x86_64 ASan/UBSan, Studio OFF, E2 live enabled | Complete configure/build; 132/132 CTests PASS, fatal errors and leak detection enabled |
| BUILD_TESTING=OFF, absent firmware path | Complete build PASS; zero tests and no FD/PTY/simulator/interoperability target |
| Normal / sanitized PTY concurrency suite | 100 repetitions each PASS, including repeated FD ownership and shutdown cycles |
| Frozen ABI/catalog, protocol/storage goldens, source/binary boundaries | PASS |
| C++ formatting, whitespace, local documentation links | PASS |
| Linux implementation without test instrumentation macro | GCC C++23 -Wall/-Wextra/-Werror syntax-only compile PASS |

The full suites retain E1/E2, L7a, sequencer, daemon/client recording, recovery,
replay, calibration, camera pairing and observation/processing regressions.
No assertion or existing test was removed or weakened. Real PTY POLLOUT can cease
before write returns EAGAIN: the kernel congestion test admits either observable
blocked condition, while the separate deterministic syscall test requires EAGAIN.

Final configurations reuse the documented E2 dependencies/loader setup:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/l2-deps/opencv" -DMANTIS_F2_LIVE_INTEROP=OFF \
  -DMANTIS_F2_FIRMWARE_SOURCE_DIR=/missing/firmware
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --parallel 4
cmake --build build/l2 --parallel 4
ctest --test-dir build/l2 --output-on-failure --parallel 4
cmake --build build/l2-sanitized --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized --output-on-failure --parallel 4
cmake --build build/l7b2a-no-tests --parallel 4
cmake --build build/l7b2a-no-tests --target help
ctest --test-dir build/l7b2a-no-tests -N
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py
c++ -std=c++23 -Wall -Wextra -Werror -fsyntax-only \
  plugins/first-party/devices/x1/f2-posix/fd-transport.cpp
git diff --check
```

### Measured resources and software delivery

Linux 6.17.0-41-generic x86_64, GCC 15.2.0, Debug build, real raw PTYs, unprivileged
user. Benchmark runs 1,000 sequential maximum REQUEST / four-byte negative RESPONSE
roundtrips. The peer uses actual PTY reads/writes and framing, without controller
execution. Durations use steady_clock; idle worker CPU uses its pthread CPU clock.

| Measurement | Observed value |
| --- | --- |
| Instrumented FdTransport size | 5,776 bytes, fixed inline queue/parser/diagnostic storage |
| Worker startup C++ allocations | 1 |
| Representative steady-state C++ allocations | 0 across all 1,000 roundtrips, including aligned new interception |
| Roundtrip p50 / p95 / p99 | 83.435 / 114.923 / 134.319 µs |
| Combined wire throughput | 2,234,030 bytes/s |
| Benchmark TX / response / event high-water | 1 / 1 / 0; saturation regressions reach 8 / 8 / 8 |
| Idle observation | 100.116 ms, zero worker wakes, zero measured worker CPU ns |
| Congested STOP admission / complete wire delivery | 2.094 / 46.296 µs after canceling unstarted queued work |

Object size includes the private test seam. C++ allocation interception excludes
libc-internal allocation, kernel PTY buffers and the OS thread stack; one worker and
one wake eventfd are bounded per instance. Descriptor-count regressions verify
return to baseline after repeated lifetime/closure cycles; sanitizer leak detection
also passes. No image-buffer copies are introduced. These reproducible observations
have no machine-specific throughput/latency acceptance threshold; test deadlines
distinguish finite progress from a hang. They do not measure a real UART, baud rate,
hardware trigger, laser behavior, safety latency or hard real-time performance.

Native ARM64 remains mandatory in the remote matrix; no local ARM64 toolchain is
available. No platform-specific integer width or x86 instruction is assumed.

### R1 final validation — 2026-10-11

Using the same configure/build commands, cached dependencies, loader environment
and sanitizer settings recorded above:

| Check | Result |
| --- | --- |
| Complete Linux x86_64 Studio ON configure/build/CTest | 135/135 PASS, optional firmware dependency disabled |
| Complete Linux x86_64 Studio OFF configure/build/CTest | 133/133 PASS, exact-SHA E2 live enabled |
| Complete ASan/UBSan configure/build/CTest | 133/133 PASS with detect_leaks=1, halt_on_error=1 for ASan and UBSan |
| PTY and abandonment repetitions | 100 repetitions of each suite, both normal and sanitized: all PASS |
| BUILD_TESTING=OFF configure/build | PASS, absent firmware source path; zero tests and no FD/simulation/interop targets |
| Frozen ABI/catalog, protocol/storage/replay, source/binary boundaries | PASS |
| Formatting, whitespace and local documentation links | PASS |

Refreshed 1,000-roundtrip measurement on the same x86_64/GCC Debug PTY setup:
instrumented FD transport 5,792 bytes (16 additional test-only barrier bytes);
Host 376 bytes, including the added eight-byte retained session token. One worker
startup C++ allocation and zero representative steady-state C++ allocations;
queue capacities remain 8/8/8 and measured high-water 1/1/0. Combined throughput
2,205,940 bytes/s; roundtrip p50/p95/p99 82.123/115.996/134.150 µs. Idle observation
100.130 ms with zero worker wakes and zero measured worker CPU ns. Congested
STOP enqueue/complete-wire delivery 3.857/64.190 µs. Allocation exclusions and
software-only timing limitations remain those stated above. No new resource
growth or image copies; the uninstrumented FD module also compiles without the
test seam. Native ARM64 remains a remote acceptance gate.

## Independent physical gates

L7b-2b-2 is PLANNED / NOT STARTED. L7b-2b-3 is PLANNED / BLOCKED BY PHYSICAL SAFETY
GATES. The [existing hardware readiness facts](x1-projected-light-integration.md#physical-controller-readiness-facts)
remain authoritative and unchanged. No physical UART/USB/GPIO, Q6A pinmux, ESP8266
access/flashing, laser output, current/power control, hardware trigger, commissioning
or deployment is performed or enabled. PTY STOP is a software request; local write
or ACK is not measured electrical/optical OFF. A disconnected UART does not establish
physical inhibition. Controller runtime acceptance, independent watchdog/interlock
qualification and physical timing/output evidence remain separate gates.
