# X1 projected-light integration — L7a

L0–L6 FINAL ACCEPTED. L7a implemented, acceptance pending. L7b–L7d and L8
planned. This is a hardware-free TEST/BENCH foundation. Physical projected-light
capability and physical laser enablement remain **BLOCKED** pending factual
controller and independent safety review. No production controller is implemented.

## Activation and isolation

With `BUILD_TESTING=ON`, Linux builds produce the dedicated
`x1-fixture-plugins/libmantis-x1-projected-fixture.so` and matching manifest. This
DSO is outside the ordinary `plugins/` directory. It uses the existing X1 plugin
ID (`org.mantis.x1`) in an isolated directory so the **actual mantisd** can exercise
its existing approved X1 path; it is an alternative fixture implementation, not
an additional scanner alongside the camera-only plugin. Its implementation/build
provenance and all simulated component IDs identify the fixture explicitly.

Activation requires all of:

- explicitly selecting that directory with `mantisd --plugins .../x1-fixture-plugins`;
- `MANTIS_X1_PROJECTED_FIXTURE=1` (exact value);
- a nonempty `MANTIS_X1_FAKE` scenario;
- an explicit `MANTIS_X1_PROFILE` compatible with the fake backend.

The fixture links only `profile.cpp`, `media.cpp` and `fake.cpp`, **excluding
linux.cpp**. Its private acquisition configuration requires the fake backend and
has no Linux fallback. Both projected query and projected open check activation.
The production `mantis-x1.so` never queries/exposes projected light, even with all
fixture environment settings present. Two real OV9281 cameras cannot advertise
emitters. Ordinary Q6A installations load the camera-only plugin directory; even
misdirecting an installation to the fixture cannot reach electrical controls.
There are no GPIO, I2C, SPI, UART, USB-controller, MCU, PWM or current/power-control
operations in this work. Simulation commands change only instance-local memory.

Reproduce the software path (never use the physical profile for a fixture):

```bash
MANTIS_X1_PROJECTED_FIXTURE=1 MANTIS_X1_FAKE=normal MANTIS_X1_FAKE_PACE=1 \
MANTIS_X1_PROFILE="$PWD/tests/fixtures/x1-projected/profile.json" \
MANTIS_TOKEN=x1-projected-fixture-token \
./build/debug/bin/mantisd --plugins "$PWD/build/debug/x1-fixture-plugins" \
  --project "$PWD/build/debug/X1Fixture.mantis"
```

Use the unchanged L5 `projected devices`, `validate`, `start`, `status`, `stop`,
`cancel`, mapped bundle and replay controls. The integration test constructs the
strict typed program through the C++ client rather than introducing another input
format. Fault scenarios `prepare`, `start`, `block`, `delay`, `cleanup` and `close`
are selected explicitly by `MANTIS_X1_PROJECTED_FAULT` in this DSO only. The
storage fault test's `delay` additionally requires an explicit
`MANTIS_X1_PROJECTED_RELEASE_FILE`; its first camera start waits for that file,
allowing deterministic installation of a real filesystem failure. File checks run
outside the abort mutex, waits are finite/interruptible, and normal runs never
use this gate. Camera
faults use the existing `MANTIS_X1_FAKE` scenarios. No fixture switch changes the
production projected capability boundary.

## Graph, identities and acquisition reuse

The parent retains `org.mantis.x1:<left bus>/<left sensor>:<right bus>/<right sensor>`.
Camera IDs remain `<parent>/left` and `<parent>/right`, with canonical `left`/`right`
roles. Their selected stream IDs are those same stable child IDs; the FrameSet
stream is `<parent>/framesets`. Physical identities are the selected backend's
`bus/sensor`, not video nodes or discovery order. The reduced fixture profile uses
bus `fixture`, the actual OV9281 sensor identities/routes, 64×48 packed Y10P,
Y10_1X10, VBLANK=196, requested 120 FPS and 5 ms software correspondence. The
production `profiles/x1-q6a.json` is unchanged at 1280×720.

