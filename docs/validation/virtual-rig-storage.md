# Virtual rig Recorder and storage qualification (SIM-01B)

This Linux-only utility uses the compiled `virtual-rig` acquisition plugin,
public `MantisAcquisitionV1` adapter, production `device::Session`, unchanged
32-entry LOSSLESS queue and 50 ms failed-admission policy, actual schema-2
segmented Store, explicit recovery and recorded replay. The isolated executable
compiles Session and the acquisition adapter with private observers and links
the existing fault-enabled Store. Symbol contracts prove these observers and
fault controls absent from product archives/executables. Public SDK layouts,
source timing/ownership, artifact schemas and durability operations are unchanged.
The existing `virtual-rig-daemon` and storage daemon tests retain public-client
coverage; the integrated startup case additionally checks `services::Runtime`'s
retained failure evidence and absence of a successful capture handle.

## Deterministic registered functional regression

```sh
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --test-dir build/headless -R '^virtual-rig-storage$' --output-on-failure
```

There is still one registration, with its existing 40-second external guard.
It covers community 2×640×480 GREY and intermediate 2×1280×720 Y10P integrity,
plus small 2×64×48 stereo healthy/transient/prolonged policy cases and public
Runtime startup failure. Configured 30 FPS defines **logical exposure cadence**,
not measured wall-clock throughput. Nominal image payloads remain 18.432 and
69.12 MB/s. Legacy single-camera 64×48 GREY8×30 (0.09216 MB/s), schema-1
acceptance and schema-2/3 compatibility retain their existing tests.

The qualification-only adapter observer selects the existing plugin-private
`ClockV1` on the **same instance** that Registry opens. It parks before `next()`;
the coordinator observes that state, advances exactly one exposure, and waits
for actual Session admission or Store callback progress. Source capacity, pending
queues, real 50 ms `push_for`, synchronous Recorder callback and actual Store
operations are unchanged. Clock credits grant acquisition calls, not fabricated
packets. Every packet still traverses the compiled renderer, SDK buffer adapter,
Session queue and segmented writer. All gates are released before thread/future
joins on success and exception unwind.

A deliberate coordinator suspension lasts `(source_capacity + 1) / logical_FPS`
(300 ms for the small reference), exceeding the old eight-event wall-clock
buffering budget. Source-park entry is observed first. After this injected wall
interval, logical time, exposure/publication counts and source overruns must be
unchanged; subsequent exact capture/replay still passes. The interval specifies
the descheduling fault, not scheduler ordering or a product FPS oracle. No CPU
quota, affinity, global clock or infrastructure settings are changed.

Transient/prolonged cases enter the actual `raw-record-before` Store checkpoint.
Transient delivery fills four real queue entries, releases with 28 slots still
available, drains and continues. The prolonged case fills 32, attempts one more
admission and observes its real 50 ms refusal, first cause, inactive producer and
still-blocked writer. Storage is released **before** stop/join; stop is not
asserted to preempt synchronous I/O. A callback success is not a per-frame fsync.
Failed capture is OPEN until explicit abandon makes it RECOVERABLE; explicit
recovery finalizes its verified prefix, not a successful failed scan.

Five-second observation guards prevent infrastructure hangs and contain no
nominal-FPS oracle. An expired guard without an established capture cause is
UNQUALIFIED (exit 2, never a silent PASS/skip); product and runner require
investigation. An already retained acquisition/Recorder/Store cause keeps FAILURE
precedence. Corruption or a failed invariant is FAILURE, including under load.
The ordinary CTest's exit 0 means its functional and expected negative assertions
passed, **not** that controlled source time establishes performance qualification.

The existing virtual-rig held-callback/finite-buffer controls separately test
source backpressure and overruns. Existing storage durability tests retain
ENOSPC/EIO, partial-write, flush/sync, publication/rename, SQLite, process-kill,
incomplete/corrupt-tail and recovery cancellation/retry coverage. This utility
does not duplicate those families or modify their fault semantics.

## Integrity and bounded history

An independent analytic raster/explicit RAW10 quartet oracle checks every pixel,
length, packing, dimension/stride, child identity/order, native sequence,
generation, calibration, coordinate, trigger and clock provenance. A restart must
change generation without changing retained history. Every committed packet has
an exact canonical **structure** snapshot: all headers, metadata, schemas and
attribute descriptors, with pixel spans normalized to readable zero-length views.
Actual pixel lengths and every pixel byte are checked independently both at
capture and on **each** replay. This decomposition compares all serialized content;
it is not a digest/count-only check. The first 128 complete source packets remain
retained, immutable and compared through the original full canonical byte oracle
on both passes as well. Segment scans independently verify membership, order,
boundaries and recovery. All retained/weak packet owners must retire on cleanup.

