# Standalone Qt Quick sanitizer qualification (UI-QT-SAN-1A)

This package qualifies a standalone Qt Quick environment before UI-QT-SAN-1B
attempts complete Studio sanitizer validation. It changes no Mantis product
behavior, build graph, public contract or system Qt package. Independent review
and required exact-commit remote CI remain acceptance gates.

## Source and environment

Qualification started on `ao/ui-qt-san-1a/root` at
`d2614b96304ff5fd45db8f09076da058afb9230d`, with a clean isolated worktree.
The protected `mantis-studio-3` worktree was checked for existence only; no
commands built, tested or modified anything there. The existing
`/home/martin/src/mantis-studio/build/baseline-studio-sanitizers` directory was
not reused or modified.

Original input: `/tmp/m3d1-qt-leak-probe.cpp` (1447 bytes). SHA-256, recorded
before copying and checked again after execution:

```text
2cb0ef9093ebfb0631e1623f577c6856dfaada0b7569cee49d3f92894cc5e3ab
```

`tests/qualification/qt-sanitizer/qt-leak-probe.cpp` is byte-identical. Its
formatting, 200 ms event loop, image generation, QML, cleanup and lifetime
behavior are preserved. It is intentionally exempt from reformatting. No
arguments are passed, so the original optional cleanup branch is unchanged and
unused. The only build adaptation is the independent CMake project with the
original Core/Gui/Qml/Quick modules and explicit instrumentation.

The original probe and four `/tmp/m3d1-qt-leak-probe-*.log` files are immutable;
copies and checksums are retained. The original probe generates a fixed
`/tmp/m3d1-qt-leak-probe.png`. Execute serially, with no concurrent use of this
path. Its pre-existing contents were copied before execution and remain
byte-identical afterward. Private `/tmp` namespaces could not be used because
the VM denies user namespace UID mapping. No source substitution was made.

Actual host: Ubuntu 24.04.5 LTS x86_64, GCC **13.3.0**, CMake **3.28.3**, Ninja
**1.11.1**, Python **3.12.3**. Dynamic sanitizer runtimes are the VM's
`libasan.so.8` and `libubsan.so.1`, Ubuntu package **14.2.0-4ubuntu2~24.04.1**;
glibc is **2.39-0ubuntu8.9**. The compiler was not upgraded. System Qt packages
are `qt6-base-dev 6.4.2+dfsg-21.1build5` and
`qt6-declarative-dev 6.4.2+dfsg-4build3`.

## Isolated installation

Installer: **aqtinstall 3.3.0**, in
`$HOME/.local/share/mantis-tools/aqt-venv`. Candidate: exactly **Qt 6.9.2**,
`linux_gcc_64`, prefix `$HOME/opt/mantis-qt/6.9.2/gcc_64`. No version/compiler
fallback, container, source-built Qt, global loader/import override or Ubuntu
Qt replacement was used.

On this VM `python3 -m venv` failed because `ensurepip` is unavailable. Pip was
bootstrapped inside the dedicated venv from the published **pip 25.2** wheel,
with SHA-256
`6d67a2b4e7f14d8b31b8b52648866fa717f45a1eb70e83002f4331d07e953717`.
The following reproduces that bootstrap without changing system Python:

```bash
python3 -m venv --without-pip "$HOME/.local/share/mantis-tools/aqt-venv"
mkdir -p /tmp/mantis-qt-installer
cd /tmp/mantis-qt-installer
python3 - <<'PY'
import hashlib, json, urllib.request
from pathlib import Path
metadata = json.load(urllib.request.urlopen('https://pypi.org/pypi/pip/25.2/json'))
wheel = next(item for item in metadata['urls'] if item['filename'].endswith('.whl'))
data = urllib.request.urlopen(wheel['url']).read()
expected = '6d67a2b4e7f14d8b31b8b52648866fa717f45a1eb70e83002f4331d07e953717'
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit('pip wheel checksum mismatch')
Path(wheel['filename']).write_bytes(data)
PY
"$HOME/.local/share/mantis-tools/aqt-venv/bin/python" \
  /tmp/mantis-qt-installer/pip-25.2-py3-none-any.whl/pip \
  install 'pip==25.2' 'aqtinstall==3.3.0'
"$HOME/.local/share/mantis-tools/aqt-venv/bin/aqt" list-qt linux desktop
"$HOME/.local/share/mantis-tools/aqt-venv/bin/aqt" list-qt linux desktop --arch 6.9.2
"$HOME/.local/share/mantis-tools/aqt-venv/bin/aqt" install-qt linux desktop \
  6.9.2 linux_gcc_64 --archives qtbase qtdeclarative --outputdir "$HOME/opt/mantis-qt"
"$HOME/.local/share/mantis-tools/aqt-venv/bin/aqt" install-qt linux desktop \
  6.9.2 linux_gcc_64 --archives icu --outputdir "$HOME/opt/mantis-qt"
"$HOME/opt/mantis-qt/6.9.2/gcc_64/bin/qtpaths" --query
```