The two emitters are `<parent>/fixture/L1` and `<parent>/fixture/L7`. The controller
is `<parent>/fixture/controller`, controlling exactly those two emitters. L1/L7
are presentation roles. The root declares all five children. Only free-running
camera capture and software OFF/ON dispatch are implemented. There is no hardware
trigger, state-feedback, pattern/line identity or optical attribution capability.
Watchdog, interlock and physical fail-OFF availability are **Unavailable**.
Discovery traverses the unchanged L2 typed graph adapter/validator.

`plugins/first-party/devices/x1/acquisition.hpp` contains the existing private X1
acquisition implementation shared by both DSOs. The executor owns one private
Device directly; it never opens another public MantisAcquisitionV1 handle or
starts a second camera acquisition. The existing assign/setup/start/next/pairing,
packed pixels, native counters, metadata, clock IDs, calibration references and
finite pending queues are reused. Native counters remain camera-local and never
become trigger identities. The accepted correspondence tolerance is unchanged.

Both interfaces in a DSO arbitrate the stable parent ID through one RAII ownership
set. L5's existing parent gate also rejects camera→projected, projected→camera and
projected→projected conflicts before opening conflicting resources. Rejections
leave the original capture and counters intact. Successful destroy releases the
parent; refused destroy retains the handle and ownership until quiescence/retry.

## Finite execution, evidence and abort

The fixture accepts both selected cameras, both emitters and its sole controller,
1–32 declared steps, at most 128 expanded instances and 256 commands/events,
64 MiB total budget, one in-flight capture and one pending L3 bundle. Run/ON/step
caps are 30/5/2 seconds; maximum call timeout is 1000 ms. These are finite software
fixture bounds, not hardware protection/timing claims. It rejects nonzero settling,
hardware-trigger modes and any evidence requirement beyond commanded-only. It
reserves conservatively for selected pixel payload and bounded metadata before
preparation. There is no background scheduler, reconnect or retry policy.

The prepared operation cursor realizes one finite step per successful `next`;
NOT_READY retains its one dispatched operation without repeating commands. L3
remains authoritative for preflight/order, causal coverage, identities, deadlines,
ON duration, cancellation precedence, resource accounting and terminal decisions.
The exercised program is DARK/L1/DARK/L7/DARK, repeated finitely, with two camera
images per captured step and a final evidence-only completion publication.

Each command establishes only an actually recorded software dispatch. Controller
acknowledgements, observed state, exposure intervals, per-camera exposure-effective
state and physical trigger evidence are **Unavailable**. Terminal command state
can be **Unknown**; established OFF is a software command value, never optical
OFF. Source timestamps/received values, native sequences and headers remain exact.
Fake receive values identify their synthetic native clock, not a measured host
arrival. Stream generations identify each fixture camera start separately from
execution generation; publication/dispatch uses host monotonic with the execution
generation. The exact supplied program reference (including three-way hash/content
presence) is owned and echoed without manufacturing a replacement hash or artifact.
Source calibration headers are retained as original references; no active calibration
or exposure facts are invented. Existing L5 calibration binding stays authoritative.

Abort has a separate short control mutex, inhibits future ON work, fences pending
work, sets both logical emitters OFF and wakes the fixture's pending wait. It never
waits for camera frames, an ordinary callback, storage, preview or L3 queues. Camera
next uses short bounded waits and an interruptible readiness backoff, so its pending
operation observes inhibition promptly. An already-entered synchronous callback may
finish; its state stays pinned. No later callback/command enters after the fence.
Stop/destroy inhibit and wait only within their deadline; destroy refuses while a
callback is in use. The first initiating failure is retained separately from cleanup
failures, including simulated failed cleanup. None of these outcomes verifies optical
OFF, an interlock, a physical watchdog or certified fail-OFF.

## Recording, replay and resource evidence

The actual daemon/client path is discovery → typed validation → durable MRUNHDR3 →
L3 prepare/start → unchanged X1 FrameSets plus software dispatch evidence →
AcquisitionBundle → RawCapture schema 3 → exact daemon outcome → FINALIZED artifact
→ deterministic replay. Failed prepare/start produce finalized inspectable zero-bundle
captures. Storage failure uses L5's priority `recording_fault`, records RESOURCE_LIMIT,
and remains RECOVERABLE with no invented final outcome. Control traffic contains only
bounded DTOs/references; pixels remain on the mapped data plane without image encoding.

