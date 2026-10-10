# L6 implementation validation

L0–L5 are FINAL ACCEPTED. L6 is implemented, acceptance pending; L7–L8 remain
planned. This record describes hardware-free infrastructure verification. It does
not certify optical measurements, scanner throughput or physical laser safety.

## Reproducible checks

The original additive CTest entries are `laser-observation-codec`, `processor-v2`,
`laser-observation-artifact` and `semantic-pipeline-replay`. Review hardening adds
`observation-append-concurrency`, `semantic-registry-lifetime`,
`semantic-isolation-state` and `semantic-pipeline-memory`. The complete existing
non-hardware suite also runs unchanged. The C-only contract DSO compiles against
the unchanged public headers; native and actual plugin-host execution are tested.

The codec test compares six checked-in independent binary fixtures in both
directions. The Python reference encoder refuses to overwrite fixtures and is not
run by the tests. Existing legacy and projected storage goldens are unchanged.
Every truncated fixture prefix and every single-byte corruption is rejected;
valid-checksum malformed context/columns/masks/attribution are tested separately.

The integration tests exercise one-emission publication, invalid ABI tables and
descriptors (including a changed registered output type), callback failures,
independent output headers, retained/transferred
buffer ownership, eight concurrent invocations, isolated crash/timeout/cancellation,
scratch cleanup, explicit V1/V2 mixing, journal corruption and process death.
An actual DSO advertising both optional processor tables verifies explicit V1/V2
selection. A weak loader-owner assertion proves retained columns pin the producing
library and release that pin afterward, independently of loader NODELETE behavior.
Streaming uses 2/2 lossless queues for 64 publications, checks exact order and no
drops, and tests cancellation and failed-node queue closure. Successful N=0,
extractor_failed and extractor_unavailable remain distinct.

Two independent BundleReplay passes through the synthetic V2 producer and typed
consumer produce identical canonical observation bytes. An isolated pass agrees
with native execution. New observation artifacts retain the actual finalized raw
artifact/hash and original bundle/frame/evidence/calibration identities; the raw
capture remains unchanged. Mapped bulk backing is shared and survives Store
destruction. No active-calibration or hardware lookup is involved.

Local commands use `git diff --check`, `python3 tests/contract/boundaries.py .`,
the full Debug Studio-OFF CTest suite, repeated concurrency/isolation tests, and
the complete ASan/UBSan suite with leak detection. On this development host,
`OPENCV_OPENCL_RUNTIME=disabled` selects the CPU calibration path because its
external OpenCL ICD is unsuitable for sanitizer runs; no repository test is skipped.

## Measured performance

`mantis-observation-benchmark SYNTHETIC_DSO` measures N=0, 2, 4096 and 65,536.
It reports serialization/decode/native V2/pipeline times, allocation counts and
bytes, streaming throughput, queue high water and cancellation latency. These are
local Debug CPU fixture measurements; native V2 cost includes fixture mapping,
semantic ABI conversion and validation, rather than just a function-pointer call.

The initial allocation measurement exposed eager validator diagnostic-string
construction: a 65,536-sample V2 call performed over three million allocations.
Constructing the same diagnostic only on failure removed those per-sample
allocations without changing any validation rule. Metadata allocations are now
approximately constant across small, medium and larger sample counts. Structural
sizing reads buffer extents without traversing payloads. Mapped decoding and native
views perform no application-level bulk copy. Isolated transport explicitly writes
and maps files, with the checksum passes documented in the format specification.

One local x86_64 Debug run (GCC 15.2, AMD Ryzen AI 9 HX 370) measured:

| Samples | Encoded bytes | Serialize µs | MiB/s | Mapped decode µs | Native synthetic V2 µs | Pipeline µs | V2 allocations |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 3,336 | 350 | 9.1 | 395 | 2,091 | 2,164 | 1,361 |
| 2 | 4,100 | 295 | 13.3 | 382 | 2,159 | 2,378 | 1,785 |
| 4,096 | 114,638 | 1,320 | 82.9 | 1,383 | 4,676 | 5,059 | 1,786 |
| 65,536 | 1,773,518 | 10,335 | 163.6 | 10,549 | 29,307 | 54,903 | 1,786 |

Serialization allocates 129 metadata objects for every nonempty size above; mapped
decode allocates 340. Native V2 allocates approximately 95.6 KiB of metadata, not
1.77 MiB of columns, at the larger size. Pipeline allocations remain at 2,153.
Observed streaming rates were 456, 353, 86 and 17 observations/s respectively;
input/output high water was 2/1 with zero drops. Cancellation woke in 436 µs.
These single-host measurements vary with load and do not define acceptance timing
thresholds. An optional C pass-through DSO argument separately measures native
adapter overhead without fixture production.

