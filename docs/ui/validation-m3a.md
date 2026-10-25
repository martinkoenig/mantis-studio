# UI-M3a reliability validation — 2026-10-10

Branch `feature/ui-m3-scan` starts directly from accepted UI-M2b
`61f54ddca229a8eb8bd232e7969d4a648ecb26d6`. No merge, backend, public API/ABI,
protocol, Scan QML or renderer change. Pre-existing untracked task/review files
remain unchanged and uncommitted. [Policy and authority](pointcloud-retry.md).

## Regression and reproduction

Before rebuilding the bridge, the accepted baseline bridge-test executable ran
against the extended authenticated fixture with a stable-project missing-cloud
mode. Two consecutive refreshes made **two** automatic data calls; the fixture's
required count of one failed. The original output is retained in
`evidence/retry/baseline-reproduced.log`. Its initial counter grouped by stable
fixture mode; the final fixture records exact confirmed project paths. This is a
data-request failure, not just an assertion of internal policy state.
To reproduce independently, build the accepted commit in an isolated checkout,
then run this branch's `tests/integration/studio_bridge.py` with that baseline
build directory as its argument. The final fixture also fails its missing-cloud
count assertion on the original executable (two requests rather than one).

The final `studio-bridge-errors` test extends the same public `mantis::client::Client`
fixture, preserving its original changing-project, structured operation/mapping,
authentication and transport checks. The fixture still asserts the exact six
original mutating operation requests, each executed once. Read-only Events replies
carry fixture data counters; mode transitions use its existing fixture command.
No runtime service/API or test-only production command is added.

| Case | Wire assertion |
| --- | --- |
| Permanent missing | 41 confirmed 500-ms-equivalent refreshes: **one** data request; 40 bounded notifications; unchanged artifact issue |
| Corrupt map | One initial request; 20 suppressed polls; one failed explicit retry; unchanged descriptor repair suppressed until one successful explicit retry |
| Fresh candidate | One immediate successful request despite prior suppression; unchanged loaded snapshots make zero requests |
| Transient IO | Exactly six automatic attempts at fake 0/2/6/14/30/60s; no requests just before deadlines or after exhaustion; one manual recovery |
| Connection/auth loss | Data failure plus failed second snapshot retains both causes, connected false; unconfirmed polls make zero data requests; explicit recovery |
| Wrong mapped type | One incompatible failure; repeated polls suppressed |
| Invalid access | Unknown/raw/empty/duplicate/mismatched-project selection makes zero data requests; older-cloud success does not erase current candidate suppression |
| Invalid clock | Throwing injected clock permits first request, then no deadline/no automatic requests; explicit recovery |
| Published hash change | Same ID with changed real descriptor metadata becomes immediately eligible |
| Project change | Identical artifact ID in A and B attempts once each with separate authority |
| Late completion | Held real data reply, confirmed B snapshot, then released A reply cannot install selection, cloud or retry state in B |
| A → B → A | Earlier generation result is rejected even when the exact path returns; no notification or policy mutation |
| Pending user request | Explicit retry, refresh and pipeline invocation under the held watcher lease produce no duplicate data access or operation |

Pure `studio-cloud-retry` tests cover every actual permanent/transient status,
unknown numeric/untyped categories, all backoff deadlines, count saturation,
manual failure after terminal/success states, each published key field, removal,
absent/negative/overflowing clock values and successful resolution. Tests advance
injected monotonic time; no long backoff sleeps. The asynchronous fixture uses a
bounded held response and completion predicates, not assumed scheduler delays.

The first expanded wire run exposed a dangling protobuf temporary in the new
counter helper and failed with a segmentation fault. It now holds the response
for the iteration lifetime. The failed log is retained and is not counted as a
pass. Existing Home fixture setup now advertises its deliberately missing finalized
PointCloud before the supported explicit request; the mapping/confirmation checks
and original Home operation assertions remain intact.

The final audit refined candidate discovery to copy metadata once for the selected
descriptor instead of once per PointCloud entry. Complete Studio ON suites are
rebuilt and repeated for this final source; preliminary full-run logs remain
separate from the final evidence.

## Complete local verification

All five existing available build configurations are configured and built in
Debug, then run their complete CTest graphs. Final counts and retained logs are
listed in [the evidence inventory](evidence/retry/README.md).

Ubuntu 24.04 x86_64, GCC 13.3, Qt 6.4.2, OpenCV 4.6.0, worktree mounted as `/work`:

```bash
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=ON -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64 --parallel 3
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 --output-on-failure
```

The same configure/build/test commands run for `build/qt64-headless` with
`-DMANTIS_BUILD_STUDIO=OFF` and `build/qt64-headless-sanitizers` with
`-DMANTIS_BUILD_STUDIO=OFF -DMANTIS_SANITIZE=ON`. The latter test uses
`docker exec -e ASAN_OPTIONS=detect_leaks=1 -e UBSAN_OPTIONS=halt_on_error=1`.
The headless graphs preserve Qt-free dependency/ABI/plugin boundaries.

Host x86_64, GCC 15.2, Qt 6.9.2, existing OpenCV 4.10 dependency bundle:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build --preset linux-debug --parallel 3
OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ctest --preset linux-debug

cmake -S . -B build/qt69-sanitizers -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=ON -DMANTIS_SANITIZE=ON -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build build/qt69-sanitizers --parallel 3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ctest --test-dir build/qt69-sanitizers --output-on-failure
```

Focused iteration command (5/5 after lifetime repair and expanded cases):

```bash
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 --output-on-failure \
  -R '^(studio-cloud-retry|studio-bridge-errors|studio-devices-wire|studio-projects-wire|studio-home-wire)$'
```

Full Studio graphs retain real Qt Home/Projects/Devices/M0/Calibration captures,
source/freshness, focus, resize and warning assertions, capture/replay/acceptance,
CLI aliases/export, plugins, ABI, streaming and performance checks. Repeated
suppressed refreshes perform no mapped point reads/copies or conversions. No new
acquisition hot-path code or retry timer is introduced; software fixtures and
existing performance suites do not certify physical capture latency.

## Independent handoff

Both native x86_64 and ARM64 Studio ON CI graphs register the new policy test and
extended bridge fixture. The existing five-job matrix remains mandatory. Both
ON jobs upload `ui-m3a/wire.log`, `wire-requests.json` and `revision.json` with the
exact Actions SHA/run URL, alongside existing UI and CTest evidence.

Only the feature branch is committed/pushed. One nonblocking status check targets
the exact pushed SHA; incomplete CI is reported PENDING with its run URL, without
waiting or polling. The independent reviewer checks all five required jobs and
code before acceptance. Local native ARM64, Windows/macOS and real scanner
verification are unavailable; existing backend project/replay isolation remains
pending. READY FOR REVIEW records completed local work, not independent ACCEPT.
