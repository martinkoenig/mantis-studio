# Validation evidence

These logs preserve the historical v0.1 source delivery process tests.

- `debug-ctest.log`: desktop, actual Qt shell and viewport, 4/4 passed.
- `headless-ctest.log`: Qt-free runtime and clients, 4/4 passed.
- `sanitizers-ctest.log`: ASan/UBSan, 4/4 passed with `detect_leaks=0` in the resumed execution environment.
- `leaksanitizer-environment-limitation.log`: separate failed LeakSanitizer startup due to restricted process inspection. This is retained rather than represented as a passing leak test.

The full conditions and platform scope are in [the validation report](../architecture/validation.md). `<SOURCE_ROOT>` replaces the transient workspace location in the logs; test output and results are unchanged.

The validation report's v0.2 section records nine passing suites for desktop,
headless and ASan/UBSan, including leak detection, plus inspected native ARM64 CI.
The `v0.2-*-ctest.log` files preserve the final local runs with source-root paths
normalized, matching the historical log convention.
`v0.2-acquisition-release-benchmark.json` preserves the actual informational
64-FrameSet fixture output; it is not sustained storage or hardware evidence.

The Q6A hardware-gap continuation adds thirteen-suite results in
`v0.2-q6a-gap-{debug,headless,release,sanitizers}-ctest.log`, covering RAW8/Y10P,
scoped media setup/rollback, quiet idle control sockets, clients, actual Studio
preview, frozen ABI, storage and replay. Leak detection is enabled for the
sanitizer result. `v0.2-q6a-gap-y10p-release-benchmark.json` records the 64-FrameSet
1280×720 packed fixture; it is not Q6A or sustained storage evidence. Physical
status and user-reported earlier findings are separated in the validation report.


The software-pairing continuation has sixteen passing suites in
`v0.2-software-pairing-{debug,headless,release,sanitizers}-ctest.log`. These include
six-frame startup offsets in both directions, bounded nearest timestamp pairing,
late-readiness deadline rejection, RAW8/Y10P exact replay, explicit observation
accounting, Python stop errors and the public-client Q6A handoff command. ASan,
UBSan and leak detection are enabled. The user-validated earlier real capture is
recorded separately from the new user-validated corrected 4 ms Q6A result
(140 FrameSets, -2.861 ms selected delta, zero loss/errors and verified replay).
See the current continuation in [the validation report](../architecture/validation.md).


The official harness continuation preserves seventeen-suite Debug Studio ON,
headless Debug, headless Release and leak-enabled ASan/UBSan results in
`v0.2-q6a-harness-{debug,headless,release,sanitizers}-ctest.log`. Runs deliberately
exported both X1 variables; the generic acceptance CTest additionally injects a
discoverable fixture to prevent an environment-isolation regression. Twelve
hardware-free harness checks cover scoped cleanup and failure reporting. The
pairing algorithm and reference profile were unchanged at that checkpoint.
ShellCheck was unavailable;
Bash syntax and Python compilation checks passed. Real Q6A harness execution and
sustained NVMe/equivalent storage acceptance remain pending.


The later real harness follow-up qualifies the favorable 4 ms successes: smoke
passed 123/123 FrameSets at +1.686 ms (+7 native offset), while full passed both
17-suite software runs and disabled-link discovery but failed cold acquisition's
4 ms criterion. Thus 4 ms is not robust for arbitrary free-running startup phase.
The new 5 ms period-derived software bound and counted steady-state re-alignment
are candidates awaiting Q6A evidence; see revised ADR-026 and the latest validation
section. The earlier logs/evidence are preserved as historical results.


`v0.2-free-running-{debug,headless,release,sanitizers}-ctest.log` retain the
17-suite phase/drift correction runs. Debug is console output; the others are
CTest's detailed LastTest logs. Sanitizers include leak detection. The tests
add arbitrary/half-period phases, 4.0–4.3 ms distances, 25 ppm drift, counted
boundary crossings, strict native-gap/budget failures, bounded recording with
no hidden loss, and exact RAW8/Y10P replay. These are software fixture results;
5 ms/counted realignment on real Q6A remains pending.


`v0.2-observed-period-{debug,headless,release,sanitizers}-ctest.log` preserve all
four 17/17 guard-removal runs at implementation checkpoint `69fd7df`. They use
`TMPDIR=/dev/shm` with both caller X1 variables exported; sanitizers enable leak
detection. Isolated 12.384 ms intervals remain diagnostic when valid ≤5 ms
correspondence exists. Real 6 ms brackets still reject with structured evidence,
including an initial failure without a capture handle. RAW8/Y10P recording,
complete accounting and exact two-pass replay remain covered. Thirteen harness
checks pass, including preservation of failure diagnostics. Bash syntax and
Python compilation pass; ShellCheck was unavailable.

The real 5 ms Q6A run at `4ec71d9` passed Debug/Release 17/17 and disabled-link
discovery but was blocked by the now-removed observed-period guard before actual
correspondence could be validated. The 5 ms pairing policy itself has not failed
on Q6A; real execution after guard removal and sustained storage remain pending.


Final v0.2 Q6A acquisition evidence: the user ran
`./scripts/validate-x1-q6a.sh --full` on real hardware at
`fad4df6439e88c7ba4f265c532343c3317e93da4`. Debug and Release were 17/17 PASS;
disabled-link discovery and plugin-owned Y10P 1280×720 setup passed; software
timestamp correspondence used the 5 ms reference with -3.652 ms final selected
delta, +8 native offset, 8/0 startup exclusions, 0/0 steady-state exclusions and
zero pairing failures. The run recorded 123/123 FrameSets with zero raw
drops/saturation, FINALIZED RawCapture and deterministic replay/raw-integrity PASS.
Sustained storage and physical synchronization remain outside this acceptance.
