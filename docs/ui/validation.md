# UI-M0 validation — 2026-10-08

Implementation and checks ran only in `/home/martin/src/mantis-studio-ui` on
`feature/ui-concept-mock`, based on
`ea40d12777db391b00d10e8f378bc4c447f18fa0` (the supplied v0.4 base). No backend,
protocol, schema, SDK or plugin ABI changes are part of M0.

## Environment and build

Local Linux x86_64: Ubuntu 25.10, GCC 15.2, Qt 6.9.2, OpenCV 4.10, Python 3.13.7.
The first normal configure failed because this machine lacked OpenCV development
files. Existing project dependencies were downloaded as Ubuntu packages and
extracted under ignored `build/deps/root/`; nothing was installed system-wide or
in another worktree. Runtime library lookup used that local directory.

Both configurations and complete builds passed:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"

# This environment-specific prefix was used for builds and tests below:
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build --preset linux-debug --parallel 4
cmake --build --preset headless --parallel 4
```

On a normally provisioned machine, use the standard commands in
[BUILDING.md](../../BUILDING.md) and [UI README](README.md); this local prefix is
not a new repository dependency. Qt Test is used only by the new UI test target,
inside the Studio/BUILD_TESTING CMake boundary. Headless configures/builds without
Qt discovery.

## Executed checks

| Check | Result | Local evidence |
| --- | --- | --- |
| Complete desktop CTest: `ctest --preset linux-debug --output-on-failure` | 46/46 PASS, 104.67 s | `build/debug/ctest.log` |
| Complete headless CTest: `ctest --preset headless --output-on-failure` | 41/41 PASS, 77.45 s | `build/headless/ctest.log` |
| After explicit scrollbars, mock endpoint isolation and compact acceptance override: `ctest --test-dir build/debug -R 'studio-\|^acceptance$\|acquisition-y10p-integration' --output-on-failure` | 7/7 PASS, 28.47 s | `build/debug/ui-m0-final-tests.log` |
| After revision-rail width correction and C++ formatting: `ctest --test-dir build/debug -R 'studio-ui-m0\|studio-calibration-qml\|^acceptance$' --output-on-failure` | 4/4 PASS, 18.42 s | `build/debug/ui-m0-layout-tests.log` |
| Final visible source-label assertions: `ctest --test-dir build/debug -R ^studio-ui-m0-qml$ --output-on-failure` | 1/1 PASS, 3.34 s | `build/debug/ui-m0-label-tests.log` |
| Exact requested mock launch with `/tmp/mantis-ui-m0-home.png`, 5000 ms | PASS, exit 0, no QML warnings | PNG at 1536x1024 |
| Additional local daemon / Virtual Scanner visual run | PASS: connected live/hybrid summaries, calibration, real acceptance export and viewport with 3072 synthetic points | `build/debug/ui-m0/*-review.log`, `screenshots/connected-*.png` |
| Final disconnected live calibration screenshot | PASS, exit 0, no QML warnings | `/tmp/mantis-ui-m0-calibration.png` |
| `git diff --check` | PASS | No whitespace errors |

The full desktop suite includes `studio-calibration-controller`,
`studio-calibration-daemon`, `studio-calibration-qml`, acquisition integration,
calibration/activation contracts, ABI/boundary checks, crash/recovery and the
original asynchronous acceptance workflow. Existing calibration tests were not
weakened. Acceptance now additionally starts with `--workspace=home` at 1080x720;
`--acceptance-export` must override that route and make the real viewport ready.
The daemon-owned capture survives Studio restart as before.

The new QML test checks mouse/keyboard navigation, ten routes and titles, source
and capability transitions, disconnected live/hybrid data, mock authority,
calibration accessibility, preserved render hooks and three resolutions.
The new CLI test checks both argument forms, invalid arguments, a shorter
screenshot timeout, startup without a token, independence from invalid runtime
endpoint variables and twelve mock routes without any TCP connection. A listening
socket with a usable test token traps accidental runtime access.

## Visual review

Generated PNGs are local review artifacts, not concept comparisons or hardware
acceptance evidence. The reference WebPs and UI documentation are versioned;
generated screenshots remain outside tracked source files.

Reviewed all ten mock route captures at 1536x1024, representative Home,
acquisition and calibration captures at 1080x720 and 1920x1080, and connected
live/hybrid Home plus the synthetic acquisition viewport. Also reviewed final
calibration wrapping and the disconnected mode treatments.

- Navigation, page titles, cards and source labels remain readable at all three
  sizes. At 1080x720, foundation content scrolls vertically and the preserved
  three-column acquisition workspace scrolls horizontally/vertically.
- Explicit scrollbars were added after review so overflow is visible.
- The calibration revision rail received a narrow width constraint after a
  long description was found clipped; controller and stage logic are unchanged.
- Live runtime connection is labelled separately from device readiness. Hybrid
  shows actual Virtual Scanner metadata and a separate amber Demo / Mock device.
- Mock has no connected hardware claim, capabilities, measurements or active
  jobs. The legacy example pipeline continues to label its geometry synthetic.
- Supported smoke paths produced no QML binding, type or runtime warnings.

Artifact locations:

```text
/tmp/mantis-ui-m0-home.png
/tmp/mantis-ui-m0-calibration.png
build/debug/ui-m0/cli/cli-mock-home.png
build/debug/ui-m0/screenshots/mock-{home,scan,process,inspect,reverse,automate,projects,devices,plugins,settings}.png
build/debug/ui-m0/screenshots/mock-{home,acquisition,calibration}-{1080,1536,1920}.png
build/debug/ui-m0/screenshots/{live,hybrid}-home.png
build/debug/ui-m0/screenshots/connected-{live,hybrid}-home.png
build/debug/ui-m0/screenshots/connected-live-{acquisition,calibration}.png
```

## Changed files

- `apps/studio/{main.cpp,bridge.cpp,bridge.hpp}`: validated launch options,
  mock transport/command boundary and additive capability metadata.
- `ui/shell/Main.qml`, `ui/design/{Theme.qml,qmldir}`,
  `ui/components/{DeviceSummary,NavigationItem,Panel,SourceBadge,StatusIndicator,StudioButton,StudioIcon}.qml`,
  `ui/state/{AppUiState,MockFixtures}.qml`: shell, design system and provider.
- `ui/workspaces/{AcquisitionWorkspace,FoundationWorkspace}.qml`: preserved
  acquisition and shared M0 panels; `CalibrationWorkspace.qml`: revision width.
- `CMakeLists.txt`, `tests/unit/studio_ui_m0.cpp`,
  `tests/integration/{studio_ui_m0,acceptance}.py`: resource/test wiring and
  stronger acceptance startup coverage.
- `docs/ui/{README,validation}.md`, `docs/ui/reference/README.md` and all ten
  reference WebPs: intent, migration, evidence and approved reference pack.
- The supplied temporary `.ui-m0-task.md` is removed before committing.

## Remaining scope and limits

No known failing acceptance criterion or regression remains from this run.
UI-M1…M7 full workspaces remain planned as described in the
[coverage/migration matrix](README.md#coverage-and-migration). The new Home is
only the M0 foundation. Existing acquisition retains its earlier visual style
until UI-M3 and requires scrolling at compact sizes.

This is local Qt 6.9/Linux x86_64/offscreen evidence. Qt 6.4 baseline compatibility,
ARM64 builds, native window-system accessibility and physical Q6A scanner/laser
behavior were not separately executed here. No physical scanner or laser was
activated. Existing v0.2/v0.3/v0.4 hardware/evidence limitations and the master
roadmap remain unchanged. Independent review remains the next step.
