# RawCapture-3 durability qualification — L8a-1a

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** L8a as a whole is NOT ACCEPTED;
L8a-1b (G02) is PLANNED. Accepted starting Studio SHA:
`e180803d7d1b94cf818461fa492e88fbab1a0b9a`.

## Persistence map and selected evidence

This map was established before implementation from Store, segments and platform
sources. Existing canonical bytes, SQLite schema/WAL/FULL policy and artifact
states remain authoritative.

| Phase | Actual boundary | Accepted evidence / additional selection |
| --- | --- | --- |
| Project initialization | Manifest part, file sync, rename, directory sync; SQLite WAL/FULL schema; exclusive project lock | Existing project/migration tests; not a new manifest or SQLite VFS campaign |
| Artifact initialization | Owned object directory, parent-directory sync, committed OPEN metadata row | New crash after row creation: absent header cannot authorize recovery |
| Header | MRUNHDR3 part write/flush/close, file sync, rename, directory sync before begin returns | Existing successful pre-prepare ordering; new interrupted initialization and sync/publication failure checkpoints |
| Active segment | Create part, directory sync, direct MRAWREC3 sequential writes and OS flush per record | New genuine prefix write then short result/ENOSPC and zero-progress EIO; active complete-prefix process crash |
| Sealing | Flush/close, file sync, file hash, rename, directory sync | New sync/rename failure; synced or closed unindexed segment crash |
| Segment metadata | SQLite transaction inserts chunk/index/accounting, COMMIT | New actual SQLite commit-hook rejection and crash around transaction commit; indexed prefix retained |
| Outcome | Seal preceding records; typed MRUNOUT3 part write/flush/close, sync, rename, directory sync | New outcome failure/publication crashes; missing outcome remains absent, valid complete part may be validated by explicit recovery |
| Finalization | Mark FINALIZING; revalidate header/segments/outcome/count/hash; commit FINALIZED/hash | New commit rejection and crash before/after final state publication |
| Startup | Replay existing journal; OPEN/FINALIZING rows become RECOVERABLE | Reopen in a new process/Store after every selected SIGKILL |
| Explicit recovery | Validate header, outcome and indexed hashes; validate closed/unindexed segments; truncate only incomplete active tail; sync/publish/index; verify and finalize | New gated cancellation/retry and second-recover rejection, exact bytes before/after; retain existing corrupt-complete rejection |

Logical transaction rollback, OS-visible process-crash recovery, filesystem sync
assumptions and sudden power removal are different claims. SIGKILL does not clear
kernel/device caches. No true power-loss qualification or physical operation is
part of this package. G02–G05 and physical acceptance remain outside scope.

## Injection boundary

The private `src/artifact-store/persistence-test.hpp` has two compile-time paths.
Ordinary builds alias `CaptureOutput` to `std::ofstream` and erase every checkpoint
and commit guard. There is no runtime fault flag, callback, registry, extra sync,
scan or allocation in that product path. The isolated Linux/BUILD_TESTING static
library compiles the **same** Store/segments sources with
`MANTIS_STORAGE_FAULT_TESTING`. Test executables select that archive explicitly;
ordinary Store/mantisd/client binaries remain uninstrumented. No library or tool
is installed. Public Store/plugin/client interfaces and schemas are unchanged.

`tests/storage/faults.cpp` owns eight finite plan slots, filtered by exact owned
project root and artifact ID (or the next matching initialization in that owned
root). Each plan consumes one selected operation. Unrelated artifacts in the same
Store and an independently running Store are exercised concurrently without
injection. Plans and their borrowed IPC pipes outlive matched calls. A short
registry mutex is released before I/O or waiting. No arbitrary callback is
accepted. Signals use fixed 321-byte messages and self-owned pipes with ten-second
communication deadlines; child RAII sends SIGKILL/reaps on failed assertions.

The mechanisms intentionally model different failures:

- A forwarding streambuf writes and flushes an actual bounded record prefix to
  its normal filebuf, then returns a short count to the real Store serialization.
  ENOSPC leaves exactly 37 MRAWREC3 bytes; EIO returns zero progress on an already
  created part. This is a short-write **stream boundary** model with real retained
  bytes, not a claim that the kernel/disk actually exhausted space. The stream's
  bad state reaches the normal segment error path. No whole-record staging buffer
  is added to production, and no exception before all writing substitutes for the
  37-byte test.
- Executable-local `--wrap=fsync` makes the selected real platform file/directory
  synchronization call return `-1/EIO`. It filters only already-open descriptors
  of the owned artifact via `/proc/self/fd`; other calls execute the real fsync.
  There is no LD_PRELOAD or host-global fault setup. SQLite's shared-library WAL
  synchronization is not claimed as separately injected by this wrapper.
