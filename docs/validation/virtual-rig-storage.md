# Virtual rig Recorder and storage qualification (SIM-01B)

This Linux-only qualification uses the compiled `virtual-rig` acquisition plugin,
its public `MantisAcquisitionV1` adapter, production `device::Session`, unchanged
32-entry LOSSLESS queue and 50 ms failed-admission policy, and the actual schema-2
segmented Store and replay source. The isolated qualification executable compiles
Session with a private admission sampler and links the existing fault-enabled
Store. Product targets, SDK ABI, artifact encodings, fsyncs and runtime ownership
are unchanged. `virtual-rig-daemon` separately exercises the real daemon and public
SDK. The new service-startup case exercises `services::Runtime` and its retained
public failure evidence without a client owning capture.

## Registered regression

```sh
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --test-dir build/headless -R '^virtual-rig-storage$' --output-on-failure
```

One registration covers 2×640×480 GREY and bounded 2×1280×720 Y10P at 30 FPS
for real-size integrity, plus 2×64×48 GREY stereo at 30 FPS for the healthy,
transient, prolonged and public-service startup latency-policy controls. The
latency fixtures use the same compiled plugin, production Session, actual Store
I/O and complete byte/provenance/replay assertions. Their small payload isolates
latency policy from the sustained writer-service deficit that unoptimized
sanitizer encoding/hashing/filesystem work can introduce. It does not qualify
real-size sustained throughput; the real-size matrix and full-reference opt-in
workloads provide their separately stated evidence. Nominal uncompressed
payloads are 18.432 and 69.12 MB/s; configured rates are not measured throughput. The legacy 1×64×48 GREY8 at 30 FPS (0.09216 MB/s)
and schema-1 acceptance remain in their existing registrations. Existing
`virtual-rig-contract`, `virtual-rig-runtime`, `virtual-rig-daemon`, `raw-capture`,
`storage-legacy-golden`, `projected-storage-durability` and the actual X1
post-commit Store-failure case retain their separate contracts. This test does
not replace or duplicate the complete syscall-fault, crash, corrupt-tail or
recovery-cancel/retry families in `tests/storage`.

The transient/prolonged gates acknowledge entry immediately before a real
schema-2 record append, using the private Store checkpoint mechanism. The added
`raw-record-before` point compiles out of product targets; first-append and
segment-sync checkpoints retain their meanings. The transient case observes four additional source publications and positive queue
growth, then releases while the measured remaining queue budget is positive.
The prolonged case observes 32 queued entries, the refused admission, retained
first error, inactive producer and still-running writer. Release always precedes
stop/join, including assertion unwind. No assertion requires stop to cancel
synchronous storage. Subsequent drain must account for exactly one explicitly
refused FrameSet; it must never count that FrameSet as callback success.

The existing `virtual-rig-contract` callback-held and finite-buffer tests remain
responsible for source backpressure/descheduling independently of Store pressure.
They observe actual compiled-plugin callback entry, then advance the modeled
exposure clock and assert exact overruns/inflight ownership before release.

The independent pixel oracle builds the analytic raster without calling the
producer renderer/model/packer; RAW10 quartets use an explicit four-sample
formula. Every retained image and both replay passes compare all bytes,
dimensions/strides, source sequence, child identity/order, generation, trigger,
calibration and clock/receipt provenance. Canonical packet bytes are compared
against retained originals in both replays. Segment scanning independently
checks ordered record membership and boundaries. Restart must change generation
without changing retained history. Failed capture stays OPEN until the
daemon/fixture explicitly abandons it;
`Store::abandon` sets RECOVERABLE synchronously. Explicit recovery finalizes its
verified prefix; reopening also preserves its RECOVERABLE state.
A recoverable prefix is not a cleanly completed failed scan.

Source exposure, delivery, pairing and Recorder refusal are separate counters.
The current stop order retires the Session producer and drains/joins its writer
before stopping the underlying source. Source exposures can therefore continue
through that drain. The fixture asserts no source overruns before stop in healthy
cases, checks every committed native sequence without gaps, and reports both
pre-stop and final source counters. For a prolonged failure it additionally
requires zero overruns at gate release. Source overruns/shutdown-unmatched
observations after acquisition retirement are explicitly accounted and are not
invented historical exposures or silently conflated with the refused FrameSet.
No physical emitter/interlock, Studio response-time or shutdown-deadline claim is
made.

## Opt-in measured full reference and bandwidth collapse

Use Release for capacity measurements; ordinary Debug/sanitizer runs remain
functional/data-integrity evidence. These modes are not CTest registrations and
add no workflow or runner. They support the existing Linux x86_64/ARM64 build
architectures; actual execution on each architecture must be reported separately.

```sh
cmake -S . -B build/rig-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMANTIS_BUILD_STUDIO=OFF -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/rig-release --parallel 4 --target mantis-rig-storage-qualification
mkdir -p /tmp/mantis-rig-qualification
# Choose a disposable parent on the filesystem being measured; default is TMPDIR.
timeout 60s build/rig-release/bin/mantis-rig-storage-qualification benchmark \
  build/rig-release/plugins/virtual-rig.so profiles/virtual-rig-trigger.json \
  /tmp/mantis-rig-qualification > /tmp/rig-throughput.json
timeout 60s build/rig-release/bin/mantis-rig-storage-qualification collapse \
  build/rig-release/plugins/virtual-rig.so profiles/virtual-rig-trigger.json \
  /tmp/mantis-rig-qualification > /tmp/rig-collapse.json
```

