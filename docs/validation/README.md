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
recorded separately from the corrected 4 ms pairing still pending on Q6A.
