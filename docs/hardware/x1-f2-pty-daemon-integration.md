# F2 PTY daemon integration — L7b-2b-3a

**FINAL ACCEPTED** at `35f4b9845c79d448c7e7295ed129d063fc1b5327`; all five required jobs SUCCESS in
[run 38162764432](https://github.com/martinkoenig/mantis-studio/actions/runs/38162764432). L7b-2b-1 is FINAL ACCEPTED
at `6489c6eec05ac61f685f71df5805cc825a912b2d`. L7b-2b-2 is FINAL ACCEPTED
at `9b6f0cc72f44134009302abe09175b7158b27f38`, with all five required CI jobs
SUCCESS. L7b-2b-3b remains **BLOCKED — F1b / F5 / hardware authorization**.

No physical UART communication occurred. No ESP8266 hardware was accessed.
No Q6A hardware was accessed. No laser emission was performed or enabled.
This PTY fixture is not a deployable physical controller backend. Runtime DT,
electrical polarity, wiring, boot/disconnect behavior, independent inhibit and
optical/electrical timing remain separate qualification facts in the
[readiness checklist](x1-projected-light-integration.md#physical-controller-readiness-facts)
and [Q6A evidence handoff](x1-q6a-uart-readiness.md). Firmware acceptance does not
qualify these facts. No firmware source is changed or needed by this composition.

## Composition and activation

The existing `tests/fixtures/x1-f2/plugin.cpp` is compiled a second time with
`MANTIS_X1_F2_PTY_FIXTURE`. The original in-memory DSO and its activation behavior
remain available. The new DSO is uninstalled, under `BUILD_TESTING` and Linux
only, in `build/<configuration>/x1-f2-pty-plugins`. Its manifest retains
`org.mantis.x1`; select this directory explicitly instead of combining it with
the production or in-memory plugin directory.

All activation conditions are required:

- Dedicated `--plugins build/<configuration>/x1-f2-pty-plugins` selection.
- `MANTIS_X1_F2_SIMULATION=1` and `MANTIS_X1_F2_PTY_FIXTURE=1`, exactly.
- Explicit `MANTIS_X1_FAKE=normal` and the synthetic `MANTIS_X1_PROFILE`.
- `MANTIS_X1_F2_CONFIG` containing the existing strict SimulationOnly JSON schema.

Missing or malformed opt-in rejects discovery. The camera backend is compiled
from `fake.cpp`; there is no physical camera fallback. The configuration admits
no serial-device, inherited-FD or physical-controller selector. `Backend()` has
no descriptor/path argument. It allocates its own PTY with `posix_openpt`, opens
only the corresponding newly allocated slave returned by `ptsname_r`, and
applies the accepted `configure_serial()` 115200 8N1 raw/no-flow policy before
transferring the slave to the accepted `FdTransport` exactly once.

The normal camera-only X1 plugin, daemon, SDK, and portable F2 Host do not link
the POSIX transport, serial settings or PTY peer. `BUILD_TESTING=OFF` contains
none of the new targets. Ordinary CI requires no hardware, permissions, private
firmware checkout, or network access. The optional accepted live firmware
interoperability target remains unchanged.

```text
mantisd / L5 projected service / L3 ProjectedRun
    -> existing ProjectedLightV1 fixture (build-time PTY composition)
    -> existing private F2 Host
    -> Backend delegation -> existing FdTransport -> configured PTY slave
    -> kernel PTY byte stream -> owned PTY master -> bounded peer parser
    -> existing Link framed-byte endpoint -> existing synthetic Controller
    -> Link's framed reply/event validation -> peer byte serialization
    -> kernel PTY byte stream -> FdTransport parser/routing -> Host
    -> unchanged evidence / RawCapture-3 / persisted daemon outcome
```

The peer checks its received canonical wire against the actual collected bytes
and submits those bytes unchanged to the existing Link. The controller state
machine is reused; no native Controller operation substitutes for Host traffic.
Responses/events are serialized and pass through the real FD receive parser.
The added read-only `Link::activity_deadline()` exposes only its next finite
controller/delayed-frame deadline for peer scheduling. It does not execute an
operation. Idle workers block indefinitely in `poll`; active deadlines use the
existing real-time simulator clock. Manual-clock Link and PTY tests are retained.

## Ownership, bounds and STOP

The instance owns camera fixture state, Backend, then Host. Reverse destruction
retires Host and its heartbeat worker before Backend. Backend owns peer then FD
transport; FD destruction closes/joins before the peer retires. The peer worker
alone closes the master. Allocation/configuration/construction failures retain
RAII ownership of untransferred descriptors. No detached workers exist.

The peer has one joined worker, one eventfd, its own parser, 16 pending output
frames, one active partial output frame, three pending reply selectors
(ordinary/KEEPALIVE/STOP), and separate one-slot STOP/KEEPALIVE output lanes.
Only identical pending idempotent control ACKs can be coalesced. Differing control
replies or output overflow fail the peer and close its master, exposing HUP.
A fixed 128-byte error buffer and counters provide bounded diagnostics. There is
no growing frame trace, per-byte allocation or image queue.

The existing FD bounds remain ordinary TX/RX/EVENT 8/8/8, active TX 1, and separate
STOP/KEEPALIVE TX/RX slots. STOP admission cancels unsent ordinary and heartbeat
work atomically. An already started frame remains serialized before STOP. The
existing Host R1 containment/fencing is unchanged. A timed-out operation never
allocates a fresh execution ID to recover uncertainty. Queued CLAIM cannot create
reusable late authority. STOP is independent of ordinary waits, events, camera
state, publication callbacks and storage. Local admission or transmission is not
an ACK, electrical inhibition or observed optical OFF.

The synthetic peer's nonblocking read/write worker never waits for a peer under
an instance/control lock. Its short metrics lock is not held over `poll`, I/O,
controller dispatch, callbacks or joins. The existing publication-admission fence
and in-use callback pinning are unchanged. Destroy refuses active callbacks,
retaining camera/backend/Host resources; a bounded retry after retirement works.
There is no automatic reconnect, peer reboot replacement, or authority reuse.
Unexpected HUP, parser/queue failures or injected peer failure propagate transport
unavailability. Primary execution error and cleanup uncertainty remain separate.

## Programs, provenance and artifacts

The same stable fake-profile parent and `/left`, `/right`, `/simulation-f2/L1`,
`/simulation-f2/esp8266` component identities are used. No ID derives from a PTY
path or FD. The one-emitter, one-controller, one-step, repetition-one control-only
program subset and commanded-only requirement are unchanged. No camera capture,
trigger, physical calibration or physical readiness is introduced.

PTY publications use `org.mantis.x1.f2.pty.simulation-only`, version 1, build
`L7b-2b-3a PTY/FdTransport synthetic ESP8266; rig revision 1`. This is the intended
provenance difference from the in-memory fixture. ProgramReference, RunId,
GenerationId, declared step and causal keys retain the accepted schema. Controller
ACKs and counted synthetic pulses do not become electrical/optical/exposure
observations. Those evidence fields stay Unknown/Unavailable as appropriate.

The daemon path records MRUNHDR3, MRAWREC3 bundles and MRUNOUT3 outcomes with no
format changes. Normal ON/OFF success records exactly two zero-camera bundles;
ON completion requires the matching real software terminal result, not RUN ACK.
Failed starts retain durable headers and zero-bundle outcomes. Storage failure
remains RECOVERABLE with no fabricated successful daemon outcome. Shutdown can
leave a verified recoverable prefix while finalization jobs retire. Tests use the
accepted explicit offline recovery operation, preserving the actual user-stop
outcome, before replay. A read-only HELLO wire barrier and exact late-terminal
event comparison precede the idle measurement when lookup wins event delivery.
For the injected recorder obstruction, the test removes
only its own empty obstruction directory and verifies recovery cannot invent an
outcome. This is distinct from a successful live finalization.

Every recorded test capture receives two canonical replay comparisons after the
daemon has exited and all `MANTIS_X1_*` activation variables have been removed.
Replay constructs Store/readers only; its binary contains no PTY backend. Canonical
bundle bytes, immutable header references and terminal outcome identities are
checked. It neither submits RUN nor recomputes evidence.

## Automated evidence

| Assignment tests | Specific observable checks |
| --- | --- |
| T01–T02 | `x1-f2-pty-fixture` rejects missing/wrong opt-ins, profile/config, fake camera and physical selectors; `x1-f2-pty-daemon-boundaries` inspects sources/product/fixture/client symbols; no-test target inventory |
| T03–T05 | Dedicated daemon/client normal and OFF runs: actual discovery/validation/prepare/claim/configure/arm/run/terminal/STOP, two bundles and exact outcomes; direct L2 diagnostics count framed requests and zero/one executions |
| T06–T07 | Daemon lost-ack/lost-event successes; backend test checks two identical RUN requests, one admission, exact execution/boot/session/ARM terminal association and actual pulse accounting |
| T08 | Backend controller-admission barrier blocks RUN reply while explicit heartbeat and STOP complete independently; original PTY blocked-normal-receive and callback-admission barriers retained |
| T09 | Daemon `partial-run`, backend failure test and accepted `x1-f2-pty-abandonment`: three admitted retries, first-byte barrier, timeout fence, no late RUN, software STOP ACK. In real-time daemon tests the 60 ms retry gap expires the 50 ms partial parser window; manual-time tests also prove the ambiguous completed partial frame then STOP ordering |
| T10 | Daemon `congestion` plus accepted `x1-f2-pty` full TX/RX/event saturation assertions: bounded rejection, reserved STOP, canceled pending work and explicit overflow |
| T11–T13 | Daemon disconnect/reboot/stop-ack/lease: failure outcomes, unavailable OFF evidence, initiating/cleanup errors; no session revival. Existing manual-clock controller/transport tests establish exact expiry and stale identity rejection |
| T14–T15 | PTY L2 callback barriers reproduce pre/post-admission abort, blocked callback STOP, repeated abort and destroy BUSY/quiescent retry; daemon cancel/stop/timeout/shutdown while next waits |
| T16–T17 | Dedicated daemon test checks schema-3 durable header, valid zero-image semantic bytes, terminal identity, failure prefixes, post-teardown two-pass exact replay and absent physical observations |
| T18–T19 | Backend 17 repeated Host/backend lifetimes with `/proc/self/fd` and `/proc/self/task` counts; L2 fixture 20 cycles and camera ownership; canonical replay asserts PTY-specific synthetic producer identity |
| T20 | Full local suites retain F2 V1–V13/E1/E2, R01/R02/R1, PTY/serial, L7a/L3–L6, camera, ABI/catalog and storage/protocol goldens |

The new `x1-f2-pty-backend`, `x1-f2-pty-fixture`, `x1-f2-pty-end-to-end`, and
`x1-f2-pty-daemon-boundaries` targets supplement all accepted tests. The daemon
suite runs 22 scenarios, including corrupt RUN ACK (CRC rejection/retry), one-byte
response fragmentation, blocked communication, cleanup error and recorder failure.
No failing scenario is skipped or hidden with a compatibility fallback.

## Reproduction and software measurements

Use the dependencies and presets in [BUILDING.md](../../BUILDING.md).

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/debug --parallel 4
ctest --test-dir build/debug -R '^x1-f2-pty-(backend|fixture|end-to-end|daemon-boundaries)$' --output-on-failure
build/debug/bin/mantis-x1-f2-pty-backend-tests
```

The hardware-free daemon harness supplies its own synthetic profile/configuration,
PTY gate and isolated plugin directory. No operator-supplied serial endpoint exists.
The binding [FD validation record](x1-f2-posix-transport.md) remains authoritative
for transport latency, allocation instrumentation, saturation and wire throughput;
this composition adds no replacement transport or wire encoding.

Representative local x86_64 Debug measurement: Linux 6.17, GCC 15.2, user-level PTYs,
12 finite ON cycles (200 ms configured duration), all replies fragmented to one
byte, four lost-ACK and four lost-event cycles. Discovery p50/p95/max
619/1022/1022 µs; total start-to-terminal p50/p95/max 202835/206713/206713 µs;
Complete STOP control roundtrip including heartbeat join p50/p95/max
186/269/269 µs. These are observed software
samples, not hard real-time bounds or physical measurements. Twelve samples do
not support a separate statistically meaningful p99; the maximum is reported.
Queue high-water TX/RX 1/1; maximum FD polls 2118 during deliberately fragmented
active traffic; 100 ms fully idle FD/peer polls 0/0. Seventeen joined lifetimes
had FD/thread delta 0. An additional 200 construction failures per backend test
exercise peer/FD-worker eventfd failure and immediate descriptor rollback. Workers
are joined before thread-count verification; the test permits only bounded Linux
`/proc` exit bookkeeping before requiring the exact thread baseline.
No new image-buffer copies or image encoding exist.

On this build: Backend 32 bytes, FD transport 5792 bytes, peer 8192 bytes, with
fixed output queues on the worker stack. Two owned object allocations and two
additional joined I/O workers supplement the existing Host heartbeat. Instrumented
C++ startup allocations total 7 (Backend/Host creation); twelve complete discovery
exchanges allocate zero C++ objects in steady state. The counter intercepts ordinary,
array and aligned C++ new/delete in the test process, including peer/FD threads;
C-library/OS/thread-stack allocations are excluded, as in the accepted FD probe.
No allocation threshold or machine-specific throughput gate replaces correctness.

A normal full daemon/client scenario measured 414.651 ms locally.
The daemon harness reports the normal full client scenario wall time, including
RPC validation, camera ownership checks, capture, recording, live replay and
process retirement. This is a software integration measurement.
Native ARM64 remains an independent remote CI acceptance gate; no physical system
or live readiness inspection is used for local verification.


## Completed local validation

The final implementation was built and checked on Linux x86_64. Cached build
configurations use `/usr/bin/python3` and the documented local OpenCV dependency
cache (`build/l2-deps/opencv`), with Debug builds and testing enabled. Studio ON
uses `build/debug`; headless uses `build/l2`; headless ASan/UBSan uses
`build/l2-sanitized`. The latter two retain the optional live E2 conformance test
against the clean, exact accepted firmware checkout
`92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8`; it is not a default dependency.
Studio ON and the non-test configuration use no firmware checkout.

| Final check | Result |
| --- | --- |
| Complete Studio ON build / CTest | PASS, 142/142 tests, 142.79 s |
| Complete Studio OFF build / CTest | PASS, 140/140 tests, 129.72 s |
| Complete ASan/UBSan build / CTest | PASS, 140/140 tests, 339.93 s; leak detection and fatal errors enabled |
| `BUILD_TESTING=OFF` full build | PASS; no POSIX/serial/PTY simulation targets or product linkage |
| New backend repetition | PASS, 50 runs: 850 Host lifetimes and 10,000 injected constructor failures |
| Existing PTY / abandonment / serial repetition | PASS, 100 runs each |
| Sanitized PTY / abandonment / serial / new fixture / backend repetition | PASS, 20 runs each |
| Sanitized daemon integration repetition | PASS, 5 runs × 22 scenarios |
| Repository boundaries / frozen L2 catalog | PASS; 129 structs, 10 callbacks, 816 fields, 113 constants, 22 IDs |
| Source/binary isolation / formatting / whitespace / local Markdown references | PASS |

Complete final build/test commands on those configured directories:

```bash
cmake --build build/debug --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled ctest --test-dir build/debug --output-on-failure
cmake --build build/l2 --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled ctest --test-dir build/l2 --output-on-failure
cmake --build build/l2-sanitized --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/l2-sanitized --output-on-failure
cmake --build build/l7b2a-no-tests --parallel 4
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
ctest --test-dir build/debug -R '^x1-f2-pty-backend$' --repeat until-fail:50 --output-on-failure
ctest --test-dir build/debug -R '^x1-f2-(pty|pty-abandonment|serial-settings)$' --repeat until-fail:100 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/l2-sanitized -R '^x1-f2-(pty|pty-abandonment|serial-settings|pty-fixture|pty-backend)$' --repeat until-fail:20 --output-on-failure
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/l2-sanitized -R '^x1-f2-pty-end-to-end$' --repeat until-fail:5 --output-on-failure
```

The full suites include accepted protocol, controller, transport, E1/E2,
L7a, acquisition/pairing/pacing, L3–L6, calibration, storage/replay, protocol
compatibility, ABI and catalog regressions. No assertion or required test was
removed or disabled. Native ARM64 execution was unavailable locally and remains
part of the independent five-job remote CI gate. Local success is implementation
evidence; subsequent independent acceptance and exact-SHA five-job CI success
for `35f4b9845c79d448c7e7295ed129d063fc1b5327` are recorded at the top of this document.