- Owned directory obstructions make the real `std::filesystem::rename` fail.
  The outcome obstruction is installed at its exact publication checkpoint, after
  admission checks. Only that empty test obstruction is removed before inspection
  or recovery. A separate injected EIO **after** successful outcome rename models
  publication having occurred before a later operation reports failure.
- A scoped per-connection `sqlite3_commit_hook` rejects the actual selected chunk
  COMMIT or FINALIZED autocommit. SQLite rolls the transaction back and reports
  its commit-hook constraint error, which the existing Store maps to Status::io.
  This is not a fabricated filesystem EIO or a corruption of WAL bytes. The hook
  and stack callback context retire before outer Store error cleanup, so the
  cleanup metadata transaction is not rejected a second time.

All normal storage exception/state handling is reused. No confirmed production
correctness defect was exposed and no storage behavior correction was required.
An unpublished/missing header causes explicit recovery refusal (currently a
filesystem error); a RECOVERABLE row is inspectable but does not promise that
recovery can invent the missing immutable program/header.

## Write/durability failure results

`projected-storage-durability`, `failures()` in
[durability.cpp](../../tests/storage/durability.cpp), seeds a durably indexed
segment with two independently specified canonical bundles. Every case verifies
that segment remains byte-identical, the error is explicit, and no failed normal
finalization is reported as successful. A new Store performs recovery, exact
header/provenance/count/outcome checks and canonical replay comparisons.

| Case | Actual failure state | Permitted explicit recovery |
| --- | --- | --- |
| Short ENOSPC | Second part contains exactly the first 37 bytes of the independently encoded next record; no complete new record/footer | Truncate incomplete tail, retain exactly two old bundles; no outcome |
| Zero-progress EIO | Second part exists with zero bytes; old indexed segment intact | Empty tail excluded; exactly two old bundles; no outcome |
| Segment file fsync EIO | Complete third record OS-visible in closed part, synchronization unacknowledged, index unchanged | Validate/sync/publish it anew; three bundles; no fabricated outcome |
| Publication directory fsync EIO | Third record's segment renamed, directory sync fails before index commit | Discover closed unindexed segment, validate/reindex; three bundles; no outcome |
| Segment rename failure | Complete synced part cannot replace owned obstruction directory | Remove own obstruction, validate/publish/reindex; three bundles; no outcome |
| Chunk commit-hook rejection | Closed segment present; real SQLite transaction rolled back; chunks remains one | Discover/reindex validated file; three bundles; no outcome |
| Outcome file fsync EIO | Prior evidence sealed; complete typed outcome.part OS-visible but sync unacknowledged | Validate exact complete part and prefix, resync/promote; three bundles and original outcome |
| Outcome rename failure | Complete synced outcome.part cannot replace owned directory obstruction | Remove own obstruction; validate/promote exact outcome; three bundles |
| Final metadata commit rejection | Header/three bundles/outcome durable; FINALIZED update rolls back; state becomes RECOVERABLE | Revalidate and finalize with exact same outcome/hash inputs |
| Error after outcome publication | Outcome rename already succeeded when injected EIO is reported; row becomes RECOVERABLE | Reconcile actual published outcome, not absence guessed from failed call |
| Header fsync EIO | OPEN row allocated, complete header.part not published; begin fails and marks RECOVERABLE | Refuse missing durable header; retain initialization evidence, no invented run |

Both failure and recovery are verified against canonical values retained independently
of the faulting files. Complete corrupt records never become acceptable incomplete
tails: `retry()` supplies a complete checksum-corrupt trailing record, requires
Status::corrupt on repeated attempts, and compares the entire tail and original
indexed segment byte-for-byte. The indexed prefix is decoded independently too.

## Synchronized process crashes

`crashes()` executes fresh child processes and waits for an exact point/artifact
message before SIGKILL; no sleep estimates the crash location. Each child is reaped
with WIFSIGNALED/SIGKILL checks before a new Store opens the project. Tests use the
existing independent L4 semantic fixtures; they do not instantiate a controller
or dispatch any represented trigger/emitter intent.