Structure snapshots are capped at 32 KiB each, at most 64 MiB for 2,048 packets.
Full payload retention stays at 128 FrameSets / 294,912,000 image bytes. Later
benchmark payloads are validated before release; bounded structure/scalar history
supports both replay passes without retaining gigabytes of images. Validation
latency is measured separately after actual Store append; callback-success rate
includes that work. Callback/append latency distributions identify their scope
and sample counts. Stage arrays remain bounded and overflow is a failure.

Ordinary native sequences remain strictly contiguous. Only real-time opt-in
runs with explicitly reported source loss can verify a selected native prefix:
each gap must be covered by persisted overrun counters; every selected identity,
timestamp, trigger, pixel and structural byte still agrees. Such loss prevents
QUALIFIED. Source exposure/pairing/overrun counters and Recorder refusal are
separate, conserved quantities. Source exposures can continue while the current
Session stop order joins/drains its writer; pre-stop and final counters are
reported separately. Replay never invents or re-pairs historical observations.

## Four qualification outcomes and negative sensitivity

| Outcome | Meaning | Exit |
| --- | --- | ---: |
| QUALIFIED | Integrity/recovery pass, no unexpected source/Recorder loss/error, and both measured windows meet both reference rates | 0 |
| UNQUALIFIED | Insufficient/sub-target measurement, controlled-source functional case, source/host limitation or inconclusive observation guard; no reference-rate acceptance | 2 |
| FAILURE | Unexpected acquisition/Recorder/Store error, corruption or invariant failure; recoverable prefix does not change this verdict | 1 |
| EXPECTED_SATURATION_POLICY_PASS | Deliberate overload proves explicit LOSSLESS refusal, accounting, exact recoverable prefix and cleanup; no performance qualification | 0 |

The regression exercises actual verdict paths: an overrun-free four-frame capture
with a held actual append remains short/sub-target and UNQUALIFIED; a real Store
EIO checkpoint after two successes remains FAILURE after exact prefix recovery;
the prolonged overload remains EXPECTED_SATURATION_POLICY_PASS. Standalone modes
also expose their actual CLI exits (expected 2 and 1 respectively):

```sh
build/headless/bin/mantis-rig-storage-qualification control-short \
  build/headless/plugins/virtual-rig.so profiles/virtual-rig-trigger.json
build/headless/bin/mantis-rig-storage-qualification control-writer-error \
  build/headless/plugins/virtual-rig.so profiles/virtual-rig-trigger.json
```

Raw first-cause diagnostics stay in private execution evidence; inspect/redact
JSON before sharing. Public verdict descriptions are bounded and must not turn
unexpected I/O failure into a CPU/source limitation. Secondary inspection or
cleanup diagnostics do not replace an established first cause.

## Opt-in measured reference and bandwidth collapse

Release capacity measurements use real source time; ordinary Debug/sanitizer
functional tests do not. No new CI job, runner or heavy CTest matrix is added.
Supported build architectures are Linux x86_64/ARM64; actual execution evidence
must identify its architecture independently.

```sh
cmake -S . -B build/rig-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMANTIS_BUILD_STUDIO=OFF -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/rig-release --parallel 4 --target mantis-rig-storage-qualification
mkdir -p /tmp/mantis-rig-qualification
# Select a disposable parent on the filesystem being measured.
timeout 180s build/rig-release/bin/mantis-rig-storage-qualification benchmark \
  build/rig-release/plugins/virtual-rig.so profiles/virtual-rig-trigger.json \
  /tmp/mantis-rig-qualification > /tmp/rig-throughput.json
timeout 60s build/rig-release/bin/mantis-rig-storage-qualification collapse \
  build/rig-release/plugins/virtual-rig.so profiles/virtual-rig-trigger.json \
  /tmp/mantis-rig-qualification > /tmp/rig-collapse.json
```

Reference: 2×1280×720 Y10P×120 = **120 FrameSets/s / 276.48 MB/s actual image
payload**, excluding container/metadata/checksum/filesystem overhead. After one
second of warmup, benchmark uses two consecutive fixed five-second steady-clock
windows. Each must contain at least **600 successful callbacks AND 1,382,400,000
actual image bytes**. Counts are independently accumulated from successful
callback timestamps and actual buffer sizes. Warmup and post-window drain are
excluded from qualification; summary phases and durable publication are separate.