The initial minimal installation's `qtpaths` could not start: `libicui18n.so.73`,
`libicuuc.so.73` and `libicudata.so.73` were missing. Installing Qt's published
matching `icu` archive resolved those dependencies locally; no apt packages
were installed. Required modules are qtbase, qtdeclarative and ICU only.

Package source is `https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_692/qt6_692/qt.qt6.692.linux_gcc_64/`.
aqt followed the publisher's redirect to `https://ftp.fau.de/qtproject/` and
validated downloads using SHA-256. Package version is `6.9.2-0-202508181146`;
the Qt binaries are the published RHEL 8.10 GCC x86_64 build and ICU is the
published RHEL 8.6 x86_64 build. Captured published archive checksums:

| Archive suffix after package version | SHA-256 |
| --- | --- |
| `qtbase-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z` | `80d12a9668a900f0a7acd4607952a7e15a30308c87c1b5529c180d30118f9d79` |
| `qtdeclarative-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z` | `062668f30a0ae73ec5ae84fcc677f1e560c893d1d799664363765c5ffd3f0eaf` |
| `icu-linux-Rhel8.6-x86_64.7z` | `447f9a60f4b698811c79e0438bb0df25de9cd5124a3ea29f8ec67244c6eeb882` |

Raw installer logs, queried metadata/checksums, resolved URLs, full Python
package freeze, OS/package versions and actual `qtpaths` output are in the
evidence archive. SDKs and binaries are not in Git or that archive.
For an exact Python dependency recreation, install the archive's
`installer-python-packages.txt` with the dedicated venv's `pip install -r`.

## Reproduction and strict checks

From a clean checkout with the same compiler and the installed prefix:

```bash
python3 tests/qualification/qt-sanitizer/test_harness.py
scripts/qualify-qt-sanitizer.sh \
  --qt-prefix "$HOME/opt/mantis-qt/6.9.2/gcc_64" \
  --output-dir build/qt-sanitizer/reproduction
scripts/qualify-qt-sanitizer.sh --verify-evidence build/qt-sanitizer/reproduction
python3 tests/qualification/qt-sanitizer/test_evidence.py \
  --evidence build/qt-sanitizer/reproduction \
  --report /tmp/qt-sanitizer-evidence-regressions.json
```

Use a new empty output directory. The harness refuses to overwrite evidence,
requires explicit prefix and exact version, and fails on missing dependencies.
Its `summary.json` uses PASS, FAIL or BLOCKED with actual command/exit receipts;
all failures exit nonzero. A 6.4.2 comparison can be requested with
`--qt-prefix /usr --expected-version 6.4.2`; the actual original leak must still
produce FAIL, not an expected-success conversion.

CMake configuration is equivalent to:

```bash
cmake -S tests/qualification/qt-sanitizer -B build/qt-sanitizer/manual -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_PREFIX_PATH="$HOME/opt/mantis-qt/6.9.2/gcc_64" \
  -DQt6_DIR="$HOME/opt/mantis-qt/6.9.2/gcc_64/lib/cmake/Qt6" \
  -DQUALIFICATION_QT_VERSION=6.9.2
cmake --build build/qt-sanitizer/manual --parallel 4
```

Both executable targets use C++23, `-O0 -g -fsanitize=address,undefined
-fno-omit-frame-pointer` and matching link instrumentation. The harness records
compile commands, `nm -D` ASan/UBSan symbols, ELF dynamic sections/debug sections,
`ldd` dependencies and actual loader initialization paths. It checks exact Qt
version, GCC identity/version, sanitizer runtimes and symbols, and debug info.