The reference profile is 2×1280×720 Y10P at 120 FPS: **276.48 MB/s nominal image
payload**, excluding record headers, metadata, checksums and filesystem/SQLite
work. Each run retains at most 128 source packets (294,912,000 image bytes), uses
fixed admission/callback/stage arrays, limits output below 512 MiB and checks at
least 1 GiB free space before starting (512 MiB for regression). Mapped replay
storage is bounded per segment; canonical comparisons temporarily allocate two
packet-sized strings. Expect additional allocator, Qt-free service, plugin,
metadata and sanitizer overhead; payload bounds are not an RSS guarantee.
Do not run concurrent build/test campaigns during a capacity measurement.
CPU scope is the whole process, with one logical core equal to 100%.

The healthy run stops after 80 callback successes or an explicit acquisition
failure. The first eight callbacks are warmup. JSON identifies the actual
steady-clock measurement boundaries, sample counts, recorded image bytes,
callback FPS/MB/s, per-camera observed delivery FPS, acquisition diagnostics,
callback/admission latency p50/p95/p99/max, queue trajectory, Store stage times,
CPU, sampled RSS/FD/thread peaks, retained buffers, observed mapped-packet backing,
output bytes and cleanup. Independent stat samples at held checkpoints or after
joins measure visible segment bytes (OS-visible writes, not durable bytes).
Indexed durable segment bytes/chunks before finalization are reported separately.
Degraded callback service ends at the first failed admission; subsequent drain
bytes are a separate phase.
Store encoding/write/hash/sync work is included in callback wall time; this is
actual Store service throughput, not a claim of exclusive kernel/device I/O time.
Directory bytes include metadata and are distinct from image payload bytes.
The fixture uses **8 MiB segment targets** for the opt-in modes (1 MiB for real-size
integrity and 64 KiB for latency controls in the short regression), increasing
publication frequency relative to the production 64 MiB default; this overhead must accompany every reported result.

Collapse starts with healthy service, then gates successive actual segment-sync
operations on a monotonic 150 ms service schedule (~61.44 MB/s image payload for
four FrameSets per segment, below the 276.48 MB/s requested source payload).
The healthy boundary and degraded windows are explicit. This models a sustained
service deficit after healthy operation, not merely a constant slow writer or
real SSD pseudo-SLC exhaustion/thermal throttling. It preserves source rate,
timestamps, queue capacity/deadline and durability. The required outcome is
explicit accounted saturation and prefix recovery, never successful full-fidelity
recording under an intentionally insufficient bandwidth budget.

Admission samples time the real `push_for` call; they include call/lock/scheduling
cost and the queue's wait, not an inferred delay from callback duration. Callback
success is distinct from `segment-synced`, publication/SQLite `chunk-committed`
and finalization/recovery. Store stage timestamps, fixture samples and source
`std.chrono.steady_clock` receipt/origin metadata share the same documented host
clock. Native/reference epochs and Session-relative diagnostic fields are kept
separate. Live counters are sampled separately and are not an atomic snapshot.
Resource sampling is rate-limited to 50 ms on coordinator progress checks, outside
the callback. JSON reports its elapsed cost/sample count and a 10,000-call
admission-sampler microprobe and a 1,000-checkpoint passive Store-trace microprobe.
Each case also serializes one identical retained packet to ordinary and qualified
streams in the same owned directory, compares the complete bytes and reports both
elapsed times. This probe excludes fsync and capture concurrency; it measures
stream-wrapper overhead, not whole-capture capacity. The wrapper caches file
identity at open and bypasses fault lookup when no write fault is armed. Existing
partial-write/fault controls still exercise the armed path. These measurements are
not zero-overhead claims or substitutes for a controlled whole-capture baseline.

Exit 0 means the requested assertions passed (a deliberate collapse is a negative
policy test, not sustained-rate success). Benchmark exit 2 reports an explicitly
unqualified host/rate limitation with metrics. Only this opt-in limitation path
may verify a noncontiguous native camera prefix: every selected native identity,
exposure timestamp, trigger and byte must agree, native order must increase, and
each observed gap must be covered by persisted source-overrun counters. FrameSet
publication order and both replay byte comparisons remain exact. Such a result
never claims a full-fidelity recording. Ordinary regression native sequences
remain strictly contiguous. Exit 2 is not a performance PASS; exit 1 is a failed invariant,
preflight or bounded scheduling observation. Timeout is a failed/incomplete run,
never PASS. Slow storage/CPU must be reported as a limitation rather than changing
source rates or weakening assertions. Internal observation/checkpoint deadlines
bound test hangs; they do not certify physical timing or uninterruptible-I/O
shutdown. The outer 60-second process budget is a last-resort operational bound;
its use invalidates the run.

Each run creates a fresh random child directory under the selected parent and
removes only that child through RAII after releasing gates and retiring callbacks.
No user data, hardware, GPU, proprietary Qt or cloud service is accessed. JSON
redirection is explicit retention controlled by the caller; inspect/redact it
before any public upload. A killed run may leave its uniquely named test child;
inspect that exact directory before manual removal. Do not use broad cleanup.
Results qualify only the measured host/build/filesystem and measurement window.
Historical Recorder incidents in #12 remain causally OPEN.
