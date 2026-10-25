# CUT-0B-C01 — project capability regression

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** Branch `integration/cut-0b`, baseline
`c381745005ba14dd9b642d1fe713f7a3a0a49507`. This correction changes only the existing
public-client test/harness and adds this record. The original CUT-0B qualification
and historical source-milestone reports are preserved.

## Failure and investigation

[Workflow 40179515231, sanitizer job 121127351819](https://github.com/martinkoenig/mantis-studio/actions/runs/40179515231/job/121127351819)
failed `project-public-capabilities` with `Replay requires a finalized RawCapture`;
the remaining 142 sanitizer tests passed. No sanitizer memory/UB violation was
reported. The old log did not retain the Capture.error, counters, artifact state or
daemon diagnostics, so the exact original producer/writer error cannot be recovered
from that CI transcript. It is not justified to label the failure a flaky sleep,
assert a specific CI errno, or claim a reproduced CI scheduling trace.

Inspected implementation establishes the relevant contract:

- `device::Session` construction waits for the first **committed** frame (or fails).
  Premature shutdown before the very first frame is therefore not an explanation
  for this successful `start_capture` call.
- For virtual-scanner schema 1, `Runtime::stop_capture` joins capture workers and
  finalizes synchronously when healthy. A session error instead abandons its OPEN
  artifact as RECOVERABLE. There is normally no finalization job for this schema.
- The C++ void `Client::stop_capture` handles a response-level error or job failure,
  but does not throw solely because the returned `Capture.error` is nonempty.
  The old test assumed the return meant recording success and did not check it.
- `Runtime::replay_capture` correctly rejects a non-FINALIZED artifact. This is
  containment, not evidence of a codec, replay or public-API incompatibility.

For this pinned schema-1 test, a normal stop return followed by that replay
rejection identifies the session-error/abandon branch rather than an unfinished
asynchronous finalization job: a schema-1 finalization exception would instead
have failed the stop request. This is a source-derived inference; the old CI log
did not directly record the abandoned state or its originating error.

Ten baseline ASan/UBSan repetitions passed on local tmpfs; ten instrumented
baseline repetitions also passed on an owned disk-backed temporary directory.
Those passes do not explain away the remote failure. Public-state diagnostics were
added before replacing the readiness assumption.

A separate controlled ASan/UBSan investigation held an **owned test project's**
SQLite writer lock (`BEGIN IMMEDIATE`, no data/schema mutation) for 1.5 s, then
released it and observed only public API state. This produced the same downstream
replay rejection through the normal bounded lossless-capture failure path:

| Observation | Actual result |
| --- | --- |
| Initial capture | Active, 1 produced / 1 committed, RawCapture schema 1 OPEN |
| Recorder pressure | Queue high-water/capacity 32; saturation 1; `LOSSLESS writer queue saturated; capture failed explicitly` |
| After stop/drain | Inactive, 35 produced / 34 committed, error retained, no finalization job |
| Artifact | Same identity, schema 1 RECOVERABLE, 34 retained chunks, no finalized hash |
| Replay | Status::incompatible, `Replay requires a finalized RawCapture` |

The observed capture was `6998267d-6bd9-427b-b90c-687b8d4488c7`, raw artifact
`fea8c8d0-c895-4250-93f3-8b9c3345a693`. Probe code/logs and full selected state are
retained locally under ignored `build/cut0b/c01-contention-*`. This characterizes
one concrete failure mechanism and verifies the invalid test assumption. It does
**not** establish that SQLite contention was the historical CI cause. No global
fault setup, real disk exhaustion or production instrumentation was introduced.

A second fault probe ran the **corrected actual test/harness** through an isolated
build-path alias. It acquired only its own project's writer lock after observing
two real committed chunks, waited for the existing producer's saturation diagnostic,
then released the lock. The client failed at `healthy()` before replay, and the
harness retained Capture/RawCapture identities, error/counters, OPEN artifact state,
daemon output and project diagnostics. Its nonzero exit was explicitly required;
`BLOCKER VERIFIED` was absent because the positive path never ran. This confirms
that a genuine failed recording is not retried, recovered or relabelled as success.
The probe and assertions are retained in ignored `build/cut0b/c01-negative-probe.py`;
every normal registered test independently verifies the OPEN-replay rejection below.

## Correction and invariants

`tests/integration/project_capabilities.cpp` now:

1. Retains Capture/RawCapture IDs and reports selected public Capture, Artifact and
   Job state on failure. It never serializes Requests, endpoints or credentials.
2. Rejects replay of the actual healthy OPEN capture with Status::incompatible and
   verifies no job was admitted. This negative check executes in every normal run.
3. Replaces the 600 ms sleep with an eight-second bounded observation loop. It
   requires 64 committed frames and at least two seconds between genuine preview
   receive timestamps. The observed last sequence must also have been committed;
   preview production alone is not recording success. Clock/type/identity, active
   state, capture errors and queue saturation are checked. Independently sampled
   counters may differ during acquisition; stopped counts must agree exactly.
4. Requires active capture immediately before/after the unchanged BUSY project
   switch assertion. Capture failure or lost authority fails the setup explicitly.
5. Inspects the public stop response and final capture status: inactive, no error,
   drained queue, produced=committed. An advertised finalization job must complete
   successfully within a bounded wait and identify the same artifact. Exact type,
   schema, identity, FINALIZED state, completed hash, bytes and chunk count are
   required **before** replay. No recovery, capture retry or state downgrade is used.
6. Requires the exact replay job to be Queued/Running before the job-BUSY refusal.
   The request begins before the worker can start. The refusal must complete inside
   the recording's observed minimum paced replay span, fencing the old race where
   an unspecified short replay could finish before the assertion. Queued/Running
   is not mistaken for completion; the same job must subsequently complete with
   the expected result artifact.
7. Retains all original two-client switching, path/auth/lock refusal, raw literal
   path, old-preview leak and lease-release assertions. The `BLOCKER VERIFIED`
   result still demonstrates the known project/replay isolation defect. GUI
   New/Open remains gated; no runtime generation redesign is attempted.

The 25 ms sleep is an observation cadence, not a replacement readiness delay.
The preview receive clock is serialized unchanged in MANTIS01 and used by the
existing paced replay source. The two-second requirement is test setup evidence
for a finite busy-job window, not a camera-rate, latency or physical timing claim.
A genuinely failed capture, finalization or replay still fails the test. Pathological
scheduling beyond the measured assertion window fails with diagnostics rather
than retrying or accepting a completed job as a busy-state proof.

`project_capabilities.py` retains/redacts the client trace and, on failure, prints
and saves at most the last 64 KiB each of the daemon log and owned project's
capture diagnostics. Partial stdout survives a client timeout. Processes have
finite cleanup waits. Access tokens are per-instance and redacted; no environment
or Request dump is collected. Existing CTest name, registration, timeout and
assertions remain enabled. No production C++, ABI/protocol, GUI, firmware or
hardware behavior is changed.

## Verification

Focused final sanitizer repetitions: **30/30 PASS**, 135.29 s, plus **10/10 PASS**
with an explicitly owned disk-backed TMPDIR, 46.28 s. Every run exercised OPEN
replay rejection, healthy finalization, active capture/job refusals and the known
old-replay leak. ASan leak detection and halt-on-error / UBSan halt-on-error were
enabled without suppressions.

| Complete local verification | Result |
| --- | --- |
| Studio ON Debug build / CTest | PASS 183/183, 467.62 s |
| Studio OFF Debug build / CTest | PASS 143/143, 131.32 s |
| ASan/UBSan build / CTest | PASS 143/143, 354.78 s; leak detection and fatal errors enabled |
| Existing ten-route UI gate, three repetitions | PASS 30/30 executions, 318.37 s |
| BUILD_TESTING=OFF product build | PASS; no product or build-definition change |
| Frozen ABI/catalog, protocol/storage goldens, repository boundaries | PASS; complete suites plus explicit boundary/catalog checks |
| Formatting / Python compilation / whitespace | PASS |

Complete suites retain all F2/PTY, acquisition, calibration, projected control,
storage durability/recovery, recording/replay and existing UI regressions. Test
names/counts remain 183 Studio ON and 143 headless/sanitizers. No existing assertion
or test was disabled. The normal and disk-backed repeated sanitizer checks above
are additional to the complete suite.

Reproduction on the isolated integration-worktree caches:

```bash
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"
export OPENCV_OPENCL_RUNTIME=disabled
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
cmake --build --preset headless --parallel 4
ctest --preset headless
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers -R '^project-public-capabilities$' --repeat until-fail:30 --output-on-failure
mkdir -p build/c01-disk
TMPDIR="$PWD/build/c01-disk" ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers -R '^project-public-capabilities$' --repeat until-fail:10 --output-on-failure
ctest --test-dir build/debug -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-scan-qml|studio-scan-stress|studio-scan-surfaces|studio-ui-m0-qml)$' --repeat until-fail:3 --output-on-failure
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
git diff --check
```

The extra library path is the existing local extracted dependency cache, not a
new repository dependency. Full logs remain under ignored `build/cut0b/c01-*`.
 Exact pushed SHA and one nonblocking CI status are supplied in the
completion report; independent review remains mandatory. Native ARM64 and remote
CI remain separate gates. No physical hardware was accessed and no other worktree was modified by the
correction.
