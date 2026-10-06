# Building Mantis Studio

## Linux reference environment

Tested in this delivery: Ubuntu 24.04 x86_64, GCC 13.3, CMake 3.28, Qt 6.4.2, Protobuf 3.21.12, SQLite 3.45.1 and Python 3.12.
The native ARM64 CI definition installs the same packages and runs the same tests. Native ARM64 CI and user-reported Q6A build/tests have passed; see the dated validation record for current evidence.

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libsqlite3-dev \
  protobuf-compiler libprotobuf-dev nlohmann-json3-dev python3-protobuf \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript
```

Review the package manager's confirmation prompt. The Python interpreter used for tests must have `google.protobuf` installed; the Ubuntu package supplies it to `/usr/bin/python3`.

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
```

A normal configure command also works:

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/debug --parallel 4
```

Outputs are in `build/debug/bin`, plugins/manifests in `build/debug/plugins`, recipes in `build/debug/recipes`, and the generated Python package in `build/debug/python/mantis`.
Do not copy only an executable away from its matching plugin/recipe directories.

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

## Running

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

## Python packaging

For development, use `PYTHONPATH=build/debug/python`. To install a wheel/package, first generate the wire module in the package source:

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