`x1-projected-fixture` tests guards, typed graph, stable identity under video renumbering,
pixel/native/header preservation, both ABI ownership directions, pending-camera and
pending-fixture abort, an entered late callback, destroy refusal/lifetime, generation
fencing, first failure, cleanup/close faults and ten repeated start/stop cycles.
`x1-projected-end-to-end` runs real mantisd and L5 C++ calls for success, invalid
participants/modes/controller selections/programs, prepare/start rejection, disconnect,
timeout, active stop, pending cancellation/shutdown, duplicate/idempotent requests, stale tokens,
all ownership conflicts and actual filesystem recording failure. After daemon shutdown,
two independent Store replay passes compare every canonical bundle byte, including
pixels/timing/evidence and header/outcome identity, with fixture switches removed.
Pending shutdown retains the exact stop/OFF outcome; if its asynchronous finalization is cancelled, explicit recovery preserves that outcome byte-for-byte. Existing L2/L3/L4/L5/L6 and camera-only regressions remain required.

Copy instrumentation increments at the actual existing acquisition memcpy. Five
64×48 Y10P stereo captures measure **10 copied images / 38,400 bytes**, one copy per
image. Borrowed semantic conversion retains those exact published buffers: **zero
additional projected pixel copies**, no extra frame queue, no image encoding.
The camera pending capacity remains two per camera; the existing L3 queue capacity
is one. Evidence is bounded by selected participants and one current publication.

Final focused x86_64 Debug hardware-free measurements: ten five-frame start/stop runs,
50 FrameSets in 0.376370 s (132.848 FrameSets/s, including immediate first frames);
actual daemon 20 captured FrameSets plus terminal in 0.228886 s (87.3798 FrameSets/s,
including control/recording/finalization). These short 64×48 fixture observations
are informational, not sustained throughput or real Q6A performance measurements.
## Local final validation

Linux x86_64 Debug, complete applicable non-hardware CTest:

| Build | Result | Wall time |
| --- | --- | --- |
| Studio OFF, cached `build/l2` | 120/120 PASS | 110.16 s |
| Studio ON, `build/debug` | 123/123 PASS | 125.31 s |
| ASan/UBSan, cached `build/l2-sanitized` | 120/120 PASS | 304.88 s |

All required frozen L2 ABI/catalog, projected contract/sequencer/RawCapture/control/
calibration-binding, acquisition/GREY/Y10P/QoS, X1 pairing/fake pacing, L6 processing/
observation, storage golden and protocol regressions run in those full suites.
The Studio build additionally runs its Qt/QML checks. No hardware is accessed.
Architecture boundaries, `git diff --check` and clang-format verification pass.
Sanitizer leak detection and UBSan halt-on-error are enabled; no suppressions or
sanitizer reports. Five additional repetitions of each new fixture/daemon test
exercise pending abort, callback lifetime, shutdown/recovery, ownership and repeated
start/stop with the final test harness.

Reproduction with the available worktree-local OpenCV configuration/cache:

```bash
cmake -S . -B build/l2 -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2 --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --test-dir build/l2 --output-on-failure
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/l2-deps/opencv"
cmake --build --preset linux-debug --parallel 4
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --preset linux-debug
cmake -S . -B build/l2-sanitized -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l2-sanitized --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --test-dir build/l2-sanitized --output-on-failure
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu OPENCV_OPENCL_RUNTIME=disabled \
  ctest --test-dir build/l2-sanitized -R '^x1-projected-' \
  --repeat until-fail:5 --output-on-failure
python3 tests/contract/boundaries.py .
git diff --check
```

The cached builds are Debug with Studio OFF, and `build/l2-sanitized` has
`MANTIS_SANITIZE=ON`. Selecting the system library family resolves the existing
mixed-OpenCV symbol error in the calibration evaluator; disabling the optional
OpenCL loader avoids the existing NVIDIA loader leak. Neither changes acquisition,
lossless semantics, tests or dependency sources. An early storage fault test raced
its fixed delivery delay; the explicit release gate now installs the failure before
any camera frame. The test waits for the authoritative RECOVERABLE transition.
Shutdown recovery follows the accepted asynchronous finalization contract and
compares the persisted outcome before/after explicit recovery.

