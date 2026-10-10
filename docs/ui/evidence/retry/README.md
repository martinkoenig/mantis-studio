# UI-M3a local verification evidence

Final implementation on `feature/ui-m3-scan`, parent
`61f54ddca229a8eb8bd232e7969d4a648ecb26d6`, verified 2026-10-10 on Linux x86_64.
All complete CTest processes exited **0**. The final implementation, fixture,
CMake and CI source fingerprints are in [source-sha256.json](source-sha256.json).
They were checked unchanged after final verification. These artifacts are committed
with that source; remote CI uses its separate literal SHA/run receipt.

| Complete configuration | Outcome | Time | Log |
| --- | --- | --- | --- |
| Ubuntu 24.04 GCC 13.3 / Qt 6.4.2 Studio ON | **72/72 PASS** | 359.52s | [Qt 6.4](qt64-full-ctest.log) |
| Ubuntu 24.04 Studio OFF | **42/42 PASS** | 121.79s | [Headless](headless-full-ctest.log) |
| Ubuntu 24.04 Studio OFF ASan/UBSan/LSan | **42/42 PASS** | 250.09s | [Headless sanitizer](headless-sanitizers-full-ctest.log) |
| GCC 15.2 / Qt 6.9.2 Studio ON | **72/72 PASS** | 349.08s | [Qt 6.9](qt69-full-ctest.log) |
| GCC 15.2 / Qt 6.9.2 Studio ON ASan/UBSan/LSan | **72/72 PASS** | 576.70s | [Studio sanitizer](qt69-sanitizers-full-ctest.log) |

Total final complete suites: **300/300**, with no failures. Sanitizers use
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` and retain the normal
repository CTest settings. Configure and final-build logs are included for every
configuration; final builds have no compiler warnings/errors. `clang-format
--dry-run --Werror` on all six changed C++ files and both `git diff --check` and
`git diff --cached --check` pass. Python fixture syntax compilation also passes.
Commands and limitations are in [validation-m3a.md](../../validation-m3a.md).

[Baseline reproduction](baseline-reproduced.log) deliberately fails the new count
assertion: the original executable makes **two** failed automatic data calls in
two stable-project refreshes. [First expanded focused run](focused-first.log) is
**1/2 PASS, 1 FAIL** because the new counter helper iterated a temporary protobuf
response. [Its compiler diagnostic](qt64-focused-build.log) is preserved; holding
the response during iteration fixes the lifetime. Neither failure is presented
as green evidence. [Expanded focused verification](focused-expanded.log) is
**5/5 PASS** after repair.

The final public-client request receipts and test output are retained separately
for [Qt 6.4](qt64-wire-requests.json), [Qt 6.9](debug-wire-requests.json) and
[Studio sanitizers](qt69-sanitizers-wire-requests.json), alongside their `wire.log`
files. In each: stable missing count **1** after 41 confirmed refreshes; transient
count **7** is six bounded automatic attempts plus one explicit recovery; corrupt
count **3** is one automatic failure plus two intentional manual requests. Unknown,
raw, duplicate and mismatched-project IDs make no data calls. Exact project A/B
and held late completion counters remain distinct.

Pre-refinement binaries passed their first complete ON suites 72/72 each;
final rebuilt complete suites above supersede them. Preliminary logs
remain locally in `build/ui-m3a-verification/`, and only completed final logs are
listed as final evidence here. No test was disabled, weakened or skipped.

Physical scanner/metrology, native ARM64 and Windows/macOS execution are not local
evidence. Native ARM64 and the five required exact-SHA Actions jobs are independent
review gates. Existing backend project/replay isolation remains a separate issue.