| Point | Persistent observation after process death | Reopen/recovery assertion |
| --- | --- | --- |
| artifact-row | Committed metadata row, header absent | RECOVERABLE but recovery refused; no fabricated header/outcome |
| header-synced | Exact complete header.part synced, rename not performed | Part bytes retained; unpublished header not silently promoted |
| header-published | Header renamed, begin not returned | Exact immutable header; explicit zero-bundle/no-outcome prefix recovery |
| record-written, second record | Complete active records flushed, seal not yet entered | Exact two-bundle prefix recovered; no outcome |
| record-write, 37-byte third-record prefix | Two indexed old bundles plus actual partial active record; stream has not returned | Partial tail removed only; exact old prefix retained |
| segment-synced | Complete part synced before rename | Validated/synced/published/indexed during recovery |
| segment-published | Closed segment exists before directory sync/index transaction | Closed unindexed segment reconciled; exact two-bundle prefix |
| chunk-commit-before | SQLite transaction contains uncommitted chunk metadata | Reopen confirms chunks=0 after rollback, then reindexes exact file |
| chunk-committed | Chunk COMMIT succeeded before writer bookkeeping | Reopen confirms chunks=1; no duplicate chunk/record on recovery |
| outcome-written | All four records sealed; complete outcome.part written/closed, before file sync | SIGKILL leaves OS-visible valid bytes; recovery verifies/promotes exact original outcome |
| outcome-published | Outcome rename succeeded before directory sync/return | Exact original outcome and four records retained |
| finalize-commit-before | Verification complete but FINALIZED update not issued | Startup classifies RECOVERABLE; verified explicit finalization required |
| finalize-committed | FINALIZED/hash committed, API not returned | Reopen already FINALIZED; second recover rejects unchanged |

All published-header cases compare exact ProgramReference, RunId, GenerationId,
header/configuration/provenance, canonical bundle order/images/evidence and the
presence/absence of a final outcome. Valid outcome parts may be recovered only
when their actual complete bytes and exact prefix binding validate. Missing
outcomes remain missing. Accepted `projected-raw-capture` SIGKILL coverage also
retains the important completed-executor-without-daemon-outcome case; no duplicate
terminal reconstruction policy is introduced here.

SIGKILL verifies process-crash recovery with live kernel caches. File/directory
sync calls and SQLite FULL transactions assume a functioning filesystem and
storage stack honoring their contract. No sudden-power-loss, cache-loss, device
failure or filesystem-corruption qualification is inferred.

## Recovery cancellation and retry

`retry()` gates a real recovery after scanning the active segment but before
acceptance/truncation/publication. The parent cancels the existing token, releases
the explicit gate, and requires Status::cancelled, RECOVERABLE state and unchanged
active bytes. A fresh explicit retry retains three exact complete records and
removes only the 37-byte incomplete trailing fourth record. Finalized recovery is
rejected without changing descriptor/hash/count/header/outcome or replayed bytes.
The corrupt-complete companion remains RECOVERABLE and byte-identical across two
recovery attempts. This does not qualify large-scan cancellation latency (G04).

## Actual daemon propagation

`projected-storage-daemon` starts an uninstalled test executable that compiles the
actual `apps/mantisd/main.cpp` unchanged, with the isolated Store selected at link
time. Only this wrapper arms the final metadata rejection in its self-owned test
project. No production daemon option or environment fault selector is added.
It loads the accepted dedicated PTY fixture with all existing activation gates,
uses the actual L5 client/service, L3 supervisor, F2 Host, FD transport, configured
internal PTY and accepted synthetic controller. One finite step/two zero-camera
bundles remain the supported F2 profile.

The test requires the actual daemon execution outcome to be COMPLETED with two
committed bundles, while recording is RECOVERABLE with Status::io and a failed
finalization job. This is **not** successful recording. The durable execution
outcome remains immutable and distinct from the storage failure. After shutdown,
a separate ordinary, uninstrumented client/Store process runs with every fixture
activation variable removed. It compares exact client-reported run/generation/
artifact IDs, expected program identity, and the independently retained terminal
bundle; explicit recovery preserves the complete outcome byte-for-byte and two
replay passes agree. Refused repeat recovery is checked too. Physical observation,
ACK-to-optical inference, hardware OFF and exposure-effective evidence remain absent.

## Requirements and remaining limits

| Requirement | Executable evidence |
| --- | --- |
| L8A1A-01 | Executable-local byte/fsync faults, scoped SQLite hook, finite root/artifact plans, concurrent unrelated artifacts/Store; `projected-storage-boundaries` checks product symbols and source/build isolation |
| L8A1A-02 / RA-04.06 | Eleven failure scenarios above, explicit error/state, old prefix bytes unchanged, exact recovered canonical records/outcomes; no false successful recording |
| L8A1A-03 / RA-04.07 | Thirteen exact pipe-synchronized SIGKILL windows, new process/Store classification, SQLite rollback versus committed index, exact prefix/outcome recovery; existing completed-without-outcome regression retained |
| L8A1A-04 / RA-05.04 | Gated recovery cancellation and retry, exact tail handling, repeated CORRUPT refusal and immutable FINALIZED recovery rejection |
| L8A1A-05 | Actual PTY daemon/client finalization failure, immutable successful execution versus failed storage, ordinary controller-free offline recovery/replay |

G01's selected software failure/crash/retry properties now have direct executable
evidence. Independent package acceptance remains pending. G02/1b, G03 client and
generation races, G04 large replay cancellation, G05 sustained qualification,
physical storage power removal, all hardware/firmware/HIL work and optical
qualification remain outside this package. No physical hardware was accessed.

