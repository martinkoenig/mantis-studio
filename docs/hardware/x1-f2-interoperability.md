# X1 F2 cross-repository wire interoperability — L7b-2a-E2

**FINAL ACCEPTED** at Studio `fd18decf6b509254fe104fa7b280cf02765f2296`. Studio E1 is FINAL ACCEPTED at
`229c508c47738f5759e67cb7667563845e07a292`. Historical L7b-1, L7b-2a and E1
acceptance evidence remains in the [simulation record](x1-f2-simulation.md).

Firmware F4a is FINAL ACCEPTED at `963f583addd2b89da9a0f6548d428dad208689f8`,
[PR #9 MERGED](https://github.com/martinkoenig/mantis-x1-firmware/pull/9).
F4b-0 is FINAL ACCEPTED / [PR #10 MERGED](https://github.com/martinkoenig/mantis-x1-firmware/pull/10).
The exact accepted main source used here is
`92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8`.
The [UART v1.0 contract](https://github.com/martinkoenig/mantis-x1-firmware/blob/92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8/shared/protocol/UART_V1_0_CONTRACT.md)
and [F2-ERR-001](https://github.com/martinkoenig/mantis-x1-firmware/blob/92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8/docs/protocol/F2-ERR-001-unknown-request-ids.md)
remain normative. F4b-1 and later runtime qualification are separate gates.
Neither firmware acceptance nor Xtensa compilation establishes deployed controller,
electrical/optical behavior, independent inhibit or physical safety qualification.

## Test boundaries and ownership

`tests/interop/x1-f2/` provides two complementary paths. No firmware implementation
is copied, vendored, installed or linked to a production Studio target.

| Layer | Responsibility |
| --- | --- |
| Existing private Studio codec/Host | Actual framing, typed validation, session/request/retry/heartbeat/fencing/STOP/recovery behavior |
| Opted-in FirmwareTransport | Bounded byte delivery, parser ownership, reply/event separation, response matching, loss/corruption/barriers, manual clock and shutdown |
| External F3a `transport.cpp` | Real accepted firmware CRC/COBS/envelope/stream parser and response wire serialization |
| External F3b `messages.cpp` | Real accepted typed codec, response payload serialization and direction validation |
| External F4a `controller.cpp` | Real accepted admission, discovery, configuration/ARM, execution timer, lease, fault/STOP batch precedence and canonical terminal retention |
| Self-contained CapturedTransport | One immutable request/response capture at a time; no controller or lifecycle model |
| Existing daemon fixture regressions | Studio L2/L3/L5 authority, recording and controller-free replay, independently of this opted-in test executable |

`MANTIS_F2_LIVE_INTEROP=OFF` is the default. The live target requires an explicit
`MANTIS_F2_FIRMWARE_SOURCE_DIR` pointing to a clean checkout at the exact SHA above.
Configure and every live-target build check commit/cleanliness. Missing, wrong-SHA
or dirty sources fail clearly when live testing is requested. Normal CI, production
builds and `BUILD_TESTING=OFF` need no checkout, GitHub token or firmware access.
The external sources compile directly into the uninstalled test executable; the
firmware CMake project and hardware application are not imported.

The Host sends its original complete wire bytes to F3a byte by byte. Validated
REQUEST frames reach F4a's `process_batch`; F3b handles payload serialization and
F3a encodes responses/events. A separate Studio streaming parser validates those
bytes before Host sees a reply. Wrong-direction frames are rejected by F3b before
controller admission. Unknown REQUESTs remain opaque, as required by ERR-001.
Native structures are never translated between implementations in place of bytes.

A short mutex gives the single-owner firmware engine exclusive access; it is
released during all receive/event waits. STOP and KEEPALIVE have independent
response slots. Ordinary/event rings are eight frames each; firmware batches are
at most eight and its event capacity is four. Wire/parser buffers retain 163/161
byte limits. Submitted-wire and exchange diagnostic histories each retain at most
256 entries; diagnostic overflow is counted and cannot gate STOP/heartbeat.
Ordinary queue loss is observable as bounded response absence/timeout. There are
no transport workers, detached threads, image buffers or image copies. Host's
existing joined heartbeat worker borrows the transport; tests quiesce it before
teardown. Closure and interruption wake pending readers with explicit outcomes.

On the local x86_64 Debug build, `sizeof(FirmwareTransport)` is 143,496 bytes,
mostly its fixed diagnostic capture arrays. This storage belongs only to the
live test executable. The Host instance layout is unchanged; neither production
runtime allocation nor image-copy behavior changes. Both queue high-water marks
reach exactly eight in the saturation regression, and saturated diagnostics do
not prevent STOP. The blocked-receive STOP check uses a 500 ms software deadline;
it is a regression bound, not a physical latency measurement.

Manual firmware time is absolute uint32 milliseconds; Studio parser timestamps
use that value times 1,000 in uint64 microseconds. Host I/O deadlines continue to
use steady-clock time. Tests advance the controller at explicit barriers, with
no sleep-driven execution or polling. The synthetic profile names controller,
board and channel UID 1, boot `0x0102030405060708`, token 999, current ceiling
50,000 µA, high/continuous-ON ceiling 5,000 µs, 1,000 pulses and a 2,000 ms run
ceiling. The selected operation is a requested 500 µA / 10,000 µs period /
100 µs high / 20 finite periods; these are virtual parameters, not measured output.
Provisioning uses F4a's `provision_synthetic` interface with scope 1. Firmware
reports SimulatedReady, electrical qualification Unavailable, no sensors and no
observed pulse count. No electrical/optical evidence is manufactured.

## Discovered Studio defects and narrow corrections

The first live run failed in Host selection before dispatch because provenance
code zero was disallowed. After that correction, actual firmware discovery failed
with `Unexpected sensor inventory`. The original Host required its local fixture's
one register descriptor, while real F4a correctly advertised `sensor_count=0`.

The exact firmware-generated wire pairs are retained in
[wire-fixtures.hpp](../../tests/interop/x1-f2/wire-fixtures.hpp): initial `discovery`
records 2 and 4 (zero-based) are GET_CHANNEL_CAPABILITIES and
GET_CALIBRATION_STATUS. Their response fields are respectively sensor count zero
at offset 63 and provenance code zero at offset 44. Firmware's declared record
has Valid synthetic calibration, a nonzero token, board revision and provisioning
revision; it does not claim qualified physical provenance.

Normative D-01/D-02 permit an empty sensor inventory and define the provenance
field as opaque u16, without a nonzero restriction. The private Host now accepts
an explicitly selected provenance code zero and **still requires exact equality**;
zero is never a wildcard. SimulationOnly mode, scope 1, controller/channel/board,
calibration/readiness, revision and snapshot checks remain mandatory. A declared
zero inventory remains empty; the existing one-descriptor profile retains its
strict validation. More than one descriptor remains unsupported. The wire
registry, `valid_message()`, public API, graph and recording formats are unchanged.

No firmware divergence requiring a firmware patch was found in the tested scope.
The tests preserve a deliberate uncertain-ACK outcome: when RUN's acknowledgement
is blocked and STOP cancels it, Host does not guess a new ordinary ID. Its attempted
status lookup receives CONFLICTING_DUPLICATE for the consumed RUN ID. The Host
reports that failure; a separate exactly sequenced framed test probe verifies the
firmware's retained STOP terminal. This is explicit uncertainty, not successful
Host recovery or fabricated OFF evidence. Lost ACK with identical retransmission,
lost terminal event, lease-expired historical lookup and reboot-erased history
have separate successful/error regressions as applicable.

## Coverage and wire-capture provenance

| Requirement | Verification |
| --- | --- |
| T01 | Both implementations decode/re-encode unchanged V1–V13; independent CRC check value; maximum complete frame and byte fragmentation |
| T02 | Actual Host coherent discovery; typed firmware/Studio values; all seven queries including unavailable unbound sensor discovery |
| T03–T04 | CLAIM, ordered IDs, session/gap/conflicting-duplicate errors, snapshot/calibration/current/timing and ARM basis fences |
| T05–T06 | Real finite execution identity/timer, canonical terminal/event/lookup; lost ACK produces identical original wire retries and only one firmware execution |
| T07 | Unknown/reserved/EVENT-only IDs, opaque maximum payload, unbound/bound admission, cached exact rejection after state changes, actual Host error interpretation/correlation |
| T08 | Firmware-generated known replies followed by link mutations: length, stage, enum, undefined result and correlation remain strict; no four-byte substitute |
| T09–T10 | Real STOP-first batch, blocked ordinary receive, actual Host independent KEEPALIVE, saturated ordinary/event/diagnostic storage, lease/counter rejection and expiry |
| T11–T13 | Corrupt CRC/COBS/version/header/length/reserved/direction, partial-frame timeout/recovery, bounded buffers, interruption/closure, reboot and erased terminal |
| T14 | Existing full Studio F2/L7a/L3–L6, ABI/catalog, storage/protocol goldens and daemon recording/replay regressions retained |

The supplementary header contains 116 bounded request/response/event wire captures
produced by the passing live implementation, with exact source SHA provenance.
Live CTest regenerates the deterministic corpus and compares it to the committed
capture bytes. Regeneration is an explicit command; firmware source is never copied.
Ordinary CI runs every capture through Studio's parser and actual Host transaction
boundary, validating typed fields, errors, correlation, canonical terminal presence
and strict known layouts. The captured transport has no replacement MCU semantics.
It supplements rather than replaces the live target.

## Reproduction and validation

Obtain a clean read-only checkout at the pinned SHA. This assignment used an
isolated authenticated clone under ignored `build/e2-firmware`; firmware status
remained clean throughout. Do not select moving branch HEAD or modify firmware.

```bash
cmake -S . -B build/l2 -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=OFF \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DMANTIS_F2_LIVE_INTEROP=ON \
  -DMANTIS_F2_FIRMWARE_SOURCE_DIR="$PWD/build/e2-firmware"
cmake --build build/l2 --parallel 4
ctest --test-dir build/l2 -R '^x1-f2-.*interoperability.*$' --output-on-failure
# Explicit capture regeneration, then rebuild the supplementary consumer:
build/l2/bin/mantis-x1-f2-live-tests --write-fixtures tests/interop/x1-f2/wire-fixtures.hpp
cmake --build build/l2 --target mantis-x1-f2-reference-tests --parallel 4
```

Full configurations, sanitizer settings and OpenCV dependency/loader setup reuse
the [E1 commands](x1-f2-simulation.md#l7b-2a-e1-local-validation--2026-10-11), with
live OFF and a deliberately missing firmware path for Studio ON, live ON at the
pinned checkout for headless/sanitizers, and BUILD_TESTING=OFF without firmware.
Local results are recorded after verification below. Native ARM64 and the exact
pushed-SHA remote matrix remain independent review gates.

### Local results — 2026-10-11

All complete builds and final suites passed on Linux x86_64 Debug:

| Configuration/check | Final result |
| --- | --- |
| Studio ON, live OFF, missing firmware path | Complete build; 131/131 CTests PASS |
| Studio OFF, live ON, exact clean firmware SHA | Complete build; 129/129 CTests PASS |
| ASan/UBSan, Studio OFF, live ON | Complete build; 129/129 CTests PASS; leak detection and fatal ASan/UBSan errors enabled |
| BUILD_TESTING=OFF, missing firmware path | Complete build PASS; private Host/codec compiled; zero tests and no simulator/interoperability targets |
| Normal controller/fixture/transport/live/reference repetition | Five tests × 100 repetitions PASS |
| Final normal live regression repetition | 100 repetitions PASS |
| Sanitized live/reference/boundary repetition | Three tests × 100 repetitions PASS |
| Live captured-wire regeneration check | 116 exact firmware captures PASS |
| Supplementary captures | 116 exchanges, including 51 unknown replies and 72 errors, parsed and checked through actual Host transactions PASS |
| Missing checkout / wrong SHA opt-in configurations | Both rejected clearly as required |
| Source/binary boundaries, frozen L2 catalog, C++ format, documentation links, whitespace | PASS |

The first full sanitizer run exposed a test setup race: the pending ordinary
receiver could drain replies between one-at-a-time flood submissions. The test
now submits one bounded firmware batch, publishing all eight responses under the
short state lock before the receiver resumes. Saturation assertions remain intact;
the final full headless/sanitizer suites and 100-repeat live checks passed afterward.

The following final suite/repetition commands used the configured directories
above and the E1 loader environment. Build commands were `cmake --build` with
`--parallel 4` for each directory, including `build/debug` and `build/l7b2a-no-tests`.

```bash
ctest --preset linux-debug --output-on-failure
ctest --test-dir build/l2 --output-on-failure --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized --output-on-failure --parallel 4
ctest --test-dir build/l2 \
  -R '^x1-f2-(live-interoperability|reference-interoperability|controller|fixture|transport)$' \
  --repeat until-fail:100 --output-on-failure
ctest --test-dir build/l2 -R '^x1-f2-live-interoperability$' \
  --repeat until-fail:100 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R '^x1-f2-.*interoperability.*$' \
  --repeat until-fail:100 --output-on-failure
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py
python3 tests/contract/x1_f2_interop_boundaries.py . \
  build/l2/bin/mantis-x1-f2-reference-tests build/l2/bin/mantis-x1-f2-live-tests
git diff --check
```

## Physical gates

[L7b-2b-1](x1-f2-posix-transport.md) is FINAL ACCEPTED at
`6489c6eec05ac61f685f71df5805cc825a912b2d`,
exercising a separate Linux FD transport only in PTY tests. L7b-2b-2 is IMPLEMENTED /
AWAITING INDEPENDENT REVIEW; L7b-2b-3 is PLANNED / BLOCKED BY PHYSICAL SAFETY GATES.
Physical authorization remains BLOCKED. The existing
[hardware readiness checklist](x1-projected-light-integration.md#physical-controller-readiness-facts)
remains authoritative. No physical UART, GPIO, USB HIL, ESP8266 access/flashing,
emitter/laser control, optical/electrical measurement or independently verified
inhibit/fail-OFF is implemented or exercised. F4b-1 and later firmware runtime,
deployment and physical qualification are separate milestones.

[L7b-2b-2 Q6A readiness and serial policy](x1-q6a-uart-readiness.md) is IMPLEMENTED / AWAITING INDEPENDENT REVIEW: bounded offline/read-only evidence inspection and test-only 115200 8N1 PTY configuration. Physical authorization remains BLOCKED.
