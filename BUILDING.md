# Building Mantis Studio

For demo, SDK, hardware and contributor routes, see the [documentation home](docs/README.md).

Build the daemon, CLI and plugins first, then start Studio as a client of the daemon.
The default Virtual Scanner provides a hardware-free starting point; building or
running it does not establish physical scanner accuracy or timing.

- **First Linux build:** [prerequisites and Debug build](#linux-reference-environment), then [run the daemon and Studio](#running).
- **Without Qt:** use the [headless preset](#qt-free-headless-build).
- **Contributor checks:** see [CONTRIBUTING.md](CONTRIBUTING.md), [sanitizers](#sanitizers) and [Architecture CI](#architecture-ci-compiler-cache-and-events).
- **Application or plugin development:** start with the [Client SDK](docs/client-sdk/README.md) or [native Plugin SDK guide](docs/plugin-development/README.md).
- **Physical X1 setup:** see [native acquisition](#v02-native-acquisition) and the separate [calibration procedure](#m8a-geometric-calibration-characterization).

## Linux reference environment

The [Architecture workflow](.github/workflows/build.yml) targets Ubuntu 24.04
x86_64 and native ARM64, with Studio enabled and disabled. Its package list and
[CMakeLists.txt](CMakeLists.txt) are the build prerequisites; dated validation
records describe what was actually run. This guide does not qualify a new build
or extend platform support.

| Requirement | Current source requirement |
| --- | --- |
| C/C++ toolchain | C++23 with `std::expected`; CI installs Ubuntu's `g++`. |
| CMake and generator | CMake 3.25+ for the version-6 [presets](CMakePresets.json), with Ninja. Manual configuration has a CMake 3.24 project minimum. |
| Core dependencies | Protobuf 3.15+ and `protoc`, SQLite, nlohmann-json 3.10+, and Python 3 with `google.protobuf`. |
| Calibration dependencies | OpenCV 4.6+ with `core`, `imgproc`, `calib3d` and `aruco`, including for headless builds. |
| Studio dependencies | Qt 6.4+ Core, Gui, Quick, QuickControls2 and Concurrent; Qt Test when testing is enabled; the QML runtime modules below. |

Install the build and Studio runtime packages on Ubuntu 24.04:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libsqlite3-dev \
  protobuf-compiler libprotobuf-dev nlohmann-json3-dev python3 python3-protobuf \
  libopencv-dev libopencv-contrib-dev \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript
```

Review the package manager's confirmation prompt. The Python interpreter used for tests must have `google.protobuf` installed; the Ubuntu package supplies it to `/usr/bin/python3`.
Run the build and test commands from the repository root.

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
```

For manual configuration instead of a preset:

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/debug --parallel 4
```

Outputs are in `build/debug/bin`, plugins/manifests in `build/debug/plugins`, recipes in `build/debug/recipes`, and the generated Python package in `build/debug/python/mantis`.
Do not copy only an executable away from its matching plugin/recipe directories.

## Running

After the Debug build and tests, start both processes from the repository root.
For the headless preset, use `build/headless/bin` for the daemon and CLI and omit
Studio. Keep this shell open for the client commands below. For offline Studio modes, see
the [UI launch guide](docs/ui/README.md#launch-and-data-sources).

```bash
export MANTIS_TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
export MANTIS_PORT=47321
./build/debug/bin/mantisd --project "$PWD/Demo.mantis" &
./build/debug/bin/mantis-studio
```

Use the same token in all client processes. The daemon binds only loopback and requires a token of at least 16 characters. The token is not written to project files or printed in logs. `--project`, `--plugins`, `--plugin-host`, `--recipes`, and `--port` override defaults.

Studio has capture start/stop, recipe selection, jobs/cancellation, plugin failure/re-enable controls, artifact selection, point-cloud orbit/zoom, PLY export and diagnostics. The viewport uses a deliberately small CPU painting renderer.

```bash
./build/debug/bin/mantis-cli --help
./build/debug/bin/mantis-cli workflow "$PWD/cli-output.ply"
./build/debug/bin/mantis-cli shutdown
```

For an unattended UI smoke run:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build/debug/bin/mantis-studio --quit-after 5000 --screenshot /tmp/mantis.png
```

The acceptance runner automatically starts its own daemon on a free port and uses a temporary project. It does not require an existing daemon or physical scanner.

## Qt-free headless build

```bash
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless
```

Qt is not located when `MANTIS_BUILD_STUDIO=OFF`. Core, runtime, CLI, protocol, plugin host and Python tests still build and run.

## Sanitizers

```bash
cmake --preset sanitizers -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/sanitizers --output-on-failure
```

The crash fixture deliberately aborts. It is successful only when the parent detects the failure and the other processes remain operational. Do not remove the fixture to make an error log appear clean.

The existing sanitizer CI job also runs one isolated, Qt-free detector-effectiveness
canary after CTest, without adding a job or duplicating the suite:

```bash
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 -B tests/qualification/headless-lsan/test_verifier.py \
  build/sanitizers/bin/mantis-headless-lsan-canary /usr/bin/c++ \
  build/sanitizers/headless-lsan-evidence
```

Only Linux/GCC headless Debug sanitizer builds with `BUILD_TESTING=ON` build this
test executable. Normal, Studio, Release and non-testing target graphs exclude it;
other configurations explicitly report `UNQUALIFIED` at configure time. It is
never linked into a product executable. This narrow supported configuration uses
GCC's dynamically linked ASan/UBSan runtimes and symbolized allocation stacks.
Missing runtime/symbols, unsupported detector overrides and LSan sandbox failures
fail qualification; they are never reported as a passing detector control.

The verifier checks a fresh child PID/executable/runtime, random nonce and bounded
allocation size against the complete LSan diagnostic and actual nonzero exit.
It exercises disabled detection, real uninstrumented C++, unrelated failures,
stale/fabricated output, malformed reports and bounded timeout/cancellation cleanup,
including reaping a forked fixture. The Linux subreaper setting is scoped to the
verifier process and restored after each invocation. Both output and runtime are
bounded; local execution receipts go only to the ignored build directory. These
checks detect realistic stale/substituted fixtures, not a compromised build host,
and do not certify every product lifetime or long-running memory growth.

## Architecture CI compiler cache and events

Architecture runs the full five configurations for every `pull_request` target,
direct pushes to `develop`, `main` and `integration/**`, and manual dispatches.
Ordinary feature pushes deliberately do not start Architecture, with or without
an open PR: open a PR for required remote acceptance. Default PR checkout still
tests GitHub's merge result. Job names, serial CTest, sanitizer options, native
ARM64, UI repeats, diagnostics and artifact collection are unchanged. The separate
agent-efficiency workflow retains its existing path/event policy.

All five jobs install Noble's `ccache` package and configure both CMake compiler
launchers. Only the ccache directory is restored; each checkout still configures,
builds and tests. Cache misses compile normally. Neither a hit nor cache statistics
can skip validation or turn a compiler/build/test failure into success.

Keys have the form
`ccache-v1-Linux-{X64|ARM64}-studio-{ON|OFF}-sanitize-{ON|OFF}-{ISO-year}-W{week}`.
Restore falls back only within that exact schema/OS/architecture/Studio/sanitizer
prefix. The key isolates configurations; ccache additionally validates compiler
**contents**, source/includes and options. Working-directory hashing stays enabled
for debug information; no path remapping or sloppiness is enabled. The isolated
configuration lives outside the restored directory, with no remote ccache backend.
Unexpected pre-existing ccache environment overrides fail preparation. Restored
files remain untrusted: ccache checks detect accidental corruption, not malicious
forgery, so cache keys are not an authenticity boundary.

PRs, integration pushes and dispatches restore only. Only successful `push` runs
on `develop`/`main` may save, after all validation, and only when the current weekly
key is absent. Before saving, cleanup enforces the approximately **384 MiB** local
limit. This starts conservatively at about 1.875 GiB for five configurations per
trusted branch per weekly generation before archive compression. The first
successful run creates an immutable snapshot for that week; corrections do not
publish per-commit snapshots. A new UTC ISO week permits refresh from the latest
compatible snapshot. This bounds publication to at most ten entries/week across
the two trusted branches, not a separate hard repository storage quota. GitHub's
unused-cache eviction and repository quota apply; no cache-deletion API or quota
configuration is added. A 384 MiB cache may evict entries from a large build;
measure hosted hit rates before adjusting it.

GitHub's branch scope governs restoration: PRs can read their base/default branch
caches, including permitted fork-PR access; sibling/child scopes are not bypassed.
This workflow never saves PR snapshots, never uses `pull_request_target`, and
requires no extra repository permissions. No credentials, environment dumps,
build directories, executables or test results belong in the cache path.

Each job logs the ccache version and effective correctness settings, resets
restored statistics, and summarizes current hits/misses, cacheable/uncacheable
calls, size, build seconds and restore/save outcomes even after validation fails.
Build timing excludes restore/save, dependencies and tests; their Actions step
durations must be included in any hosted end-to-end comparison. An empty-cache
run and a warm local compiler probe are not evidence of a hosted CI speedup.

The first-party restore/save actions are pinned to the verified v6.1.0 commit
(Node 24, cache-service v2; hosted runners supply the required runtime).
References: [action release](https://github.com/actions/cache/releases/tag/v6.1.0),
[cache scope/immutability/eviction](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching),
[ccache 4.9 correctness/configuration](https://ccache.dev/manual/4.9.html),
[Noble ARM64 package](https://packages.ubuntu.com/noble/arm64/ccache).
Run the focused helper/event regressions with ccache, GCC/G++, and `python3-yaml`
installed: `python3 -B tests/ci/compiler_cache_test.py`. They supplement the full
project tests; they do not replace them.

## Python packaging

For development, use `PYTHONPATH=build/debug/python`. The install example also
requires Ubuntu's `python3-venv` package. To install a wheel/package, first
generate the wire module in the package source:

```bash
protoc -I protocol/protobuf --python_out=sdk/python/mantis protocol/protobuf/mantis.proto
python3 -m venv .venv
.venv/bin/python -m pip install ./sdk/python
```

The generated `mantis_pb2.py` is build output and is not hand-maintained.

## Windows and macOS

These remain architecture targets, not verified releases. The platform layer includes Windows dynamic library, process, mapping, locking and socket branches; macOS uses the POSIX branches. Use a C++23 toolchain with `std::expected`, Qt 6.4+, Protobuf, SQLite and nlohmann-json. Native packaging, Unicode path certification on Windows, platform-specific durability testing, and non-Linux CI still need validation. Do not advertise a platform as supported merely because CMake can configure it.

## Troubleshooting

- **Runtime unavailable:** start `mantisd`; check that token and port match.
- **Project already open:** another daemon holds the project lock. Stop that daemon; never remove a live process's lock as a workaround.
- **Qt module missing:** install the QML runtime packages above, not just the development headers.
- **No device:** inspect `mantis-cli plugins list`; keep generated manifests with the matching libraries.
- **Export path exists:** choose a new filename. The Skeleton does not overwrite exports.
- **RECOVERABLE:** `mantis-cli artifact recover ARTIFACT_ID` validates committed chunks and finalizes them.

## v0.2 native acquisition

The X1 plugin builds only on Linux and uses kernel media/V4L2 headers and the
existing nlohmann-json dependency. No media-ctl/v4l2-ctl executable is required at runtime. No OpenCV, libcamera, udev library or new
third-party dependency is added. Generic headers remain Qt/kernel independent.
The versioned example profile is `profiles/x1-q6a.json`; set MANTIS_X1_PROFILE in
the daemon environment. Discovery refreshes while acquisition is idle. Restart
or stop an active capture before rediscovery after reconnect.

For sustained recording and informational benchmarks use Release:

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMANTIS_BUILD_STUDIO=OFF -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/release --parallel 4
./build/release/bin/mantis-acquisition-benchmark /tmp 64 Y10P
./build/release/bin/mantis-acquisition-benchmark /tmp 64 GREY
```

Replace `/tmp` with a directory on the actual capture filesystem for storage
measurements. The benchmark creates/removes its own temporary project and reports
construction, bounded queue, append, finalization, validated replay/index and peak
RSS observations. Cache-backed short results do not certify sustained media speed.
The preferred Q6A validation workflow is the repository-owned harness:

```bash
./scripts/validate-x1-q6a.sh --smoke
./scripts/validate-x1-q6a.sh --full
# Later storage candidate (still a smoke check, not sustained acceptance):
./scripts/validate-x1-q6a.sh --full --storage /mnt/mantis-nvme --duration 10
```

Smoke configures/builds the matching headless Release tree, discovers the real X1,
captures for one second, finalizes and verifies deterministic replay and raw
integrity. Full first builds Debug with Studio and runs all Debug tests, then
builds/tests Release with `TMPDIR=/dev/shm`; it also checks discovery with only
the four mutable measurement links disabled. There are now 17 Linux CTest suites
(the previous 16 plus harness regression coverage); exit status decides success.
The generic acceptance test isolates the caller's X1 environment.

The harness requires `v4l-utils` for independent hardware read-backs, and defaults
to `/dev/shm` only after a capacity check. Kernel, cameras and permissions must
already be provisioned. It stops an active `mantis-cameras.service` for the run
using a specific `sudo systemctl` action, then restores its prior running state.
The source profile is preserved. A printed, saved runtime snapshot sets
`disable_conflicting_links=false` so conflicts fail without changing other routes.
No root shell or global media reset is used. `--profile`, `--port`, `--build-dir`,
`--debug-build-dir`, `--output-root`, `--jobs` and `--disable-measurement-links`
provide explicit controls; use `--help` for details. The interpreter defaults to
`/usr/bin/python3` and can be selected with `MANTIS_VALIDATION_PYTHON`.

Timestamped reports are in `validation-output/x1-q6a-*/`. Capture projects remain
on the selected storage; `/dev/shm` data lasts until reboot/removal. Failed runs
retain logs and data. See [the Q6A validation guide](docs/hardware/x1-q6a-acquisition-validation.md)
for the harness contract and lower-level manual diagnostics, ten-second storage
criteria and process-kill procedure. Real 4 ms starts passed (including the new
harness smoke: 123/123 FrameSets, +1.686 ms, native offset +7), but a later full
cold disabled-link start failed the 4 ms criterion after both software suites
and discovery passed. Therefore 4 ms is not robust for arbitrary free-running
phase. The accepted reference uses **5 ms software correspondence** and
explicitly counted, bounded steady-state re-alignment; the final real Q6A full
harness passed at `fad4df6439e88c7ba4f265c532343c3317e93da4`. Neither timestamp
tolerance measures optical skew.
Sustained NVMe recording and real crash recovery remain pending.

The reference profile is version 2: explicit sensor/bus/entity routes, 1280×720
Y10P / Y10_1X10 and VBLANK=196. Existing version-1 profiles remain externally
configured and RAW8 remains supported. Changing a profile to version 2 requires
explicit routes and setup ownership; it is not an implicit migration. Do not run
an external camera setup service concurrently with plugin-owned setup.
`v4l-utils` is optional for hardware inspection only. Kernel, DTBO/driver, boot
camera enablement and permissions must already be provisioned on the Q6A.
The requested target FPS, driver interval, VBLANK read-back and measured receive
FPS are different diagnostics. VBLANK=196 does not certify exactly 120 FPS.
A full-rate 720-line Y10P capture requires 276.48 MB/s payload before overhead;
the user's 32.04 MB/s microSD observation is not a recorder throughput limit.
NVMe or equivalent storage will be evaluated later.

Software correspondence uses two pending observations per camera, at most 32
startup exclusions and at most two steady-state exclusions between published
pairs, all bounded by the profile stall timeout. The default software tolerance
is `ceil(600000000 / requested_fps)` ns: half a nominal period plus 20% headroom,
**5,000,000 ns at 120 FPS**. At measured ~119.27 FPS, half-period is ~4.192 ms.
The plugin rejects software bounds below nominal half-period. Observed native
periods, half-periods and maxima are diagnostics; an isolated long interval does
not veto a valid cross-camera pair. Actual nearest/bracketing timestamps enforce
the configured correspondence bound. The plugin never widens a bound or
retries for a favorable startup phase. Review old explicit 4 ms profiles and
restart the daemon after an intentional profile update. The harness validates
5 ms and preserves the user's profile.

Inspect `pairing_mode`, `native_sequence_offset`, `startup_unmatched_left/right`,
`steady_state_unmatched_left/right`, `pairing_exclusions`, observed periods and
`paired_v4l2_delta_ns`. Zero recorder drops covers published FrameSets; every
excluded observation is counted separately. The exact decisions and pixels are
persisted and replayed without pairing again. Hardware-configured mode retains
strict equal-counter checks, its configured timestamp limit and a 4 ms default;
it permits no re-alignment. SyncQuality remains software and exposure skew is
unavailable. See [ADR-026](docs/adr/026-bounded-software-observation-pairing.md).
No build/runtime dependency changed.

The real 5 ms Q6A full run at `4ec71d9` passed both 17-suite software builds and
disabled-link discovery, but a conservative observed-period guard blocked
`capture.start()` before correspondence could be validated. That guard is now
removed; the final real 5 ms full-harness run at `fad4df6439e88c7ba4f265c532343c3317e93da4`
passed Debug/Release 17/17, disabled-link discovery, 123/123 FrameSets, zero raw
loss/saturation, finalization and deterministic replay. Pairing failures retain
candidate distances and period diagnostics in `pairing.json`; a startup failure without a
capture handle also preserves a JSON `capture.diagnostics` event in the retained
project's `diagnostics.log`. Sustained-storage acceptance remains pending.

## M8a geometric calibration characterization

Use `./scripts/validate-x1-calibration.sh --prepare` and follow the
[real Q6A operator procedure](docs/hardware/x1-geometric-calibration-validation.md).
M8a tooling/CI PASS is software validation only. M8 hardware acceptance remains
pending; real characterization and evidence-based threshold review are required.