## Normal-path cost and compatibility

No format, public API, SQLite schema/ordering, production write/flush/sync count,
queue, image copy or whole-record staging buffer changed. Product checkpoint
macros erase to void expressions; output remains std::ofstream. Source/binary
checks prove the fault types and controls are absent from product Store/mantisd/
client. BUILD_TESTING=OFF excludes every qualification target.

A local GCC 15.2 Debug executable-section comparison with the retained pre-change
Store object found 8,222 of 8,223 common executable sections byte-identical, no
added/removed sections. The seal function's Debug instruction shape differed by
one instruction, with the same 49 calls. In particular the append hot path has
no new checkpoint calls, allocations or I/O. This is compiler/build-scoped evidence,
not a universal machine-code promise.

A representative production-path synthetic schema-2 Y10P benchmark (32 reused
1280×720 stereo FrameSets, 73,728,000 pixel bytes, two segments, Debug Linux x86_64)
measured append 432.7 MB/s, write+finalize 174.0 MB/s, replay 452.4 MB/s, peak RSS
79,671,296 bytes and zero queue drops. This is an informational compatibility/cost
sample, not sustained schema-3, real Q6A or physical storage qualification. Golden
schema-1/2/3 bytes and ABI/protocol regressions remain the compatibility gate.

## Final local verification

Linux x86_64, GCC 15.2, Debug; all three configurations completely built.
The expired `/tmp` OpenCV cache was restored by extracting official Ubuntu
4.10.0 packages into ignored `build/l8a1a-deps`, without installing packages or
changing repository dependencies. The headless and sanitizer caches retain the
optional exact-SHA E2 live firmware test at clean read-only source
`92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8`; normal/default CI does not require it.

| Check | Actual result |
| --- | --- |
| Complete Studio ON build / CTest | PASS, 145/145, 143.96 s |
| Complete Studio OFF build / CTest | PASS, 143/143, 131.03 s, including optional live E2 |
| Complete ASan/UBSan build / CTest | PASS, 143/143, 341.37 s, leak detection and fatal errors enabled |
| New durability and actual-daemon tests, normal | PASS, 50 repetitions each, 45.78 s; 650 synchronized SIGKILL cases |
| New durability and actual-daemon tests, sanitizer | PASS, 20 repetitions each, 71.44 s; 260 synchronized SIGKILL cases |
| BUILD_TESTING=OFF full build | PASS; no qualification/PTY fixture targets; product contains no fault controls |
| Source/binary and frozen boundary/catalog checks | PASS; unchanged 129 structs, 10 callbacks, 816 fields, 113 constants, 22 IDs |
| Formatting, whitespace, Python compilation, Markdown paths/anchors and requirement/status references | PASS |

Complete suites include existing schemas 1/2/3 and all storage goldens,
projected controls/daemon/recording/replay, L7a/L3–L6, F2/E1/E2/PTY/R1/serial,
camera pairing/pacing and frozen ABI/protocol regressions. No existing test or
assertion was modified or disabled. Default new counts are 145 Studio ON and
142 headless/sanitizers; the optional live test adds one locally. Native ARM64
execution was unavailable locally and remains a required remote acceptance gate.

Reproduction on the configured caches (see [BUILDING.md](../../BUILDING.md)
for normal presets and dependencies):

```bash
export LD_LIBRARY_PATH="$PWD/build/l8a1a-deps/root/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"
export OPENCV_OPENCL_RUNTIME=disabled
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/l2 --parallel 4
ctest --test-dir build/l2 --output-on-failure
cmake --build build/l2-sanitized --parallel 3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/l2-sanitized --output-on-failure
ctest --test-dir build/debug -R '^projected-storage-(durability|daemon)$' --repeat until-fail:50 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/l2-sanitized -R '^projected-storage-(durability|daemon)$' --repeat until-fail:20 --output-on-failure
cmake --build build/l7b2a-no-tests --parallel 4
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
git diff --check
```

The extra library search path is specific to this local dependency restoration,
not required by normal CI. Sanitizers are unsuppressed. The fault model is one-shot
selected operation failure plus the explicit crash/cancel cases; it does not
qualify persistent failure of every subsequent cleanup metadata write, arbitrary
SQLite/WAL/media corruption, sudden power loss, or combined UART faults. L5 exposes
the actual finalization error and failed job; MRUNOUT3 is not extended to synthesize
a historical finalization errno after restart. The persistent RECOVERABLE state
and exact execution outcome remain independently inspectable.

Independent exact-SHA review and the five-job remote matrix determine acceptance.
No physical UART, camera, Q6A, ESP8266, GPIO, USB-HIL, flashing or laser operation
was performed. No firmware source was modified.
