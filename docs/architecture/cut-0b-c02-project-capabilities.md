# CUT-0B-C02 — project capability CI correction

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** Branch `integration/cut-0b`, starting
revision `da41e552225e70eba2d4d4967d677c2141f7eb92`. This record supplements the
[historical C01 investigation](cut-0b-c01-project-capabilities.md); its validation
report is preserved. CUT-0B remains subject to independent exact-revision review.

## Two distinct failures

[Workflow 40184265895](https://github.com/martinkoenig/mantis-studio/actions/runs/40184265895)
failed `project-public-capabilities` in all five jobs. Other CTests passed. Failed-job
logs were retrieved through GitHub CLI. They demonstrate two separate mechanisms,
not an ASan memory diagnostic or a UI route failure.

### Ordinary builds: dangling snapshot reference

C01 introduced a range-for over `a.snapshot().jobs()`. Protobuf returns a reference
to a repeated field owned by the temporary Response. With GCC 13 that Response is
destroyed before iteration. Local C01 qualification used GCC 15, which implements
C++23 P2718's extended range-for temporary lifetime; CI uses GCC 13. See the
[GCC implementation table](https://gcc.gnu.org/projects/cxx-status.html) and
[GCC 15 changes](https://gcc.gnu.org/gcc-15/changes.html).

The actual instrumented client translation unit was rebuilt with GCC 13.4, linked
against the existing local public Client/wire libraries, and run against the real
daemon and virtual scanner. It reached a FINALIZED schema-1 artifact with 64
committed chunks, then terminated with **return code -11 / SIGSEGV**, immediately
after `API snapshot status=OK jobs=1`. The daemon remained alive. A second run under
GDB established the native failure location:

```text
Program received signal SIGSEGV, Segmentation fault.
#0 google::protobuf::internal::RepeatedPtrIterator<...Job const>::operator*
#1 main .../tests/integration/project_capabilities.cpp:194
   for (const auto &value : job_snapshot(a).jobs())
```

`job_snapshot(a)` was the diagnostic equivalent of the original `a.snapshot()`;
it preserved the invalid owner/reference lifetime for reproduction. GCC 13 also
reported `-Wdangling-reference`. Historical remote logs did not retain a signal
number; the signal and backtrace above are actual **local reproduction**, not an
invented remote exit status. The corrected code owns the Response during lookup.
No production Client, protobuf layout or runtime repair was necessary.

### Sanitizers: real LOSSLESS writer pressure

The failed sanitizer capture reported 45 produced / 13 committed FrameSets,
queue high-water/capacity 32/32, saturation 1 and depth 30, over approximately
1.584 seconds. These aggregate counters correspond to approximately 28 produced
and 8 committed FrameSets/s in that short failure window; they are not a sustained
throughput benchmark. The production legacy session aims at a 33 ms producer
cadence. Schema-1 append performs packet/journal synchronization, rename and
SQLite commit before committing a frame. The C01 requirement for 64 committed
frames and a two-second receive span exposed a sustained producer/writer imbalance.
The runtime explicitly failed rather than dropping lossless data or finalizing it.
The C01 healthy-state check correctly rejected that recording.

A controlled reproduction locks only the owned project's SQLite writer after an
explicit client checkpoint. The real unpaced source fills the real finite queue.
The same C++ healthy-state check fails; after releasing the lock and stopping the
capture, the artifact is RECOVERABLE and replay rejects it. This establishes the
pressure/failure mechanism without cgroup throttling, disk exhaustion or global
fault injection. It does not claim to recreate the remote runner's exact scheduler
or identify a particular remote filesystem bottleneck.

## Correction and contracts

| Requirement | Implementation and executable evidence |
| --- | --- |
| C02-01 / C02-02 | Flushed stage markers surround finalized-artifact assertions, replay submission, job lookup, BUSY refusal and exact-job completion. The harness records child return code, signal/name, timeout, last attempted/completed stage, last API status and both daemon statuses. GCC 13 reproduction and native backtrace above identify the failing operation. |
| C02-03 / C02-04 | An isolated test-only module wraps the existing virtual-scanner generator with one-frame/s, non-catch-up delivery. Three genuinely committed images are required, retaining clock/domain, sequence and receive-time consistency checks. The readiness deadline remains finite. This decouples capability verification from sustained 30fps storage throughput; it changes neither production cadence nor queue capacity. |
| C02-05 | A Linux test-daemon executable compiles the existing mantisd entry point and uses executable-local `--wrap=mmap`. An inherited socketpair arms a one-shot barrier at the finalized packet read. The client waits for the reached acknowledgement, checks its exact replay ID is Queued/Running, requests project switching and checks BUSY, then verifies the same job is still active before releasing the worker. The job must subsequently Complete with the exact RawCapture result. No replay-duration assumption establishes BUSY. |
| C02-06 | `project-public-capabilities` retains missing/locked project, auth/path/NUL, active capture, two-client identity, Unicode/literal path, OPEN replay rejection and old-preview leak assertions. All original security refusals remain required. GUI New/Open stays blocked. |
| C02-07 | `project-public-capabilities-negative-writer` uses normal mantisd and the production virtual generator, plus the owned SQLite lock. It requires actual queue saturation, C++ readiness failure with exit 1, RECOVERABLE state and replay refusal. Missing, stale, Completed, Failed and Cancelled jobs are also fed through the actual job-selection predicate and must fail to establish active-job evidence. |
| C02-08 / C02-09 | Workflow GDB route diagnostic runs only if LastTestsFailed lists `studio-ui-m0-qml`; unrelated Core failures do not trigger that binary. Existing required jobs and CTest failures remain mandatory. Redacted, bounded capability evidence is uploaded for ON/OFF and sanitizer jobs. |

### Ownership, isolation and limits

All new C++ fixture sources are under `tests/fixtures/project-capabilities` and
BUILD_TESTING. The paced plugin lives in `project-capabilities-plugins`, has no
install rule and is selected explicitly only by this harness. It delegates image
generation and buffer ownership to the existing public-SDK virtual scanner; no
extra per-image copy or alternate recording implementation is introduced. Delivery
waits are bounded to one second, with no catch-up burst; legacy session stop may
wait for that already-entered `next()` call. Native synthetic clock facts remain
unchanged. Receive timestamps reflect actual delivery.

The Linux replay gate is linked only into `mantis-project-capabilities-mantisd`,
not normal mantisd, Studio, the SDK or any production plugin. The public API has
no job pause operation: observing Running alone cannot reserve its lifetime across
the next RPC. The socket barrier establishes that lifetime without a production
API or scheduler change. It neither changes
stored bytes nor bypasses the real replay worker. It recognizes owned `.packet`
reads only after the client explicitly arms it; `.packet.part` capture writes are
not paused. Store `object_path()` releases its SQLite mutex before the mapped
integrity read, allowing public snapshots and project-BUSY requests to proceed.
One gate has one owner per endpoint, one admission and a 15-second wait deadline.
EOF, timeout or a malformed release fails the mapping/job. No production callback,
signal handler, pause option, environment selector or ABI was added.

Python owns both daemons, the client process group, socketpair and temporary
project. A failing negative-test checkpoint releases its SQLite lock in `finally`.
The client has a 30-second collection deadline; owned processes are killed/reaped
on failure. Client output retains at most a 64K-character tail plus a truncation marker; raw
failure-log reads retain at most 64 KiB each. Tokens are redacted. Optional `--gdb-client` runs the actual client without dumping
its arguments, locals or protocol requests. Debugger mode explicitly labels its
wrapper return code; the native signal remains visible in its backtrace output.

No retry turns an unhealthy recording into a pass. Positive capture still requires
zero errors, saturation and drops; matching Capture/RawCapture IDs; consistent
produced/committed counts; a drained terminal queue; supported schema; FINALIZED
state; changed nonempty hash; positive content size; and matching committed chunk
count. OPEN and failed recordings never satisfy replay admission. The Linux-only
negative/barrier infrastructure does not claim Windows/macOS qualification.

## Local qualification

Local environment: Linux x86_64, Ubuntu 25.10, GCC 15.2, Qt 6.9 and OpenCV 4.10;
GCC 13.4 was additionally used for the actual client reproduction/correction.
Existing caches use an extracted local OpenCV dependency path; this is not a new
dependency or production configuration change.

| Verification | Actual result |
| --- | --- |
| Complete Studio ON Debug build / CTest | PASS 184/184, 467.18 s |
| Complete Studio OFF Debug build / CTest | PASS 144/144, 132.55 s |
| Complete ASan/UBSan build / CTest | PASS 144/144, 345.04 s; leak detection and fatal errors enabled |
| BUILD_TESTING=OFF complete product build | PASS; no capability fixture targets/directory |
| Initial normal positive/negative repetition | PASS 10 repetitions of each, 75.39 s |
| Initial ASan/UBSan positive/negative repetition | PASS 10 repetitions of each, 79.20 s |
| Final restored preview-consistency assertions, both tests x5 | PASS in ON/OFF/sanitizers, 37.59 / 37.57 / 39.42 s respectively |
| Final GCC 13 actual client | PASS 5/5 after rebuilding final source; GDB correction check also PASS |
| Existing ten-route UI gate x3 | PASS 30/30 executions, 317.38 s |
| Repository boundaries / frozen ABI catalogs | PASS; 129 structs, 816 fields, 10 callbacks, 113 numeric constants, 22 IDs |
| Product/Studio binary and source isolation | PASS; test wrapper present in isolated daemon, absent from normal daemon/Studio/plugin; no production C++/SDK/protocol edits |
| Workflow diagnostic guard | PASS actual shell guard against non-UI failure and real route-failure manifests; five-job matrix preserved |
| Formatting / Python compilation / local documentation links / whitespace | PASS |

The complete suites include Core/StudioBridge, acquisition, F2/E1/E2 reference,
PTY/serial-settings, calibration, L3–L6, storage durability, schema/golden,
recording/replay and existing UI regression registrations. Generated inventories
preserve every original name: 183 ON and 143 OFF/sanitizer tests, with **only**
`project-public-capabilities-negative-writer` added, no removals or disabled tests.
The final diff review retained C01's preview clock/domain/sequence checks; the two
affected tests were then rebuilt and repeated five times in each configuration.
Other test binaries and production sources were unchanged by that assertion review.
The GCC 13 five-pass check was repeated against that final source as well.

Full logs and compiler/backtrace evidence remain in ignored `build/cut0b/c02/`;
public capability evidence is under each build's `ui-m2a/capabilities*` directory.
No required check was skipped or weakened.

```bash
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"
export OPENCV_OPENCL_RUNTIME=disabled
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless
cmake --preset sanitizers -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/sanitizers --output-on-failure
ctest --test-dir build/headless -R '^project-public-capabilities' --repeat until-fail:10 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/sanitizers -R '^project-public-capabilities' --repeat until-fail:10 --output-on-failure
python3 tests/integration/project_capabilities.py build/headless --gdb-client
cmake -S . -B build/no-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/no-tests --parallel 4
ctest --test-dir build/debug -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-scan-qml|studio-scan-stress|studio-scan-surfaces|studio-ui-m0-qml)$' --repeat until-fail:3 --output-on-failure
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
git diff --check
```

The known cross-project replay/preview isolation defect remains deliberately
reproduced, not repaired or waived. Native ARM64 is a remote CI gate; local native
ARM64 and a full Ubuntu 24.04 rebuild are not claimed. Public ABI, protocol, storage
formats and production Core/UI sources are unchanged. No firmware, physical device,
Q6A, GPIO, serial, laser or HIL operation occurred. No other worktree was modified;
no develop/main promotion or Orchestrator activation is included.