The C pass-through baseline measured 44.8 µs, 110 allocations and 9.5 KiB per
native adapter call. It includes complete input/output validation and buffer
ownership conversion. It is not a bare function-pointer timing.

Initial L6 implementation validation passed:

- complete Debug Studio-OFF CTest: **52/52**, 117.83 seconds;
- complete ASan/UBSan CTest, leak detection and halt-on-error: **52/52**, 327.00 seconds;
- final ProcessorV2 repeat: five successful native runs and five sanitizer runs;
- semantic stream repeat: ten native runs and five sanitizer runs;
- architecture boundaries and `git diff --check`.

Those tests include changed descriptors, explicit dual-interface selection,
oversized files, journal accounting and loader-independent buffer lifetime checks.
No test or assertion was disabled. Frozen ABI snapshots, legacy/projected storage
fixtures, L3 sequencing and L5 control DTOs remain unchanged. Required GitHub
acceptance is the five-job matrix for the exact final pushed SHA; the completion
report records that run and every job conclusion. Green local tests alone do not
establish CI acceptance. Human L6 acceptance remains pending.

## Sanitizer CI cumulative pairing-test deadline

The initial L6 [sanitizer job](https://github.com/martinkoenig/mantis-studio/actions/runs/37992655842/job/114030546965)
passed all four new tests but timed out the existing `x1-pairing-stream` test at
30.05 seconds. That unchanged test combined 56 independent GREY/Y10P software,
hardware-mode fixture and recorder scenarios in one process/deadline. The accepted
L5 [sanitizer job](https://github.com/martinkoenig/mantis-studio/actions/runs/37926537448/job/113806717738)
took 28.39 seconds for the same test. No assertion failure or sanitizer diagnostic
appeared in the failed test log; its output reached the last direct-stream fixture
before the cumulative timeout, before the recorder scenarios began.

Five local aggregate sanitizer repeats passed in 24.60, 24.22, 23.67, 23.83 and
24.33 seconds. A two-CPU run with one bounded CPU contender passed in 22.93
seconds; it did not reproduce the CI timeout. This is evidence of an inadequate
cumulative test deadline, not evidence of a production camera/queue defect or a
verified external infrastructure failure.

CTest now registers all 56 existing scenarios individually, each with the original
30-second deadline. Every assertion, three-second acquisition/recorder wait,
lossless queue/accounting check, exact replay comparison and failure expectation
remains. The executable retains aggregate invocation for load investigation and
checks that filtered invocation selects exactly one scenario (or 56 unfiltered).
No camera/plugin/pairing implementation changed; no timeout/queue was increased,
test disabled, assertion removed or packet dropped. The non-Studio suite contains
107 tests; Studio adds three more. Repeated split-case and complete local/sanitizer
results, followed by the entire five-job matrix, are required for the final commit.

Follow-up validation passed all 56 split cases three times under ASan/UBSan
(168 invocations, maximum 1.37 seconds), while the Debug suite ran concurrently.
The aggregate invocation also passed all 56 scenarios under ASan/UBSan, retaining
the repeated-session/leak check. The complete expanded Debug suite passed
**107/107**, 120.71 seconds. The complete expanded ASan/UBSan suite passed
**107/107**, 326.29 seconds, with leak detection and halt-on-error enabled.
Architecture boundaries and diff checks passed again. The final completion report
identifies the corrected commit's five-job workflow and every required conclusion.

## Independent-review ownership/isolation hardening

The correction following reviewed HEAD `c32c6a39ae1fcb17d4db910ae142a8d7a21af2f2`
preserves all accepted codecs, golden fixtures, C tables and L3–L5 semantics.
The implementation and resource policy are described in
[LaserObservation processing](laser-observation-processing.md).

- Observation writes pause deterministically before encoding and after durable
  journal publication. While paused, get/list, an independent RawCapture-3 append,
  and another observation append/finalize complete. Same-artifact mutations return
  BUSY. Eight concurrent writers, Store-wrapper destruction during an append,
  injected journal failure and reopen/recovery preserve exact bytes/hash and one
  committed chunk.
- Retained native/isolated factories and instances fail safely after Registry
  teardown. Pending isolated calls are killed/reaped; cancellation stays separate
  from the owner token. A trusted successful callback is released by an explicit
  fixture gate after teardown and cannot publish. Retained output buffers remain
  valid. Repeated lifecycles run under leak detection.
- Isolated crash, invalid output, callback failure and deadline violation become
  observable FAILED entries with specific diagnostics. Later calls refuse the
  entry until explicit re-enable. Cancellation and malformed caller input leave
  it registered, and an unrelated synthetic processor remains usable.
- A 25-node chain uses independent 128-KiB backings and weak owners to prove last-use
  release. Fan-out keeps shared backing until its final consumer and preserves
  both terminal outputs. Tests check unchanged Storage identities, finite budget
  rejection, full backing/slice accounting, uint64 overflow, cancellation and
  repeated execution without accumulated ownership.

Both local Studio-OFF builds passed the complete non-hardware suite: **111/111
Debug (129.17 seconds)** and **111/111 ASan/UBSan (374.96 seconds)**. Sanitizers used
`detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`. The four new
tests each passed ten consecutive runs with two CTest workers, in Debug and under
ASan/UBSan: **40/40 invocations per build**. Architecture boundaries and
`git diff --check` passed. No physical hardware was used.

Per the current CI handoff policy, commit/push follows successful local verification.
The final report links the pushed commit's workflow as PENDING; the independent
reviewer verifies all five remote jobs before acceptance. L6 remains acceptance
pending, and L7 has not started.

## Fake-camera host pacing follow-up

The [reviewed sanitizer run](https://github.com/martinkoenig/mantis-studio/actions/runs/38001379898)
for `c98d33262493e636002227fd18e40a7e3239e8b8` passed 110/111 tests. All
ProcessorV2/ownership tests passed; `acquisition-integration` failed during the
first GREY camera-only CLI capture start with an explicit LOSSLESS queue
saturation. The other four matrix jobs passed. That path does not run semantic
processing. The log contains no per-frame host delivery or writer-latency trace.

`MANTIS_X1_FAKE_PACE=1` previously selected non-catch-up scheduling only for
`phase-*`. Startup fixtures advanced a cumulative host deadline from camera
start, permitting overdue frames to burst after a scheduling delay. Every paced
fake camera now schedules its next delivery one configured frame period after
the preceding callback completes. A late caller can receive one frame immediately;
the following delivery waits a full period. A delayed callback cannot accumulate
deadline debt. No native frame is skipped or fabricated. Native timestamps,
received timestamps, counters, clock domains, sensor identities, formats,
metadata and packed pixels do not depend on that scheduling clock.

The private `FakeTiming` seam supplies host time and waits to the actual fake
backend. Seven deterministic tests cover startup-left/right, phase delivery,
delays before the first frame and between frames, a delayed callback, timeout
without consuming a frame, paced/unpaced exact native-frame equality for both
cameras and formats, and unchanged failure injection. With pacing absent or `0`,
phase fixtures remain CPU-speed and other fixtures retain their cumulative
schedule for stress tests. No real camera, pairing algorithm, queue capacity,
50-ms admission deadline or lossless failure policy was changed.

A worktree-local injection experiment delayed Session thread creation by 500 ms
after camera start and the first `.segment.part` write by 150 ms. With the original
pacing branch it reproduced the exact initial CLI saturation error in Debug for
GREY and Y10P. A virtual-clock test also failed against that branch because
overdue startup frames did not wait between deliveries. The controlled runtime
experiment did not reproduce saturation with the original branch in the local
sanitizer build. It proves a matching failure mechanism, not the exact scheduling
history of the GitHub runner or that this was its sole cause. The injection tools
live only in ignored build scratch space and are not installed or run by CI.

Two-core CPU contention (three scoped burners at 85%, 85%, 25% duty) and repeated
8-MiB file writes/fsync exposed another fixture assumption: the 0.2-second drift
validator run could finish before the native half-period crossing at about 24
pairs. Its existing drift-exclusion assertion remains intact. The integration
test now requests at least 32 committed pairs through the optional
`--minimum-framesets` validator argument and verifies that count. Progress has a
finite 15-second allowance, matching the integration test's existing progress
budget. Default validator requests retain their duration-based behavior. This
changes the test completion condition, not camera cadence or recorder deadlines.

Final local verification for this correction passed:

- Complete Debug Studio-OFF suite: **118/118**, 136.20 seconds.
- Complete ASan/UBSan Studio-OFF suite: **118/118**, 370.92 seconds, with
  `detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`.
- Seven pacing cases and the existing explicit saturation/write-failure QoS
  case, ten repetitions each: **80/80 in each build**. Genuine queue saturation
  still reports failure, with saturation counted and no silently dropped packet.
- Corrected ASan/UBSan acquisition integrations under the CPU/storage pressure
  above: **10 GREY + 10 Y10P**, 158.27 seconds, without recorder saturation.
- Corrected Debug acquisitions with the same injected startup/write delays and
  CPU/storage pressure: **5 GREY + 5 Y10P**, all passed.
- Architecture boundaries and `git diff --check` passed. All eight L6 processing/
  ownership regressions, frozen ABI contracts and legacy/projected storage
  goldens passed in both complete suites.

The four closed L6 implementations, accepted formats/fixtures, L3–L5 control and
storage semantics, and real Linux camera backend are untouched. No L7 work or
optical extraction was started. Per the CI handoff policy, remote results for
the focused follow-up are PENDING until independent review of the pushed SHA;
L6 is not declared FINAL ACCEPTED here.