There is **zero allowed measured rate deficit**, not a guessed runner tolerance.
One boundary frame gives 0.2 FPS / 0.4608 MB/s resolution per window (about 0.167%
of nominal); this is reported uncertainty, not a relaxed threshold. Five seconds
contains 600 nominal frame periods, over thirteen conservative recording-buffer
budgets (8 source + 2 pairing + 1 plugin inflight + 32 Recorder + 1 producer +
1 writer = 45 FrameSet opportunities, some overlapping). Thus a short no-overrun
capture or a finite-buffer burst cannot replace the measured rate requirement.
Two windows show only that bounded interval; they are correlated observations,
not a statistical confidence interval, long-soak guarantee, or proof of SSD cache
exhaustion/physical scanner performance. An otherwise correct nominal runner can
be UNQUALIFIED at a boundary or under contention; there is no retry-until-green.

The benchmark caps 2,048 packets, artifact output below 5 GiB and requires 6 GiB
free before starting. Collapse/functional cases cap 128 packets and 512 MiB output;
free-space preflight is 1 GiB/512 MiB respectively. Full pixel history is capped
at 294,912,000 bytes in every mode; add the actual bounded queue/inflight images,
up to 64 MiB structure history, metadata, replay mapping, allocator and code/stack
cost. Payload bounds are not an RSS guarantee: report measured steady/peak RSS
and provision memory headroom (especially for sanitizers). Per-packet replay
backing and temporary canonical comparisons remain bounded. The 180-second
benchmark guard covers the larger recording/replay workload; timeout invalidates
the run. No product enqueue/queue/deadline/durability bound is increased.

Opt-in segments target 8 MiB versus the production 64 MiB default (ordinary
real-size 1 MiB / latency 64 KiB). All publication/sync overhead remains included.
Collapse starts with ordinary service, then gates successive actual segment syncs
on a 150 ms service schedule (~61.44 MB/s nominal payload for four frames per
segment). It measures actual pre/post service, queue trajectory, first refusal,
source loss, drain, artifact state and cleanup. It models a service deficit, not
real SSD pseudo-SLC exhaustion or thermal behavior. Its healthy phase may already
be source-limited; never call that full-rate baseline success. Unexpected I/O is
FAILURE, not an expected saturation pass.

## Metrics, clocks, environment and cleanup

JSON version 2 records actual bytes/FPS and fixed window boundaries, append and
admission p50/p95/p99/max/counts, per-frame validation overhead, source/Session
accounting, queue trajectory/high-water, Store stages, visible segment writes,
indexed durable bytes/chunks, finalization/recovery, retained/mapped buffers,
CPU, sampled RSS/FD/thread peaks and restored cleanup baselines. Whole-process
CPU uses one logical core = 100%; capture RSS includes validation history and
full-process high-water includes replay. FD probes include temporary probe FDs.
Store service includes encoding/hash/write/sync, not exclusive device I/O time.

Bounded before/after runner snapshots include affinity, visible cgroup CPU quota
and throttling, CPU/memory/I/O pressure, memory limits/current use and OOM events.
Unavailable own-cgroup mappings/probes are explicit; snapshots are not atomic or
proof another process caused a failure. Rate/source/service evidence must remain
separate from environment hypotheses. Run capacity measurements serially without
competing builds/tests. No global scheduler/cgroup/security changes are needed.

Fixture/admission/callback/Store wall-clock stamps share steady-clock origin.
Controlled-source receipt/reference/native clocks are separate and never treated
as measured wall-clock FPS. Live counters are separately sampled, not an atomic
cross-layer snapshot. Callback success is distinct from segment sync, SQLite
publication, finalization and power-loss durability. Source clocks/epochs remain
unchanged through persistence/replay. A stalled synchronous operation need not be
preempted by the 50 ms admission budget; no Studio response or emitter/interlock
claim follows from this software fixture.

Instrumentation uses bounded arrays, coordinator resource sampling at most 20 Hz,
a 10,000-call admission microprobe, 1,000 passive Store checkpoints and one identical
packet through ordinary/qualified streams. The stream probe compares every byte;
it excludes fsync/capture concurrency and is not a whole-product baseline.
The wrapper caches identity and bypasses lookup only without armed write controls;
existing partial-write/fault tests retain their assertions.

Each run creates/removes only its fresh random child directory under the selected
parent. Gate release and callback/thread retirement precede deletion. JSON files
redirected by the caller are explicit retention; a killed run may leave its exact
owned child for inspection. Never use broad cleanup. No user data, hardware, GPU,
proprietary Qt, cloud or paused work is accessed. Hardware/long-soak/native-platform
claims need separate matching evidence. Historical #12 causes remain OPEN.
