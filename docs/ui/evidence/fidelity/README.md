# UI-M1 fidelity screenshot evidence

- [Approved target](../../reference/01-home.webp): user-supplied concept, not data authority.
- [Before](before-mock-1536.png): actual application, reviewed `090fa253675699ce459f5876f0a249616c36e0dc`, Qt 6.4.2 offscreen/software, 1536×1024.
- [After](after-mock-1536.png): actual rendered final Home QML test fixture, Qt 6.4.2 offscreen/software, 1536×1024.
- [Side-by-side](reference-before-after.jpg): reference / before / after at original resolution; 40px caption strip only. Reference pixels appear only in this review document, never in application assets.
- [Native](native-wayland-1536-logical.png): Qt 6.9.2 normal Wayland desktop window, OpenGL QRhi, 1536×1024 logical / 1920×1280 pixels at the desktop's 1.25 scale. A rendered-frame capture, not a physical scanner, driver stability or screen-reader acceptance claim.

The [numbered inventory](../m1-fidelity-delta.md) and
[validation record](../../validation-m1.md) explain differences and source boundaries.

Reproduce the state/size matrix with the registered tests:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build/qt64 -R '^studio-home-(qml|wire)$' --output-on-failure
```

Outputs: `build/qt64/ui-m1/{screenshots,wire}/`. In CI the build directory is
`build/ci`, uploaded by both Studio ON jobs with complete CTest and wire logs.
The 38 M1 PNGs include mock, confirmed/unconfirmed/empty/stale live, hybrid and
showcase scroll at 1080×720, 1536×1024 and 1920×1080, plus local guide/focus,
long activity and real-public-client failure/reconnect states.

Application-only baseline command (same mode is usable for final manual capture):

```bash
docker exec -e QT_QPA_PLATFORM=offscreen -e QT_QUICK_BACKEND=software \
  mantis-ui-m0-qt64 /work/build/qt64/bin/mantis-studio \
  --ui-mode=mock --workspace=home --window-size=1536x1024 --quit-after=1000 \
  --screenshot=/work/build/ui-m1-fidelity/before/actual-mock-1536.png
```

Native check used the host build and existing extracted OpenCV dependencies:

```bash
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
QT_QPA_PLATFORM=wayland QT_LOGGING_RULES='qt.scenegraph.general=true' \
  ./build/debug/bin/mantis-studio --ui-mode=mock --workspace=home \
  --window-size=1536x1024 --quit-after=2500 \
  --screenshot=build/ui-m1-fidelity/native-wayland-mock-1536.png
```

Retained runtime log: `build/ui-m1-fidelity/native-wayland.log`, reporting a
threaded render loop and successfully created OpenGL QRhi. No Qt/QML warning.
