# X1 F2 v1 hardware-free integration — L7b-1 / L7b-2a

**L7b-1 FINAL ACCEPTED. L7b-2a FINAL ACCEPTED.** L0–L6 and L7a are FINAL ACCEPTED;
L7a baseline is `90d4b15850560c2f0037796770fb42e6d53e2d33`. L7b-2b, L7c, L7d
and L8 remain separate planned gates. This record establishes software behavior,
not physical controller readiness or acceptance of L7 as a whole.

Independent L7b-1 acceptance is Studio SHA
`27185c198a15b9340023cf03e55938c327acc018`, with all five required CI jobs
SUCCESS in [run 38079990496](https://github.com/martinkoenig/mantis-studio/actions/runs/38079990496).
That acceptance includes R01 calibration revocation and R02 publication admission.

Independent L7b-2a acceptance is Studio SHA
`f23a5fd47935010efc7e88ffbebe5ba484df5ec2`, with all five required jobs SUCCESS in
[run 38089039961](https://github.com/martinkoenig/mantis-studio/actions/runs/38089039961).
L7b-2a-E1 is FINAL ACCEPTED at `229c508c47738f5759e67cb7667563845e07a292`.

## Authority and boundaries

The historical accepted shared F2 baseline is firmware commit
[`854220b1717ef1e299bbd840d4cb5005dbcc31ed`](https://github.com/martinkoenig/mantis-x1-firmware/tree/854220b1717ef1e299bbd840d4cb5005dbcc31ed).
The transport/lifecycle/message dictionary plus discovery/calibration and terminal
recovery corrections at that SHA are authoritative, including V12's 29-byte
CONFIGURE and V13's unbound sensor selector. Historical draft status prose does
not change that pinned acceptance. The original simulated backend does not use
firmware runtime/F3/F4 code. Firmware PR 4/main are not floating protocol dependencies.
The L7b-2a assignment named `shared/protocol/UART_V1_0_CONTRACT.md`; the pinned
tree does not contain that path (GitHub contents API returns 404). The five
`docs/protocol/` transport, lifecycle, messages, discovery/calibration and recovery
documents present at that historical accepted SHA supplied the original protocol authority.

The E1 normative firmware protocol baseline was
`091d5702417ba6c9c67e82806b8014c7cf4d86a0`. Its
[UART v1.0 contract](https://github.com/martinkoenig/mantis-x1-firmware/blob/091d5702417ba6c9c67e82806b8014c7cf4d86a0/shared/protocol/UART_V1_0_CONTRACT.md)
and [F2-ERR-001](https://github.com/martinkoenig/mantis-x1-firmware/blob/091d5702417ba6c9c67e82806b8014c7cf4d86a0/docs/protocol/F2-ERR-001-unknown-request-ids.md)
are ACCEPTED / NORMATIVE. The entrypoint exists at this newer SHA. The erratum
clarifies unknown REQUEST rejection without changing version, known layouts or
V1–V13. At E1, firmware F4a was not independently accepted or used as authority.
The subsequently accepted firmware main is
`92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8`: F4a FINAL ACCEPTED at
`963f583addd2b89da9a0f6548d428dad208689f8` / PR #9 MERGED; F4b-0 FINAL ACCEPTED /
MERGED via PR #10. The [E2 interoperability record](x1-f2-interoperability.md)
uses those real portable implementations only in an opt-in conformance executable.
E2 is FINAL ACCEPTED at Studio `fd18decf6b509254fe104fa7b280cf02765f2296`. No firmware source is modified
or vendored, and normal production/CI builds remain independent of firmware access.
The [L7b-2b-1 Linux FD foundation](x1-f2-posix-transport.md) is separate and
FINAL ACCEPTED at `6489c6eec05ac61f685f71df5805cc825a912b2d`, compiled only for PTY tests.
This simulation retains its original in-memory byte transport and activation guard.

`plugins/first-party/devices/x1/f2/codec.{hpp,cpp}` is a private standard-C++ codec,
independent of Qt, Protobuf, SQLite, OpenCV and OS UART APIs. It defines explicit
LE fields, CRC-32C, standard COBS, framing, bounded streaming recovery and typed
schema validation. It changes no Studio public header or persistent format.

`tests/fixtures/x1-f2/controller.*` owns a bounded deterministic MCU-local model.
`link.*` provides the in-memory framed byte transport and per-instance injection.
The reusable `plugins/first-party/devices/x1/f2/host.*` owns coherent discovery,
session/transaction handling, lease servicing, terminal recovery and STOP through
`transport.hpp`. `plugin.cpp` bridges those facts through frozen ProjectedLightV1.
The simulator receives bytes through its own parser; replies/events are encoded
and pass through a second host parser. Studio never directly calls an execution
method to bypass F2. Controller mutation hooks are per-instance test facilities;
their callbacks must be nonblocking. No such callback runs on a hardware path.

mantisd remains authoritative for canonical program, RunId/GenerationId, resource
ownership, sequencing, recording, final outcome and replay. There is no X1 branch
in L3 and no replacement service API.

## L7b-2a-E1 F2-ERR-001 conformance

**FINAL ACCEPTED at `229c508c47738f5759e67cb7667563845e07a292`.** E1-01–E1-10 implement the
normative erratum identified above. Classification now uses the frozen REQUEST
registry: EVENT-only IDs are unknown when submitted as REQUEST. An envelope-valid
unknown unbound request returns exactly four bytes, UNSUPPORTED / TERMINAL and
the current state, whether or not another session exists. Opaque payloads remain
bounded by F2a's 132-byte maximum. This grants no discovery or execution authority
and changes neither session, configuration, request cache nor lease.

Bound unknown requests retain the existing session/sequence/duplicate precedence.
An expected regular request consumes one ID and caches its complete immediate
rejection. Retries replay the original header, error and controller-state byte,
even after the state changes. Conflicting duplicates cannot replace that cache;
exhaustion never wraps. Disabled simulator injection selectors no longer match
unknown opcode zero accidentally.

The reusable Host handles the exception only at its existing transaction boundary.
It requires an outstanding unknown REQUEST, a parsed RESPONSE matching all three
correlation fields, exactly four payload bytes, a defined nonzero result,
TERMINAL stage and a valid controller state. It reports a `ProtocolError`, never
success. Unbound probes do not consume a regular ID; bound errors retain the
existing consumed/unconsumed admission distinction. `valid_message()`, known
opcode lengths, rejection extensions, enums, wire version and V1–V13 are unchanged.
No arbitrary-opcode public operation was added. A private friend seam in the unit
test reaches the actual transaction implementation through the real framed Link.

| Regression | Observable coverage |
| --- | --- |
| T01–T05 | No-session/active-session probes, EVENT-only IDs, 0/1/58/132-byte payloads, partial-unbound admission, unchanged authority and lease |
| T06–T10 | Exact consumption/cache replay across completion, conflicting duplicates, stale/gapped IDs, exhaustion, session replacement and reboot |
| T11–T12 | Actual Host transaction rejection; all defined errors/states; bad correlation, direction, length, positive/undefined result and stage; unsolicited reply filtering |
| T13–T15 | Every known rejection layout and zero extension remains strict; corrupt CRC/COBS/version/header/reserved/direction traffic cannot admit commands or refresh leases |
| T16 | Barrier-controlled blocked unknown receive, traffic and saturated queues; independent heartbeat and acknowledged priority STOP |
| T17–T18 | Existing discovery/session/finite-run/recovery/STOP, R01/R02, daemon/client recording and controller-free exact replay tests retained |

The Host still depends only on the private codec/transport. Controller behavior
and injection remain test-only. No queues, workers, image copies or hot-path
allocations were added. Link's deterministic receive-barrier instrumentation adds
one fixed 64-bit counter for unknown IDs (8 bytes per fixture instance). All
existing resource limits, borrowed transport lifetime, heartbeat retirement and
callback admission rules remain unchanged. Software rejection/STOP is not
physical output-state evidence. L7b-2b is PLANNED / BLOCKED by separate physical
and controller-implementation qualification gates.

### L7b-2a-E1 local validation — 2026-10-11

Validation uses the same cached configurations and dependency/runtime setup as
the accepted extraction. Complete builds and full suites, plus focused repetition,
cover the unchanged L7a/F2 daemon paths, recording/replay, ABI/catalog, camera,
L2–L6, protocol/storage goldens and source/binary boundaries.

| Check | Result |
| --- | --- |
| Linux x86_64 Studio ON, complete build/full CTest | 129/129 PASS, 131.98 s |
| Linux x86_64 Studio OFF, complete build/full CTest | 126/126 PASS, 118.52 s |
| ASan/UBSan, complete build/full CTest | 126/126 PASS, 325.94 s; leak detection and fatal errors enabled |
| Complete BUILD_TESTING=OFF / Studio OFF build | PASS; codec/Host libraries built, no simulation targets/plugin directory, 0 tests |
| Controller, fixture and transport, 100 repetitions each | 300 executions PASS, 113.13 s |
| All six F2 and both L7a tests, 10 repetitions each, Studio ON | 80 executions PASS, 85.58 s |
| ASan/UBSan: all six F2 and both L7a tests, 10 repetitions each | 80 executions PASS, 185.91 s; leak detection and fatal errors enabled |
| Boundaries, frozen ABI/catalog, changed-C++ formatting, whitespace and local documentation references/anchors | PASS |

Reproduction:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/l2-deps/opencv"
cmake --build --preset linux-debug --parallel 4
cmake -S . -B build/l2 -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=OFF \
  -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2 --parallel 4
cmake -S . -B build/l2-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DMANTIS_SANITIZE=ON -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2-sanitized --parallel 4

export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu
export OPENCV_OPENCL_RUNTIME=disabled
ctest --preset linux-debug
ctest --test-dir build/l2 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-(controller|fixture|transport)$' \
  --repeat until-fail:100 --output-on-failure
ctest --test-dir build/debug -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
```

The non-test build and boundary/catalog/format checks use the commands in the
L7b-2a validation section below. Only locally executable x86_64 configurations
were run; native ARM64 and the new exact-SHA five-job CI matrix remain independent
review gates. No test or assertion was disabled or weakened.

On this compiler/ABI, Host remains 368 bytes and Link is 7,480 bytes (previously
7,472); the added barrier counter is fixed instance storage, with no heap owner.
A Debug sample on Ubuntu 25.10, GCC 15.2, AMD Ryzen AI 9 HX 370 measured
324,899 V13 encode + standalone/streaming decode iterations/s, zero counted
hot-loop heap allocations, a 216-byte parser and 161-byte collector. A Studio-ON
controller sample measured GET_STATUS p50/p99/max 7.454/8.085/11.121 µs, priority
STOP with blocked RUN acknowledgement 16.781 µs, and 100 complete manual-clock
lifetimes in 0.019534 s. Reply/event capacities remain 8/8 with separate
STOP/heartbeat slots. F2 has no image buffers or image copies. These single-run
software samples were taken during other local tests; they are not throughput
thresholds, physical timing measurements or safety guarantees. Reproduce with
`build/l2/bin/mantis-x1-f2-protocol-tests` and
`build/debug/bin/mantis-x1-f2-controller-tests` using the environment above.

## L7b-2a private host/transport boundary

The codec and new `mantis-x1-f2-host` static target compile in ordinary builds,
including `BUILD_TESTING=OFF`. Their sources/headers require only standard C++ and
Threads; they are private implementation modules, uninstalled and unlinked to the
production camera plugin. The simulator and alternative plugin remain test-only.
There is one Host implementation and no Host copy under `tests/fixtures/`.

| Layer | Ownership |
| --- | --- |
| Codec | Unchanged framing, bounded incremental parser, CRC/COBS and typed schemas |
| Transport contract | Bounded exact-wire submission; matched replies, separate events, deadlines and explicit timeout/interrupted/closed/unavailable outcomes |
| Host | One ordinary transaction, request IDs, byte-identical retries, coherent discovery, session/ARM/execution association, heartbeat, fencing, STOP and terminal recovery |
| Simulated Link | Two parsers, finite reply/event rings, dedicated STOP/heartbeat slots, manual time, response matching, disconnect/interruption and fault injection |
| Simulated Controller | Boot/session/configuration/ARM, finite pulse accounting, lease enforcement, faults and canonical terminal retention |
| ProjectedLight fixture | Strict existing JSON/activation/mapping, Studio-to-controller association, pure preflight, L2 evidence, abort and callback admission |
| mantisd / ProjectedRun | Canonical program, Studio RunId/GenerationId, resource ownership, sequencing, recording, final outcome and controller-free replay |

`configuration.hpp` separates an explicit controller/board/channel selection and
calibration provenance policy from finite current/period/high/pulse parameters.
No BenchConfig constants, synthetic MCU, concrete Link, injection or fixture hook
enter Host. The fixture converts its unchanged, strictly validated JSON values
into these private representations. Only an explicit SimulationOnly policy exists;
no synthetic token or readiness can qualify a physical output. The current host
execution profile retains the accepted single-channel/register-inventory v1 limits.

Host borrows a caller-owned Transport without a shared ownership allocation. The
transport must outlive every Host call and its joined heartbeat worker; callers
quiesce public operations before destruction. In the fixture, Link is declared
before Host and destroyed afterward. Shutdown fences admission, dispatches STOP,
interrupts reads and retires heartbeat within the deadline; refused plugin destroy
retains ownership until active callbacks retire. No detached worker was added.

Transport waits release the lock needed for byte dispatch. STOP remains request ID
zero, with its own reply slot, independent of ordinary/event capacity and pending
heartbeat replies. Interrupt wakes ordinary/heartbeat/event reads but cannot cancel
priority STOP reception. A closed link wakes all reads and refuses submission.
Host never inspects injection settings: suppressed heartbeats are dropped at the
simulated byte peer before lease admission; a blocked terminal read reports
interruption, preventing that read's status reconciliation. Lost events report
timeout and retain the accepted status/exact-terminal recovery path.

`x1-f2-transport` adds actual-byte fragmentation, corrupt COBS/CRC, idle recovery,
matching/stale reply filtering, event separation, closure/interruption, identical
RUN retry bytes, one outstanding ordinary transaction, heartbeat progress during
blocked RUN, shutdown during receive, saturation and STOP versus blocked heartbeat.
Existing controller tests retain delayed/duplicate replies, calibration revocation,
snapshot/lease/reboot recovery and repeated lifetimes. R02 callback barriers and
the actual daemon/recording/replay tests are unchanged. `x1-f2-boundaries` checks
the entire private include graph, forbidden fixture/API types, non-test target
placement, absence of duplicate Host and the fixture binary's physical API symbols.
No image copy, extra frame queue or per-byte allocation is introduced.

A real UART backend, physical policy, electrical qualification, device selection,
USB HIL, hardware commissioning and physical output remain separate blocked gates.
The existing [hardware readiness checklist](x1-projected-light-integration.md#physical-controller-readiness-facts)
remains authoritative. L7b-2a is FINAL ACCEPTED as a hardware-free extraction;
this establishes no acceptance of physical L7b-2 integration.

### L7b-2a local validation — 2026-10-10

All four complete builds succeeded. Local results for the extraction:

| Check | Result |
| --- | --- |
| Linux x86_64 Studio ON, full CTest | 129/129 PASS, 159.25 s |
| Linux x86_64 Studio OFF, full CTest | 126/126 PASS, 146.30 s |
| ASan/UBSan, complete full CTest | 126/126 PASS, 385.23 s; leak detection and fatal UB enabled |
| Fresh complete BUILD_TESTING=OFF / Studio OFF build | PASS; codec/Host libraries present, simulator/fixture targets and plugin directories absent, 0 tests |
| Controller, fixture and transport, 100 repetitions each | 300 executions PASS, 82.68 s |
| All six F2 and both L7a tests, 10 repetitions each | 80 executions PASS, 83.51 s |
| ASan/UBSan: all six F2 and both L7a tests, 10 repetitions each | 80 executions PASS, 195.96 s; leak detection and fatal UB enabled |
| Source/binary boundaries, frozen ABI/catalog, changed-C++ formatting, Python syntax, whitespace and local documentation links | PASS |

The full suites retain camera-only acquisition/pairing, calibration, L2/L3/L4/L5,
L6 processing, protocol/storage goldens and daemon/controller-free exact replay.
No assertions, tests, sanitizer checks or physical activation guards were weakened.
The existing full configure/build/CTest commands below were reused, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`.
Additional reproduction commands (same dependency cache/loader environment):

```bash
cmake -S . -B build/l7b2a-no-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF \
  -DOpenCV_DIR="$PWD/build/l2-deps/opencv" -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l7b2a-no-tests --parallel 4
cmake --build build/l7b2a-no-tests --target help
ctest --test-dir build/l7b2a-no-tests -N

export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu
export OPENCV_OPENCL_RUNTIME=disabled
ctest --test-dir build/l2 -R '^x1-f2-(controller|fixture|transport)$' \
  --repeat until-fail:100 --output-on-failure
ctest --test-dir build/l2 -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
python3 tests/contract/x1_f2_boundaries.py . \
  build/l2/x1-f2-plugins/libmantis-x1-f2-fixture.so
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
git diff --check
```

The environment is the same Ubuntu 25.10/GCC 15.2/x86_64 Debug environment
recorded below. A standalone Studio-ON sample gave 314,854 V13 encode + standalone
and streaming decode iterations/s, zero counted hot-loop heap allocations,
GET_STATUS p50/p99/max 10.85/11.782/16 µs, STOP with blocked RUN acknowledgement
17.843 µs, and 100 complete manual-clock lifetimes in 0.0285209 s. These are
scheduler-sensitive software samples, not physical timings or throughput floors;
the codec source is unchanged. Reproduce with the three standalone protocol,
controller and transport test executables. Queue high-water remains 8/8, with
separate STOP/heartbeat slots; parser remains 216 B with a 161 B collector.

On this compiler/ABI, compiling the accepted and extracted headers for `sizeof`
comparison gives Host 336 → 368 B, Snapshot 872 → 904 B and simulated Link
7312 → 7472 B. Selection is 32 B and FiniteExecution 16 B. The per-fixture fixed
increase is 224 B: two stored selections plus Link's virtual interface and bounded
receive-barrier counters. No new per-instance heap ownership, worker, queue or
image copy is added; the existing joined worker/function storage remains.
Native ARM64 and the exact submitted SHA's five-job remote matrix remain
independent-review gates. Real UART/USB HIL and hardware qualification remain blocked.

## Exact activation and mapping

BUILD_TESTING on Linux builds `x1-f2-plugins/libmantis-x1-f2-fixture.so`, outside
ordinary `plugins/` and outside the L7a fixture directory. It is not installed.
Activation requires **all** of:

- explicit `mantisd --plugins .../x1-f2-plugins` selection;
- exact `MANTIS_X1_F2_SIMULATION=1`;
- nonempty `MANTIS_X1_FAKE` and explicit `MANTIS_X1_PROFILE`;
- explicit `MANTIS_X1_F2_CONFIG` naming the checked version-1 bench configuration.

The DSO reuses `org.mantis.x1` as an explicitly selected alternative implementation.
It must not be combined with the ordinary X1 directory. Its implementation identity
is `org.mantis.x1.f2.simulation-only`, plugin version 1.1.0. The production
`mantis-x1.so` still queries only camera acquisition, regardless of fixture flags.
The fixture links fake/profile/media sources, **never linux.cpp**. No physical
transport class, serial/GPIO device selector, output API or fallback exists.
Unknown configuration keys, backend names and mappings fail closed; a
`serial_device`/GPIO selector cannot select hardware. Configuration paths must
name regular JSON files; special device files are rejected before opening.

The parent retains the accepted camera-derived stable physical identity. Cameras
remain `<parent>/left` and `/right`, with exact selected streams, sensor/bus
identities, dimensions and left/right roles. The mapped synthetic emitter is
`<parent>/simulation-f2/L1`; its role is explicitly `synthetic-L1`. The controller
is `<parent>/simulation-f2/esp8266`, controlling exactly that emitter. No L7 or
second electrical channel is advertised. The root contains those four children.

The versioned `config.json` explicitly binds controller `0x82660001`, board revision
1, channel UID `0x8266000100000001`, index 0, those component suffixes and synthetic
rig revision 1. Selected parameters are 10,000 µA requested synthetic current,
10,000 µs period, 1,000 µs high, 20 pulses: 200 ms finite synthetic execution.
Limits are 50,000 µA, 1,000 pulses, 2,000 ms run, 5,000 µs continuous high; low
interval must be at least 100 µs. These are implemented simulation constraints,
not measured electrical/current/timing limits. The token is obtained from current
coherent discovery, bound to the simulated boot/channel/board/provisioning scope,
never supplied as a physical calibration credential.

The graph preserves camera discovery and the shared private ownership gate from
`x1/acquisition.hpp`; camera-only acquisition continues using the accepted native
pairing path. F2 projected programs do not start cameras. Both direct ABI and L5
ownership tests reject camera→projected, projected→camera and second projected
opens, without stopping the original run. Normal shutdown releases the stable
parent; in-use destruction refuses and retains state. Production profile, native
counters, 5 ms software correspondence and pixel handling are unchanged.

## Program admission, identities and evidence

Only one declared step, one repetition, one selected emitter/controller and one
complete OFF/ON vector are accepted. CaptureMode::none, no cameras/trigger,
zero settling, CommandedOnly and ControllerRegister scope are required. Declared
step/run/ON bounds must contain the selected finite configuration and fit device
limits. Multi-step/repeated, L1+L7, camera capture, hardware trigger, positive
settling, stronger/electrical/optical proof and unsupported mappings are rejected
before ARM. An ON step submits exactly one RUN_FINITE. OFF-only dispatches STOP,
which also invalidates configuration/ARM, without RUN_FINITE.

validate is read-only discovery/preflight: no CLAIM, CONFIGURE, ARM, RUN or lease
renewal. prepare invalidates old prepared state first and checks the current boot
and snapshot without enabling. start binds immutable Studio identity to private
`boot → host session → arm generation → execution ID`; controller identities do
not replace Studio identities. Normal sessions/execution identities and simulated
boots use OS-backed `std::random_device`; deterministic identity/clock injection
is restricted to tests. Session tokens are collision-avoidance/correlation values,
not authentication.

The first bundle establishes the actually dispatched software command, its host
monotonic dispatch time, exact declared step, implementation and ProgramReference.
A matching successful terminal record is required for completed evidence. Admission
ACK alone never completes an ON run. Terminal record bytes are retained verbatim
as bounded diagnostic provenance in terminal evidence, including actual synthetic
pulse accounting, native clock, origin identities and separate initiating/cleanup
codes. Diagnostic text does not introduce a typed physical measurement. Existing
schema-1 evidence remains authoritative for command/step/disposition semantics.

Acknowledged/observed/exposure-effective fields are Unavailable; terminal command
presence is Unknown. No frame, exposure, trigger, measured current, optical ON/OFF,
laser attribution, hardware synchronization, electrical inhibit or physical
watchdog/interlock/fail-OFF evidence is fabricated. A sensor descriptor is synthetic
controller-register inventory only, with no runtime measurement delivery path.

## Transactions, timing, recovery and STOP

Discovery checks the exact boot/snapshot pair, channel UID/index, synthetic
readiness/calibration/board, per-channel/global limits and sensor schema, then
rechecks global capabilities and boot. Three complete attempts are the maximum;
no mixed partial snapshot is advertised. CONFIGURE rejects changed snapshots
without configuration/ARM/lease mutation; normal state transitions do not increment
the snapshot. ARM and RUN revalidate the configuration basis.

The host has one ordinary transaction, sequential IDs without wrap and three
maximum attempts with 20 ms retry windows capped by the caller's total deadline.
Every retry sends identical complete bytes. The MCU caches the latest consumed
request and immediate response, including admitted semantic rejection. Duplicate
RUN does not reset/repeat pulse accounting. An ambiguous run transaction fences
future admission; recovery never submits a new RUN to resolve uncertainty.

Lease is 500 ms and heartbeat cadence 100 ms, software parameters only. KEEPALIVE
has request ID 0 and exact session/arm/advancing counter. A blocked ordinary
transaction does not own the heartbeat gate. Idle hosts do not poll; lease work
uses one joined `jthread`, interruptible condition waits and one heartbeat slot.
The manual-clock controller processes completion/lease boundaries chronologically;
interrupted counts use completed periods, not requested counts. Lease expiry
invalidates authority, retains the latest terminal record, and requires a fresh
session/configuration/ARM. Reboot changes boot/token and erases volatile history.

Lost/delayed/stale events are reconciled through status and exact terminal lookup.
Live-session unbound status redacts identities/lease/sequence; historical recovery
requires the exact origin triple. Wrong/malformed/reboot-erased records cannot
become successful execution or confirmed physical OFF.

Abort first fences Studio generation and host enabling admission, then sends the
real F2 STOP frame (0x0014, request ID 0, empty payload). Only the bounded byte
exchange holds the short transport/control locks; ordinary response waits do not.
STOP has its own response slot, independent of normal/event capacity and heartbeat
admission. It never waits for recorder, L3 publication or a pending next call.
Missing STOP acknowledgement produces Unknown request outcome and explicit cleanup
error, never physical inhibition. Initiating and cleanup errors remain separate.
Abort retires the joined heartbeat worker within the abort caller's budget before
returning its outcome, so run-deadline expiry can safely leave zero budget for
subsequent stop/destroy. Fresh start can resume a retired worker; restart/retirement
share a separate bounded lifecycle gate that never gates STOP dispatch. Tests use
heartbeat request barriers for three retire/restart cycles and zero-budget
stop/destroy after an entered callback retires.
Destroy refuses entered callbacks and retires the heartbeat before deleting state;
there are no detached threads. Already-entered synchronous callbacks may finish,
while future stale publications/ON commands are fenced.

## Resource and validation evidence

Fixed capacities: payload 132 B, decoded frame 160 B, encoded COBS 161 B, complete
wire 163 B; collector 161 B with 50 ms idle discard to the next delimiter. Normal
response/event rings each hold eight frames, with one separate STOP and heartbeat
slot, one delayed response/event each, one current execution and one latest
terminal record. Per-session execution and accepted ARM nonce histories each
admit at most 64 unique identities; overflow rejects rather than forgetting intent.
Graph bounds: five components/two discoverable cameras, one step/instance, 32
commands/events, 1 MiB accounting, one pending L3 bundle, 1,000 ms maximum call,
2 s run/step/ON ceilings. There is no frame bridge queue, image serialization or
pixel copy in F2. Configuration is bounded to 4 KiB; diagnostics stay bounded.

Tests are `x1-f2-protocol`, `x1-f2-controller`, `x1-f2-fixture` and
`x1-f2-end-to-end`. Independent literal V1–V13 goldens come from the pinned F2
specification/oracle, never from Studio's codec. Mutations cover CRC, COBS,
header/reserved/class/version/length, overflow, noise, partial input and idle
recovery. Controller tests exercise lifecycle, calibration/snapshots, admitted
rejection caching, duplicates, wrong session/state/direction, heartbeat/lease,
STOP/DISARM/fault-clear, exact terminal bytes, lost/delayed/corrupt responses,
stale/malformed events, reboot, bounded timeout and queue saturation.

Priority tests use request/event barriers and manual clocks for blocked ordinary
RUN/ARM, heartbeat progress, no post-STOP pulses, blocked next and entered callback
lifetime. Repeated cycles check ownership release. The actual daemon/client test
covers discovery/pure validation, idempotent/stale start/control identities,
ON/OFF-only success, zero-bundle prepare/configure/ARM failure, lost ACK/event,
lease/reboot/cleanup, stop/cancel/deadline, missing STOP ACK and a real filesystem
recorder failure. Recorder failure stays RECOVERABLE with RESOURCE_LIMIT and no
false final outcome. Healthy captures finalize RawCapture-3 using unchanged
MRUNHDR3/MRAWREC3/MRUNOUT3. Replay runs twice after daemon/controller teardown,
with all fixture activation variables removed, comparing canonical semantic bytes
and outcome identities. It opens no controller and recomputes no evidence.

Reproduce in existing repository build trees:

```bash
cmake --build build/l2 --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --test-dir build/l2 -R '^x1-f2-' --output-on-failure
# The same tests run in linux-debug (Studio ON), headless and sanitizer builds.
```

For explicit daemon bench activation:

```bash
MANTIS_X1_F2_SIMULATION=1 MANTIS_X1_FAKE=normal MANTIS_X1_FAKE_PACE=1 \
MANTIS_X1_PROFILE="$PWD/tests/fixtures/x1-projected/profile.json" \
MANTIS_X1_F2_CONFIG="$PWD/tests/fixtures/x1-f2/config.json" \
MANTIS_TOKEN=x1-f2-simulation-only-token \
./build/debug/bin/mantisd --plugins "$PWD/build/debug/x1-f2-plugins" \
  --project "$PWD/build/debug/F2Bench.mantis"
```

Per-instance fault scenarios selected only in this DSO through
`MANTIS_X1_F2_FAULT` are configure, arm, lost-ack, lost-event, blocked, stop,
cleanup, lease, link, reboot, prepare, hold and recorder. hold/recorder require an
explicit `MANTIS_X1_F2_RELEASE_FILE`; checks occur outside the priority lock and
wake on abort. They establish deterministic filesystem-failure/ownership barriers.
Protocol tests use the richer per-instance Injection structure directly.

### L7b-1 initial local results

Historical implementation verification on 2026-10-10 (before R01/R02 acceptance):

| Configuration/check | Result |
| --- | --- |
| Complete Linux x86_64 Studio ON build / full CTest | PASS, 127/127, 126.18 s |
| Complete Linux x86_64 Studio OFF build / full CTest | PASS, 124/124, 114.89 s |
| Complete ASan/UBSan build / full CTest, leak detection and UB halt enabled | PASS, 124/124, 305.19 s |
| Four new F2 tests, ten consecutive repetitions each | PASS, 40 executions, 58.10 s |
| Controller test, 100 additional consecutive repetitions | PASS, 100 executions, 55.34 s |
| ASan/UBSan F2 and L7a tests, ten consecutive repetitions each | PASS, 60 executions, 172.94 s |
| Boundary contract, changed-C++ clang-format, Python syntax, diff whitespace | PASS |
| Relative documentation links and new anchors | PASS, 118 links |

The complete suites include frozen L2 ABI/catalog, projected contract/sequencer/
RawCapture/control/calibration, acquisition/Y10P/QoS, X1 pairing and fake pacing,
L7a fixture, L6 ProcessorV2/observation, storage golden and protocol compatibility
regressions. No existing assertion was weakened or test disabled. Final code
review corrections cover nonadjacent ARM nonce reuse, unresolved cleanup claim/
fault-clear admission, special-file configuration rejection and synchronized,
generation-fenced terminal diagnostics and zero-budget cleanup after heartbeat
retirement. All affected tests were rerun.

Synthetic measurement environment: Ubuntu 25.10 x86_64, Linux 6.17.0-41,
AMD Ryzen AI 9 HX 370 (24 logical CPUs), GCC 15.2.0, CMake 3.31.6, Debug.
One sample of the standalone tests gave:

| Measurement | Observed software result |
| --- | --- |
| 100,000 V13 request iterations, each encode + standalone decode + streaming decode | 425,751 iterations/s; zero heap allocations during the counted loop |
| Parser storage / collector | 216 B object / 161 B fixed collector |
| 100 framed GET_STATUS request/reply transactions | p50 4.889 µs, p95 5 µs, p99 5.861 µs, maximum 6.341 µs |
| STOP dispatch plus reply while ordinary RUN acknowledgement is blocked | 9.518 µs |
| 100 complete manual-clock Host/controller lifetimes | 0.0172233 s; one admitted run per lifetime |
| Reply/event queue high water under deliberate saturation | 8 / 8 frames; separate STOP/heartbeat slots remain usable |
| Image-buffer copies in F2 | 0; the supported profile has no images or image buffers |

Reproduce with `build/l2/bin/mantis-x1-f2-protocol-tests` and
`build/l2/bin/mantis-x1-f2-controller-tests`. Timings use host steady clock; pulse
execution uses an injected manual clock and deterministic test identities, so
the lifetime total excludes the virtual 200 ms per-run interval. The allocation
counter covers the codec/parser loop, not all host construction. These are single
Debug software samples sensitive to scheduler load, not real UART, Q6A, optical
or safety latency measurements. Regression assertions enforce finite storage,
zero hot-loop allocations, exact results and bounded caller deadlines rather
than machine-specific throughput floors.

The existing headless/sanitizer caches use the same Debug/Studio-OFF/sanitizer
settings as the repository presets. Final configure/build/test commands are:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/l2-deps/opencv"
cmake --build --preset linux-debug --parallel 4

cmake -S . -B build/l2 -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2 --parallel 4

cmake -S . -B build/l2-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DMANTIS_SANITIZE=ON \
  -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2-sanitized --parallel 4

export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu
export OPENCV_OPENCL_RUNTIME=disabled
ctest --preset linux-debug
ctest --test-dir build/l2 --output-on-failure
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-' --repeat until-fail:10 --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-controller$' --repeat until-fail:100 --output-on-failure
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
python3 tests/contract/boundaries.py .
git diff --check
```

The worktree's existing OpenCV dependency cache is reused. The loader selects
system OpenCV and disables vendor OpenCL initialization for CPU-only fixtures;
no sanitizer suppression or disabled test is used. Native ARM64 execution is
unavailable on this x86_64 host and remains a mandatory remote CI review gate.

### Assignment coverage

| Requirements | Implementation and verification |
| --- | --- |
| L7B-01–03 | Private adapter/controller identities, daemon ownership, isolated SimulationOnly DSO and activation rejection tests |
| L7B-04–08 | Independent codec, bounded parser, exact registry/layouts, snapshot fencing and literal V1–V13/mutation tests |
| L7B-09–13 | Deterministic lifecycle, coherent synthetic discovery/calibration, checked timing and actual interrupted pulse accounting |
| L7B-14–18 | Sequential session transactions, identical retries/cached rejection, independent heartbeat, canonical terminal retention/recovery |
| L7B-19–21 | Separate STOP admission/slots, generation fence, bounded cleanup and entered-callback/destroy barriers |
| L7B-22–24 | One-step control-only admission, explicit versioned mapping and capability/Studio duration preflight |
| L7B-25–28 | Frozen L2 callbacks, pure validation, invalidated preparation, terminal-gated completion and honest software evidence |
| L7B-29–30 | Real daemon/client controls, ownership, RawCapture-3 success/failure and controller-free two-pass replay |
| L7B-31–36 | Protocol/lifecycle injections, priority/lifetime barriers, repeated runs and complete accepted regression suites |
| L7B-37–39 | Fixed collectors/queues/history, zero parser allocations/pixel copies, reproducible synthetic timings and resource checks |
| L7B-40–41 | This validation record, consistent acceptance status and unchanged physical-readiness blockers |

## Independent review corrections R01/R02

R01 revocation of the active synthetic calibration immediately removes execution
authority. CONFIGURED/ARMED invalidation clears the configuration basis, arm and
lease, retaining the session for diagnosis; it reports NotReady/invalid calibration
without inventing a controller fault. During RUNNING, revocation uses the existing
controller fault/termination path with CALIBRATION_INVALID (18). It retains origin
boot/session/arm/execution, counts only periods completed at the revocation clock
instant, and records software OFF request presence and initiating/cleanup errors
separately. Cleanup failure retains its existing terminal-result precedence without
overwriting the calibration initiating error. Later STOP or clock advancement
cannot rewrite the canonical terminal or execute more pulses. Repeated revocation
is idempotent. A harmless snapshot revision remains distinct from revocation;
normal CONFIGURE/ARM/RUNNING/completion does not increment the snapshot.

The manual-clock regressions cover CONFIGURED, ARMED, zero completed pulses and
three completed pulses, both successful and failed cleanup, exact event/lookup
terminal bytes, fault/status fields, rejected stale configuration/arming/execution,
and a fresh boot-scoped synthetic token followed by new CLAIM/CONFIGURE/ARM.
Revoked bindings return the frozen CALIBRATION_INVALID code; with an otherwise
valid record, a missing request token still returns NOT_CALIBRATED. No remote
calibration operation was added.

R02 constructs the immutable publication views before callback admission, then
atomically checks the generation fence and reserves the publication under the
short control mutex. That reservation is the callback-admission boundary. Callback
execution occurs outside the priority lock; reservation retirement uses only a
short state update. The existing ordinary call pin retains all borrowed state.
A callback admitted before abort may finish;
a fence before admission returns NOT_READY without calling the host. Destruction
continues to refuse active calls, retaining the camera parent ownership.

A private export in the isolated test DSO installs per-instance pre/post-admission
barriers only while calls are quiescent. No hook is installed through daemon/bench
activation or the public plugin interface. Promise/future barriers reproduce abort
before admission, after admission and inside a blocked callback, repeated abort,
BUSY destruction/retained ownership, quiescent retry and a fresh generation. The
blocked callback compares canonical bundle bytes before/after abort to verify
borrowed-view immutability. STOP succeeds while each barrier remains closed;
publication count, first-error precedence and single execution admission are checked.
No queue, worker, public ABI, wire/recording format or image copy was added.

Correction validation uses the configure/build/full-suite commands above with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. All complete builds succeeded. Full Linux x86_64
Studio ON passed 127/127 tests (128.74 s); Studio OFF passed 124/124 (114.50 s);
ASan/UBSan passed 124/124 (312.12 s), with leak detection and no suppressions.
These suites include frozen ABI/catalog, V1–V13, F2/L7a daemon recording/replay,
sequencer, storage, ownership, camera pairing and L6 regressions. The expanded
controller and fixture tests additionally passed 100 consecutive runs each
(200 invocations, 80.99 s). All four F2 tests and both L7a fixture/integration tests
also passed ten consecutive runs each under ASan/UBSan (60 invocations, 178.53 s),
including the expanded revocation/admission regressions, with leak detection.
Boundary, frozen catalog, formatting, local document link-target and diff checks
passed. Native ARM64 remains an unexecuted local gate.

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --test-dir build/l2 -R '^x1-f2-(controller|fixture)$' \
  --repeat until-fail:100 --output-on-failure
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-(f2-|projected-)' \
  --repeat until-fail:10 --output-on-failure
```

The correction-focused Studio ON sample retained 0 image copies; framed STOP with
ordinary RUN acknowledgement blocked took 20.057 µs. GET_STATUS p50/p99/max were
10.44/11.882/16.491 µs; 100 manual-clock lifetimes took 0.015248 s. This sample ran
alongside local validation on the environment described above; it is not a
physical timing guarantee or a machine-specific throughput threshold. Admission
barrier regressions require acknowledged STOP with a 100 ms caller budget while
the callback remains blocked, with a 500 ms scheduler allowance on wall time.
R01/R02 are included in the independently accepted L7b-1 SHA and five-job CI
evidence recorded above. L7b-2a acceptance and its independent CI are recorded above;
L7b-2a-E1 is independently FINAL ACCEPTED; L7b-2a-E2 requires separate review/CI.

## Physical gates still blocked

There is **no physical UART, GPIO, laser/LED output, flashing, physical calibration,
optical/electrical qualification, independently verified inhibit, hardware trigger
or physical fail-OFF implementation or claim**. No PhysicalQualified backend was
introduced. F1b and L7b-2 hardware commissioning are unauthorized here.

The firmware's documented supervised ESP8266 prototype/UART design is not a
verified deployed X1 controller revision, electrical interface, emitter mapping or
independent safety mechanism. The [L7a readiness fact table/checklist](x1-projected-light-integration.md#physical-controller-readiness-facts)
continues to apply: actual board/revision/transport and electrical qualification,
channel/emitter mapping, reset/disconnect behavior, independent OFF/inhibit,
watchdog/interlock, measured OFF latency/readback, OV9281 trigger wiring/configuration
and optical timing method remain hardware blockers. F2's accepted software bytes
resolve protocol design only. Later integration requires explicit authorization and
independent review; it must not enable real emitters using synthetic provisioning.