Native ARM64 and the remote five-job matrix remain an independent acceptance gate
for the pushed SHA; local x86_64 results do not establish remote CI acceptance.
Frozen public ABI/protocol numbering, MANTIS01/02/03, MRAWREC2/3, MRUNHDR3,
MRUNOUT3, MLOBS001 and MSEM0001 implementations/fixtures are unchanged.

## Physical controller readiness facts

Classification describes repository evidence, never software fixture success.
The [Q6A acquisition record](x1-q6a-acquisition-validation.md) verifies camera-only
mode/setup and software correspondence. It explicitly defers physical trigger and
optical skew. [ADR-040](../adr/040-projected-light-programs-and-safe-state-ownership.md)
and [v0.4 §14](../architecture/v0.4-laser-acquisition.md#14-hardware-integration-boundary-and-facts-required-for-l7)
provide requirements, not an electrical controller specification.

| Fact | Status | Existing evidence / missing evidence |
| --- | --- | --- |
| Actual laser-controller model/revision | UNKNOWN / NOT PROVIDED | No board/controller/firmware identification |
| Controller transport | UNKNOWN / NOT PROVIDED | No selected transport/protocol |
| Physical channel-to-emitter mapping | UNKNOWN / NOT PROVIDED | L1/L7 software presentation roles do not specify channels |
| Electrical interface, levels and polarity | UNKNOWN / NOT PROVIDED | No schematic/pinout/enable polarity/isolation facts |
| Reset/boot output behavior | UNKNOWN / NOT PROVIDED | Requires independently observed physical behavior |
| Controller disconnect behavior | UNKNOWN / NOT PROVIDED | Requires transport/power-loss output characterization |
| Command/acknowledgement contract | UNKNOWN / NOT PROVIDED | No framing/version/ack stage/error semantics |
| Independent OFF/inhibit path | UNKNOWN / NOT PROVIDED | No implementation or independence evidence |
| Watchdog/interlock implementation | UNKNOWN / NOT PROVIDED | Required by ADR-040; not supplied |
| Measured OFF latency | UNKNOWN / NOT PROVIDED | No instrumented physical measurements |
| Emitter output readback | UNKNOWN / NOT PROVIDED | No electrical/optical sensing specification |
| Hardware trigger wiring | UNKNOWN / NOT PROVIDED | Camera record explicitly defers physical trigger |
| OV9281 trigger configuration | UNKNOWN / NOT PROVIDED | Verified free-running configuration is not trigger-mode evidence |
| Optical timing measurement method | UNKNOWN / NOT PROVIDED | No instrument/coverage/uncertainty procedure |
| Camera-only 1280×720 Y10P / VBLANK=196 / 5 ms correspondence | VERIFIED | Accepted dated real Q6A record; does not verify illumination or hardware sync |
| Program/control/independent safety responsibilities | DOCUMENTED BUT UNVERIFIED | ADR-040/§14 obligations have no matching physical controller validation |
| Fixture logical channel mapping and OFF result as hardware evidence | NOT APPLICABLE | Synthetic memory state only |

Before L7b the user must provide and reviewers must accept:

1. Controller/board revision, firmware build, datasheets/schematic and physical
   resource ownership/topology, with exact L1/L7 channel mapping.
2. Transport/protocol specification, framing/version, command IDs/generations,
   acknowledgement stages, errors, reset/reconnect/disconnect behavior and bounds.
3. Pinout, levels/isolation, enable polarity, valid combinations, current/power
   limits and measured boot/reset/power-loss output states.
4. Independently enforced OFF/inhibit, watchdog/interlock design and instrumented
   fault tests that remain effective when daemon/control transport is unavailable.
5. Instrumented OFF latency, readback availability/scope, ON/settle/duty limits and
   measurement uncertainty; requested limits are insufficient evidence.
6. Trigger wiring/polarity/pulse semantics, actual OV9281/driver trigger-mode
   support and readiness, native association/counter/clock semantics.
7. Optical timing/exposure-overlap method, instruments, software/hardware versions
   and retained evidence, including the scope and uncertainty of each claim.

L7b physical integration stays BLOCKED until these facts and independent safety
behavior are reviewed. L7c/L7d/L8 and production extraction/triangulation are outside
L7a. Independent Technical Lead review is required before L7a acceptance.