The execution environment is an allowlist: system executable PATH, HOME,
`LANG=LC_ALL=C.UTF-8`, a private 0700 XDG runtime directory, and these exact values:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
QT_QPA_PLATFORM=offscreen
QT_QUICK_BACKEND=software
QT_DEBUG_PLUGINS=1
QML_IMPORT_TRACE=1
QSG_INFO=1
QT_LOGGING_RULES='qt.scenegraph.*=true;qt.quick.*=true'
LD_DEBUG=libs
```

There are no suppression files or LSAN_OPTIONS, LD_PRELOAD, LD_LIBRARY_PATH,
QT_PLUGIN_PATH or QML import overrides. Diagnostics augment the unchanged
workload. `QT_DEBUG_PLUGINS` records plugin loading and metadata;
`QML_IMPORT_TRACE` records QML resolution. Filesystem plugins/libraries must
resolve inside the selected prefix. Qt plugin metadata encodes major/minor
(patch zero), so exact patch is additionally proven by `qtpaths`, CMake version
and resolved library filenames. System non-Qt dependencies are allowed.

The actual 6.9.2 Qt libraries resolve under the isolated `gcc_64/lib`, QPA is
`gcc_64/plugins/platforms/libqoffscreen.so`, image plugins are under
`gcc_64/plugins/imageformats`, and the Window QML plugin is
`gcc_64/qml/QtQuick/Window/libquickwindowplugin.so`. QML paths include that
prefix's `qml`, Qt resource URLs and the standalone executable directory. No
6.4.2 Qt libraries/plugins were loaded by the 6.9.2 probe. The comparison uses
system `/usr/lib/x86_64-linux-gnu` Qt libraries and its `qt6/plugins`/`qt6/qml`.

Each accepted run must show 32×32 PNG decoding, software backend initialization,
320×240 software scenegraph rendering and a completed software frame. Merely
exiting without a leak report is insufficient. This is offscreen software
evidence, with no GPU claim.

Raw `LD_DEBUG=libs` output contains 42 `libasan.so.8` symbol-lookup diagnostics
labelled `(fatal)` during interceptor discovery in each final probe and the
deliberate-leak control. They precede later library initialization and main's
workload; the three probes still exit 0, and the control reports its expected
leak and exits 1. These lines are retained without filtering. This behavior is
consistent with the runtime's
[interception code](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/libsanitizer/interception/interception_linux.cpp),
which uses `dlsym` and permits unavailable addresses to return a failed
interception result. This is an interpretation of the loader trace, not a claim
that every libc interceptor is supported. No sanitizer option or error-report
policy was changed to accommodate those diagnostics.

## Results (2026-10-30)

| Check | Actual exit(s) | Result |
| --- | --- | --- |
| Historical immutable 6.4.2 default/basic/cleanup logs | Not independently recorded in those logs | 4544 bytes / 6 allocations |
| Historical immutable 6.4.2 threaded log | Not independently recorded in that log | 4696 bytes / 7 allocations |
| Fresh identical-source 6.4.2, GCC 13.3.0 | 1 | Reproduced 4544 bytes / 6 allocations; image and software render observed |
| Initial identical-source 6.9.2, GCC 13.3.0 | 0 | Clean; image, rendering and isolation observed |
| First harness 6.9.2 positive runs | 0, 0, 0 | Clean; overall harness correctly failed on missing control marker |
| First harness deliberate leak | 1 | LSan detected 123 bytes / 1 allocation; stdout marker buffered/lost |
| Corrected-marker harness 6.9.2 positive runs | 0, 0, 0 | PASS, required workload/isolation each time |
| Final strengthened-verifier harness 6.9.2 positive runs | **0, 0, 0** | **PASS**, required workload/isolation each time |
| Final deliberate leak (marker on stderr) | **1** | **PASS expected failure**, exact 123-byte leak and allocation stack |
| Parser/policy regression suite | 0 | 15 tests passed |
| Actual evidence/CLI regression suite | 0 | 12 cases passed |
| Final raw evidence + manifest verification | 0 | PASS |

Fresh 6.4.2 stacks match the earlier image/texture leak: 4096-byte image data,
160-byte QSG texture, 160-byte QImage data object, 72-byte pixmap, 40-byte
software texture resource and 16-byte shared-pointer control allocation. No
allocation-count adjustment was needed. The historical threaded run's extra
152-byte QThread allocation is retained as a different render-loop observation;
the final comparison used the default software loop without forcing one.

The separate negative-control executable performs an observable 123-byte heap
allocation in `leakForDetectorControl`; it links the same Qt modules, has the
same instrumentation/environment and never enters production binaries or the
normal Studio suite. The first harness failure was a control diagnostic bug:
LSan detected the leak and terminated before buffered stdout was flushed. Only
the marker was moved to stderr. Failed evidence remains retained; no sanitizer
or runtime-isolation check was relaxed.
The archived initial failed and corrected-marker harness, CMake and original
probe inputs are byte-identical; `control-marker-correction.diff` records the
complete control change and `unchanged-qualification-policy.json` records their
hashes. Final review added explicit CMake metadata and two-compilation-record
requirements to the evidence verifier. The final fresh run passed those stronger
checks. `evidence-verifier-strengthening.diff` and
`unchanged-execution-policy.json` prove execution/environment/sanitizer policy
remained unchanged.

Automated negative checks reject a missing prefix, system 6.4.2 when 6.9.2 is
required, missing/tampered evidence, a real-probe nonzero exit, missing image or
render evidence, mismatched Qt libraries/plugins and an absent deliberate-leak
report, missing CMake metadata and an empty compilation database. Actual
CLI/evidence mutations rehash their manifests before verification
where appropriate, proving the raw policy checks reject them independently of
checksum differences. The verifier rechecks required files and actual exits,
not only the summary's PASS label.

## Normal repository gates and retained evidence

After standalone PASS, normal Studio Debug and Headless Debug builds/tests run
in this worker's fresh `build/debug` and `build/headless` caches using system
Qt 6.4.2, four build jobs, sanitizers OFF and sequential CTest. Complete Studio
sanitizer qualification is explicitly deferred to 1B. All following normal
gates passed; the portable archive binds standalone inputs and raw logs.

| Normal repository gate | Exit | Result / elapsed |
| --- | --- | --- |
| Studio Debug configure/build | 0 / 0 | PASS; system Qt 6.4.2, sanitizers OFF |
| Full Studio Debug CTest, sequential | 0 | PASS 184/184; 781.02 s |
| Headless Debug configure/build | 0 / 0 | PASS; Qt disabled, sanitizers OFF |
| Full Headless Debug CTest, sequential | 0 | PASS 144/144; 284.47 s |
| Existing ten UI route tests, each repeated three times, sequential | 0 | PASS 30/30 executions; 440.58 s |

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --parallel 1
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless --parallel 1
ctest --test-dir build/debug \
  -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-scan-qml|studio-scan-stress|studio-scan-surfaces|studio-ui-m0-qml)$' \
  --repeat until-fail:3 --output-on-failure --parallel 1
```

