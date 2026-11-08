# Current-source Studio sanitizer qualification

This opt-in local qualification addresses CI-STAB-2 / S2-04A, Issue #24. It adds
no mandatory CI job and changes neither the headless sanitizer preset nor the
five Architecture job names, deadlines, registrations or UI10x3 repetitions.
Independent source review and all five required exact-head remote checks remain
acceptance gates. A tool's successful exit does not independently ACCEPT a release.

## Reproduction

Use an isolated assigned worktree, its current expected Git SHA, the Linux build
dependencies in BUILDING.md, `/usr/bin/python3`, and an explicit Qt prefix/version.
Never run another build/test process against these caches during qualification.
The output must be a new private directory outside the source tree. The tool
neither installs packages nor changes system Qt. Build parallelism is bounded to
eight; all CTest execution is sequential.

```bash
/usr/bin/python3 -B tools/qualify_studio_sanitizers.py --phase prepare \
  --source-sha "$EXPECTED_SHA" --qt-prefix "$QUALIFIED_QT_PREFIX" \
  --qt-version "$EXPECTED_QT_VERSION" --output "$PRIVATE_EVIDENCE" --jobs 8
/usr/bin/python3 -B tools/qualify_studio_sanitizers.py --phase execute \
  --source-sha "$EXPECTED_SHA" --qt-prefix "$QUALIFIED_QT_PREFIX" \
  --qt-version "$EXPECTED_QT_VERSION" --output "$PRIVATE_EVIDENCE" --jobs 8
```

Preparation configures/builds the normal Studio ON Debug, headless Debug, existing
headless sanitizer, and separate Studio ON sanitizer caches. It captures complete
post-build CTest argv/properties, compiler/link commands, ASan/UBSan symbols,
debug sections, runtime linkage and binary/library hashes. NC2 temporarily adds a
use-after-free to the existing instrumented Scan test's production
`MeasurementView::setImage` path. Its primary ASan error must occur; `finally`
restores exact source bytes and rebuilds before positive tests. An interrupted
preparation requires inspection/restoration of that test before any further run.
GCC may detect the invalid vtable read at the call site before entering the setter;
the verifier requires the primary heap-use-after-free and production MeasurementView
allocation/release source frames, plus distinct mutant/restored binary identities.

Execution rechecks unchanged inputs, renders three standalone Qt control frames,
requires the intentional standalone leak to produce primary LSan evidence, then
executes focused Studio tests before assessing the remaining Studio inventory.
It runs all normal/headless mandatory suites, the preserved UI10x3, an actual
Studio software-rendered screenshot with loader/plugin tracing, and a disposable
failing CTest whose primary diagnostic must remain available. Recomputed-manifest
mutations of real evidence exercise wrong source SHA, inconsistent Qt loader
identity, absent authored instrumentation and incomplete CTest results.

The runtime environment is constructed from an allowlist, with exactly:

```text
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
QT_QPA_PLATFORM=offscreen
QT_QUICK_BACKEND=software
```

Repository-specific CTest settings, including DPI scales, remain effective.
Suppressions, inherited loader/import overrides, disabled tests, masked exits,
sanitizer findings and incomplete evidence cannot qualify. If LSan or daemon
tests cannot operate within an execution boundary, retain the actual failure;
request only the supported command-scoped execution approval. A boundary retry
is separate evidence and is never a leak suppression.
Every CTest gate uses `--no-tests=error` and verifies exact passing names/results
immediately. The selector uses CTest's regex dialect; a real CTest regression
rejects accidental empty selection rather than trusting a zero exit.

## Risk coverage and remaining inventory

The focused set in `tools/verify_studio_sanitizers.py` covers corrected calibration
A→B→A with held success/error, stale generations, current active binding, controller
destruction while workers finish, real daemon/QML calibration, bridge errors,
Home/Projects/Devices/Scan QML interactions and transitions, scrolling, stress,
and production LiveViews/render rehosting and teardown. The existing surfaces
test additionally destroys visual hosts, checks QPointer invalidation, replaces
preview storage, rehosts the same C++ owned views, and exercises both
bridge-before-owner and owner-before-bridge release with queued updates.
All existing functional assertions remain.

The broader sanitized selection is the actual configured Studio-only inventory
minus the focused set. It includes the remaining model, wire/daemon, DPI/layout,
route/CLI and capture paths rather than assuming historical test counts. Common
headless tests run under the existing full strict sanitizer gate; repeating that
entire common inventory in a second Studio-enabled matrix is unnecessary for
the GUI-specific ownership risks. The complete normal Studio suite still runs.
The verifier compares normalized complete ON/OFF registration/property maps,
including original timeouts, and checks every selected test's log and JUnit result.

## Evidence and trust limits

Raw logs, environment paths, source-file manifests, caches, loader traces and
diagnostic stacks stay private. Publish a short sanitized review report with
source/base/head, content and tool hashes, actual selected/executed counts,
exits, runtime/memory, negative controls and limitations. Never commit raw archives,
binaries, SDKs, caches or copies of unpublished source.

The evidence manifest and source manifest hashes printed on completion must be
retained independently. Reverify with:

```bash
/usr/bin/python3 -B tools/verify_studio_sanitizers.py "$PRIVATE_EVIDENCE" \
  --expected-manifest-sha256 "$EVIDENCE_SHA256" \
  --expected-source-sha "$TESTED_SOURCE_SHA" \
  --expected-source-manifest-sha256 "$SOURCE_MANIFEST_SHA256"
/usr/bin/python3 -B tests/qualification/studio-sanitizers/test_verifier.py
```

The source SHA is the checkout HEAD when tested. Added/modified precommit inputs
are separately identified by a complete content manifest. After committing, compare
every committed file/mode against that tested manifest; do not relabel precommit
runs as executions of a later commit. Receipts and hashes detect inconsistent
evidence, not malicious fabrication by an evidence author.

The historical PR #4/#5 approach is reused selectively: strict standalone leak
controls, retained raw receipts, instrumentation proof, loader verification and
fail-closed parser regressions. CUT-0C constants, protected old-worktree snapshot
assumptions, fixed historical test counts and a settling timer were not reused.
Existing prefixes need fresh version/configuration, binary and runtime-loader
identities. Installed Qt configuration can identify a prebuilt vendor origin;
installer claims or old archive checksums alone do not establish current bytes.

Prebuilt Qt and system dependencies are not assumed instrumented. Authored
Mantis production and Studio test translation units must be instrumented; generated
Protobuf and unrelated test-only ABI/media drivers are reported separately.
Offscreen/software results do not qualify GPU drivers, native Wayland/X11,
Windows/macOS, hardware timing, scanner accuracy or metrology. Maximum RSS from
`time -v` is a process-tree high-water measurement, not summed concurrent memory.
No fixed CI budget or unmeasured performance improvement is claimed.