Ignored local qualification evidence is under `build/qt-sanitizer/qt692-verified/`;
its `SHA256SUMS` covers raw logs, source inputs, environment, command/exit
receipts and summary. Initial failed evidence is `build/qt-sanitizer/qt692/`;
the first corrected-marker run is retained in `build/qt-sanitizer/qt692-final/`.
Original inputs, baseline run, installation and repository-gate logs are
retained under `/tmp/ui-qt-san-1a/evidence/`. A portable archive consolidates
those receipts without SDKs, executables, caches or build trees. Validate its
contents with `sha256sum -c SHA256SUMS` after extraction. Re-running the harness
verifier additionally requires the recorded Qt prefix still installed locally.

Portable archive (232,237 bytes):
`/home/martin/.ao/data/artifacts/mantis-studio-4/ui-qt-san-1a-evidence.tar.gz`.
SHA-256:

```text
c8e9bd1fc73b21695d1cbef038fc3ab00052232fff2a4cca667b9fc09ddef5a2
```

## Conclusion and limits

For this VM and GCC 13.3.0, the original standalone Qt Quick 6.4.2 leak
reproduces and is absent in three consecutive strict Qt 6.9.2 runs, while LSan
still detects the deliberate leak. The isolated environment is a supported
candidate for **1B to attempt full Studio sanitizer qualification**. This
does not establish that Studio itself is leak-free or certify Qt globally.
Historical GCC 15.2/Qt 6.9.2 Studio results in `validation-m3c.md` are context,
not evidence for this compiler/environment. No compiler compatibility
investigation or upgrade was needed.

Prebuilt Qt/system dependencies themselves are not rebuilt with sanitizers;
instrumentation is verified for the standalone executables and allocation
interception detects the system Qt leak. Evidence is Linux x86_64 software
only: no hardware, physical timing/accuracy, GPU, Wayland/X11 display, ARM64,
Windows/macOS or production-readiness claim. No full Studio sanitizer suite
has run. Exact-SHA remote CI and independent review remain required.
