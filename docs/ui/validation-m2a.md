# UI-M2a Projects validation — 2026-10-09

## V2 fidelity and docked scrolling correction — 2026-10-09

This section supersedes the earlier geometry/scrolling decisions below, which
remain as the reviewed UI-M2a baseline record. Starting revision:
`5b68b580bb8f7d72df60c80eeaf970297dda6019`; branch `feature/ui-m2a-projects` only.
The four supplied user images and assignment are local review inputs, not committed.

Before editing, the approved 1536×1024 concept, all four user captures and actual
Qt baseline frames at 1536×1024/3440×1440 were inspected. The initial regression
failed with **12 columns, 54px Versions→Scans panel drift at 3440×1440, and no
independent gallery viewport**. It now requires six or fewer columns, <=1px drift
and a dedicated clipped browser viewport. New/Open remain blocked by the proven
backend replay/preview isolation defect. No runtime/client/model/Home/Acquisition/
Calibration behavior or global minimum window size changes are included.

### Region audit: before / after / remaining difference

1. **Header:** small subtitle and crowded controls / slightly larger subtitle,
   unchanged honest disabled New/Open and clear source warning / unsupported
   project operations remain disabled; reference's ready/scanner/user claims absent.
2. **Navigator:** four gray bullets, no tag counts / six actual colored circles,
   right-aligned counts from the twelve mock studies, source-scoped filters and
   internal overflow / no live catalog, tags, persistent favorites or cloud counts.
   Housing blue `#3996ed`, Prototype purple `#aa6de3`, Customer A orange `#ef963e`,
   R&D green `#24c88d`, Quality Control yellow `#edc94c`, Tutorial gray `#94a1aa`.
   The engine/cover mock studies illustrate Customer A/Tutorial respectively.
3. **Gallery:** 12 narrow cards on ultrawide, 110px previews, tiny concatenated
   metadata / width-derived grid capped at six, four at reference size, 128–230px
   previews, 13px bold names, 11px dates and shared semantic tag/type pills /
   original approved transparent technical studies retained; no fictional sync glyphs.
   Wrapped chips have a reserved second line and never collide with the date.
4. **Inspector:** fixed 316px, raw space-aligned multiline tree / adaptive
   320–420px, larger preview/name, illustrative star, label/value metadata and
   seven explicit icon/label/count rows, compact action grouping and its own
   overflow / live preview, scanner, description and unsupported metadata remain
   honestly unavailable. Runtime-wide jobs/events stay explicitly unassociated.
5. **Contents:** height tied to rows, tab jumps, unallocated lower strip / fixed
   placement with a bounded 31% body-height share (188–300px), readable columns,
   four mock versions visible at reference size, weighted empty state and a clipped
   independent 128-row table / nine tabs include authoritative Artifacts; no fake
   revision/mutation controls or rows to fill unused table space.
6. **Scrolling:** whole page moved / gallery/list, table, overflowing inspector
   and navigator have separate native vertical Flickables; desktop chrome,
   filters, rails and tab panel stay docked / compact Filters/Details stack inside
   the browser's own viewport and may scroll there; table remains docked.
7. **Space:** detached lower panel and narrow inspector / panel bottom matches
   workspace bottom within 1px, retaining only the existing 12px shell/footer
   inset; inspector right gap <=1px / tall windows retain intentional browser
   breathing room, while table height is capped at 300px rather than stretched.

### Ownership, focus, native input and bounded work

`ProjectsViewport` clips vertical content and uses attached interactive scrollbars.
There is no outer page ScrollView, horizontal scroller or global wheel handler.
Events at desktop bounds have no scrollable page ancestor or sibling fallback.
Focus reveal walks the focused item's enclosing viewport(s); compact stacked
panes additionally reveal within the browser. Layout completion schedules the
same bounded reveal method. The dock frame owns independent bounds around its
RowLayout: Qt 6.9 otherwise retained an ultrawide rail allocation after resizing
until a tab changed content hints. A strict sanitizer regression reproduced the
420→360.72px shift; isolating the frame allows its own layout polish without
private Qt APIs or delayed geometry mutations. Keyboard movement cancels a competing flick only
when needed to reveal a row. No harmless snapshot resets an offset or selection.
Grid/list keeps the numeric offset where possible, clamps it to new bounds, and
anchors keyboard focus if needed. Tab/source changes reset their own table to
its first row; the gallery retains its offset. Search, canonical identity and
source authority survive resize. Twelve gallery, 128 table and six tag slots
remain persistent outside Layout caches, including source/mode changes.

Qt's [Flickable documentation](https://doc.qt.io/qt-6/qml-qtquick-flickable.html)
and the exact
[Qt 6.4.2 wheel/release implementation](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/quick/items/qquickflickable.cpp?h=v6.4.2)
were investigated. ScrollEnd ends the pixel drag but does not start a release
flick; platform ScrollMomentum is handled separately. The native drag release
uses a 100ms recent-motion cutoff. Changing flickDeceleration cannot create a
missing OS momentum phase. ScrollView's mouse/touch defaults and filters were
also checked against
[Qt 6.4.2 source](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/quicktemplates2/qquickscrollview.cpp?h=v6.4.2).

Actual unlocked Linux Wayland input from the laptop's PIXA touchpad was captured
with Qt 6.9.2. The baseline has 1762 raw window wheel observations: Begin 140,
Update 1482, End 140, **Momentum 0**; duplicate bubbled boundary observations are
retained rather than presented as distinct gestures. Updates are predominantly
7–8ms apart and have pixel deltas and system natural-scroll inversion. This
reproduces the platform's absence of native momentum, rather than a mouse-wheel
or global zoom problem. Revised physical traces record pointer containment and
20ms samples for 300ms after release. The first attempt occurred outside the
browser and is not used to certify coasting. The central-list trace confirms
native post-release motion with decreasing velocity; see the retained trace
summary and raw wheel-only logs in `evidence/projects-v2/`.

The presentation-only `ProjectsScrollInput` handles only its own Flickable.
Phased Update/Momentum pixel deltas move that viewport immediately by the exact
native delta, clamped to its bounds. Qt's wheel-as-drag path otherwise discards
the first small movement while acquiring a drag. Direction is already adjusted
by the platform and is not inverted again. OS Momentum uses the exact same
pixel path and bypasses supplemental release. Mouse wheel, unphased input,
touch dragging and phase boundaries retain Qt's native handler. A scrollable
viewport accepts precise pixels even at its boundary; a compact stacked panel
without overflow permits propagation to its enclosing browser.

For recent phased motion without OS momentum, the adapter requests exactly one
public Qt `flick(0, velocity)` after ScrollEnd. It averages at most four recent
pixel/time samples and resets on reversal, pause and context changes. Qt owns
the release trajectory, deceleration and bounds. The first physical revision
coasted but compressed fast gestures against Qt's default 2500px/s ceiling.
A failing gentle/fast regression measured velocities **500/2500**; the measured
6000px/s ceiling now yields **500/4000** and distinct native travel. Physical
speed-sensitive traces span approximately 80–4526px/s initial release velocity.
The user confirmed speed dependence but reported a startup delay. Exact native
pixels eliminate that acquisition delay: a first 3px Update moves the viewport
3px immediately, while the same native flick owns release. The final physical
test was explicitly accepted by the user: “Yes, it starts promptly and coasts
naturally.” This acceptance concerns scrolling on the tested laptop, rather
than a claim that automated geometry tests establish visual design acceptance.
There are no product timers, per-wheel animations, global hooks, private Qt APIs,
new backend methods, image requests or unbounded buffers. Four small input
adapters are scoped to Projects viewports. Diagnostic timers sample only test
windows and never drive their position. Other compositors/devices and physical
Qt 6.4/ARM touchpads are not certified by this laptop session; phased routing,
release/bounds and momentum bypass are tested with Qt 6.4 on both CI architectures.

### Validation and exact-revision gate

Added `studio-projects-fidelity`, `studio-projects-scroll`, and scroll runs at DPR
1.5/2. Studio ON now registers **64 tests**, OFF/headless sanitizers **42**.
Scroll regressions cover every result count `{0,1,2,3,4,5,6,12}`, nine viewports,
all sources, six semantic colors/counts and absence of live tag/count claims.
They cycle all nine tabs 30 times per source, then repeat with changed project,
sort and facet. Scene bounds assert stationary navigator, inspector, gallery
header, tab strip and bottom panel. Grid/list top/end, table-only 128-row scrolling,
keyboard arrows through every artifact, inspector overflow, boundary routing,
natural-direction pixel input, standard wheel and explicit platform momentum
are exercised. A no-Momentum release must coast once; paused fingers and stepped
wheel must not add a release. Injected tests prove routing/native mechanics;
the separate physical trace supplies native laptop evidence.

All final local suites passed: Qt 6.4 Studio **64/64**, Studio OFF **42/42**,
headless ASan/UBSan/LSan **42/42**, Qt 6.9 Studio ASan/UBSan/LSan **64/64**,
and five UI suites repeated three times (**15/15 invocations**). Native default-
renderer normal/maximized/restored checks passed for all three sources. Home
remains pixel-identical to the reviewed baseline. Logs, screenshots and physical
input observations are retained in the [V2 evidence index](evidence/projects-v2/README.md).
The existing five-job architecture matrix is retained; ON jobs also repeat the
new scroll suite three times. CI generates exact `ui-m2a/revision.json` receipts
and uploads all screenshots and full/repeated test logs. Final SHA/run are supplied
in the review handoff after all five jobs succeed; no branch is merged.


Branch: `feature/ui-m2a-projects`, created directly from accepted Home commit
`6b07fd34ab193ee32dc52c6ffba76a22a59497ab`. No merge or other feature-branch change.
The [action/authority matrix](projects.md) was published before implementation.
The approved [Projects reference](reference/07-projects.webp) was inspected at its
full 1536×1024 size, alongside Home and the rendered placeholder before coding.

## Local checks

Ubuntu 24.04 container `mantis-ui-m0-qt64`, GCC 13.3, Qt 6.4.2, Debug, x86_64,
software offscreen renderer. Existing OpenCV development dependencies and the
public-client/backend suites remain in use. Studio OFF has no Qt dependency.
The configurations/builds and complete suites were executed as follows:

```bash
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=ON -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64 --parallel 3
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 --output-on-failure
# Same configure/build/test with build/qt64-headless and Studio=OFF.
# build/qt64-headless-sanitizers additionally uses -DMANTIS_SANITIZE=ON.
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64-headless --output-on-failure
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64-headless-sanitizers --output-on-failure
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 \
  -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-ui-m0-qml)$' \
  --repeat until-fail:3 --output-on-failure
```

Extra installed Qt 6.9.2 Studio ON ASan/UBSan/LSan was built and tested locally:

```bash
cmake --build build/qt69-sanitizers --parallel 3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ctest --test-dir build/qt69-sanitizers --output-on-failure
```

The OpenCL override is the previously documented host dependency-loader workaround,
not a sanitizer suppression. No leak/failure suppression was added. Qt 6.4's known
software-renderer dependency LSan limitation is unchanged; the required sanitizer
matrix job remains headless, and Qt 6.9 provides extra Studio ON evidence.

| Final local suite | Result |
| --- | --- |
| Qt 6.4 Studio ON | 60/60 passed, 240.85s |
| Studio OFF | 42/42 passed, 101.81s |
| Headless ASan/UBSan/LSan | 42/42 passed, 234.28s |
| Qt 6.9 Studio ON ASan/UBSan/LSan | 60/60 passed, 418.66s |
| Qt 6.4 Home/Projects/resize/M0, each ×3 | 12/12 executions passed, 154.13s |
| Qt 6.9 Projects focus/layout stress ×10 | 10/10 passed, 137.95s |
| Native Wayland mock/live/hybrid maximize/restore | Final default-renderer pass in all three sources, normal/maximized/restored; search focus retained |

The suite has 60 Studio ON / 42 OFF tests (accepted baseline: 53 / 41). No test was
removed or disabled. Existing bridge fixture assertions now require clearing old
selection when that fixture confirms a different project path on every snapshot;
the prior retention assertions conflicted with the required project-context safety
contract. All its operation, transport, artifact and authentication checks remain.
Other existing QML hosts only register/link the new model for the shared shell.
The M0 route test additionally reports each stage and collects engine-managed
garbage after every route; its original assertions remain enabled. Both ON CI
jobs now repeat M0 as well as Home/Projects/resize, twelve executions total.

## New contracts and safety evidence

- `studio-projects-qml`: typed model presence/freshness, malformed lists/scalars,
  unknown types, positive/full uint64 chunk values, raw vs bounded display identity,
  Unicode/control/bidi/literal markup, duplicate IDs, deterministic search/sort/
  filter/reset, canonical selection and source separation. Real mouse, Space,
  Tab/Backtab/arrows, text entry, sort/type/state/source dropdowns, all nine tabs,
  compact Filters/Details and Escape are exercised. Unsupported actions are checked
  through mouse, Return, Space and direct accessible press.
- `studio-projects-wire`: actual public C++ Client and asynchronous StudioBridge
  against a deterministic authenticated wire peer. Confirmed metadata, explicit
  rejection, duplicate-request busy lease, transport loss, stale retention,
  authentication denial, changed identity and restoration are checked. Passive
  GUI actions issue **zero** project-open/capture/replay/data/preview requests.
  Four test-only peer controls and one rejected pipeline operation are explicit
  audit actions. One separate public-wire `project_open` audit loses the write
  response after execution, confirms the new snapshot, preserves the operation
  root cause and clears selection, without retry. This is not an enabled GUI action.
- `project-public-capabilities`: real mantisd, temporary projects and two public
  clients; a second daemon holds a competing store lock. Checks missing/locked
  projects, authentication denial, invalid file ancestor/NUL paths, active capture
  and busy replay refusal, Unicode/literal shell-character paths, create/open
  semantics and shared current identity. `create=true` opens existing data too.
  A completed A replay remains readable after both clients confirm B and writes its
  preview under B. [Retained audit](evidence/projects/public-capability-audit.log).
  **The test proves the blocker, not safe switching.** New/Open are unavailable in
  every source; the bridge exposes no GUI project-open invokable. Daemon internals,
  protocol/client ABI and storage are unchanged.
- Confirmed external identity changes clear frontend selected/newest cloud,
  capture/replay, PointCloud render and both preview surfaces. Late old-generation
  preview delivery and old explicit artifact selection are rejected. Disconnect
  retains last-known data with stale provenance until an authoritative snapshot.
  This frontend guard cannot fix another client's runtime replay queues.

## Rendered visual and responsive evidence

Every size below is captured at DPR 1 in mock, confirmed live, hybrid,
never-confirmed, confirmed missing identity, confirmed empty, stale, recovered,
and hybrid-stale states: **81 size/state captures**. Mock/live/hybrid also have
each size at DPR 1.5 and 2: **54 additional captures**, plus interaction, wire,
single-result, Home, maximize and native evidence. Pixel dimensions are asserted
against logical size × actual Qt DPR. These are actual `QQuickWindow::grabWindow`
images, not HTML approximations or screenshots sliced into product assets.

| Logical viewport | Gallery columns (12 samples) | Grid-card width (logical px) |
| --- | ---: | ---: |
| 1080×720 | 4 | 217 |
| 1280×720 | 5 | 206.4 |
| 1366×768 | 3 | 204.67 |
| 1440×900 | 3 | 229.33 |
| 1536×1024 | 4 | 193 |
| 1920×1080 | 6 | 188.67 |
| 2560×1440 | 9 | 192.89 |
| 3440×1440 | 12 | 215 |
| 3840×2160 | 12 | 248.33 |

The navigator/inspector collapse below 1120 workspace logical pixels (about a
1320px window); the header wraps below 1160 workspace pixels. Panels remain
discoverable via Filters/Details and stack in the vertical scroll. Multi-pane
navigator and inspector are 188px and 316px; measured inspector right-edge gap is
0px. The main workspace remains fluid with no overall cap, centering or zoom.
Grid cards are 180px tall and capped at 300px even for one filtered result; live's
current-project card is capped at 420px. List thumbnails retain fixed proportions.
No page horizontal overflow, changed 1080×720 minimum, or clipped toolbar was found.

`studio-projects-resize` executes 252 width transitions (three sources, three passes,
both directions, fourteen widths spanning the real breakpoints and ultrawide),
then maximize/restore and compact list/contents checks. QPointers prove that 12
gallery and 128 artifact slots survive resize/mode transitions; search focus and
selection remain stable. The spanning navigator does not push the gallery away
from its filter header. Hybrid's authoritative contents precede sample cards.
Keyboard focus scrolls into view, including the twelfth list item. No Qt/QML
warnings are accepted by the Projects test host.

The final application executable also captured the default desktop directly:

```bash
docker exec mantis-ui-m0-qt64 env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  /work/build/qt64/bin/mantis-studio --ui-mode mock --workspace projects \
  --window-size 1536x1024 --screenshot /work/build/ui-m2a/projects-default-final-1536.png \
  --quit-after 4500
```

Exit status was zero; the full image was inspected. The retained default-view
capture and selected matrix images were refreshed after final tab and combo-label polish.
The complete suites and QML matrix already used the final implementation.

Full Qt captures were inspected against the approved reference. The desktop
retains its header, internal navigator, four-column technical gallery, selected
mint outline, right-edge inspector and lower contents tabs. Compact layouts wrap
controls and preserve scroll access; ultrawide layouts add columns instead of
stretching a few cards. Transparent subject foregrounds share Home's continuous
QML preview background/padding. The lower pane can sit below the fold on shorter
windows, preserving vertical scrolling. Home's actual full rendered capture was
also inspected; its design/assets and standalone-artifacts removal are intact. The
shared icon compatibility correction produces an exactly identical full Home
image before/after at 1536×1024 in Qt 6.4 mock/offscreen/software mode
([pixel comparison](evidence/projects/home-icon-pixel-comparison.log)).

Deliberate reference deviations: original procedural studies replace vendor casts,
shoe/statue imagery; sample dates/authors/size/version metadata say illustrative.
Live art and unsupported fields show Unavailable; no scanner/size/time/ownership
or project-version claims are invented. Artifacts is an additional truthful tab.
New/Open are visibly blocked, and other missing capabilities are inert/explained.

Native Qt 6.9 Wayland was available on the user's GNOME desktop. Actual normal
1536×1024 and maximized 2048×1210 logical windows at DPR 1.25 were inspected in all
three sources, with compositor exposure, focus and restore assertions. After the
session unlocked, the final application code was rebuilt and all nine native
normal/maximized/restored images were inspected;
[final default-renderer log](evidence/projects/native-final-rhi.log) confirms every
assertion. The host font/rendering stack differs from the Qt 6.4 reference captures. This is
desktop presentation evidence, not hardware acquisition, native screen-reader,
metrology, Windows/macOS or physical 4K-monitor verification.

## Bounded work and assets

A 12,000-entry synthetic snapshot inspects at most 512 descriptors and renders at
most 128. Counts distinguish snapshot, matching inspected sample and shown rows;
limited search is disclosed. Queries are literal and bounded to 256 characters,
never regex. One hundred normalized identical presentations emit zero model
changes. Those 100 presentations took 689ms in Qt 6.4 Debug and 3,132ms in
Qt 6.9 with sanitizers; retained full CTest logs record the measurements. These
are local Debug/instrumented observations, not universal performance guarantees.
There is no model-owned timer/client/watcher, no data-plane thumbnail/point loading,
and no per-notification image encoding. The existing bridge remains the transport.

Four Home PNG subjects are reused unchanged. Eight new original RGBA foregrounds
total **774,616 PNG bytes**, **2,944,624 native decoded RGBA bytes**. Each passes
nontrivial alpha/antialiasing and eight-pixel transparent-border checks. Decode
requests reuse the 520×355 Home bound; renderer/cache copies remain possible.

| New PNG | Native pixels | Bytes |
| --- | ---: | ---: |
| bracket-qc | 368×321 | 109,625 |
| cover-qc | 410×221 | 86,199 |
| enclosure | 406×238 | 105,844 |
| engine | 390×310 | 122,097 |
| flange | 310×248 | 81,047 |
| gear | 274×194 | 72,550 |
| manifold | 394×271 | 120,813 |
| pipe | 280×261 | 76,441 |

Blender 4.3.2 CPU Cycles, 256 samples, seed 0 and offline lossless alpha packaging
are documented in [asset provenance](../../ui/projects/assets/README.md). Blender
and Pillow are not application/build/CI dependencies. No external/reference pixels,
network art or runtime image generation. Initial clipped bracket framing was
rejected by validation and corrected before packaging.

## Failure evidence and limitations

Initial exact-revision CI failed the native ARM64 M0 route test while the other
four jobs passed. The evidence refresh reproduced it; that run also retained an
acceptance ready-marker timeout. Neither failure was discarded or retried into
an alleged pass. Diagnostic runs
[af7cb37](https://github.com/martinkoenig/mantis-studio/actions/runs/37924972256) and
[9b9a746](https://github.com/martinkoenig/mantis-studio/actions/runs/37926818410)
retained the crash. GDB identifies `QV4::MemoryManager::collectFromJSStack`; Qt's
exported QML stack helper identifies `StudioIcon.qml`, `expression for onPaint`,
line 15. [Full QML/backtrace evidence](evidence/projects/ci-arm-qml-stack.log),
[first failure](evidence/projects/ci-first-arm-job.log) and
[second failure](evidence/projects/ci-second-arm-job.log) remain available.

Qt's upstream [QTBUG-111935 fix](https://codereview.qt-project.org/c/qt/qtdeclarative/+/466808)
adds accumulator preservation around captured call-context creation in the JIT;
the Qt 6.4.2 source lacks that fix. The shared stroke icon now uses persistent
helpers with explicit canvas arguments, removing the handler's captured closures
while preserving every coordinate, drawing operation and style. Projects queues
bound focus methods directly instead of repeatedly allocating closures. No global
JIT/interpreter/GC override, test skip or backend change is used. The failed-job
diagnostic detector uses portable `grep`; the first diagnostic run lacked `rg`.

Final label checks also caught Qt Basic's indicator padding being added to the
custom combo text padding. Explicit control padding fixes source/sort truncation;
all size/source checks assert those labels are readable. Native checks capture a
rendered frame before geometry checks and wait for actual compositor state rather
than a fixed 200ms delay. A later GNOME rerun was blocked by the locked session
([blocked run](evidence/projects/native-locked-final.log)). A later
[session-state query](evidence/projects/native-session-final.txt) returned false;
the default native Qt renderer then passed all three sources on the final code.
A separate native Wayland/software run failed its exact screenshot-DPR geometry
assertion after maximize ([failure](evidence/projects/native-software-dpr-failure.log));
it is retained as a renderer-specific limitation, with no assertion weakened.
The final offscreen/software DPR matrices and native default-renderer pass remain
distinct evidence.

Earlier Qt 6.9 runs exposed a fixed-30ms focus-scroll test observing old content
height between frames. [Failure evidence](evidence/projects/README.md) is retained.
Queued geometry-aware reveal plus an actual visible-rectangle deadline replaced
that assumption; ten consecutive sanitizer runs passed before final toolbar-label
polish. The native evidence includes an earlier transient desktop-focus failure;
final runs still assert focus rather than masking the failure. Initial stale-binary
test execution during a failed host build was discarded and rebuilt sequentially;
it is not counted as successful validation. Final full logs supersede early runs.

| Priority | Remaining issue / scope |
| --- | --- |
| P1 | Backend replay/preview project isolation blocks New/Open; separate runtime review required. Path security/exclusive-create/unknown-outcome contracts also need hardening. |
| P2 | Accepted unreadable PointCloud can retry `client.data()` about every 500ms; separate required reliability fix before UI-M3, unchanged here. |
| P3 | Native Wayland/software screenshot DPR assertion fails after maximize; default renderer passes. No public catalog/versions/tags/share/package/delete APIs, live thumbnail or cross-platform/native screen-reader/hardware evidence; truthful unavailable states and mock metadata remain. |

## Exact-commit GitHub Actions gate

The required [architecture workflow](../../.github/workflows/build.yml) retains
five jobs: x86_64 Studio ON/OFF, ARM64 Studio ON/OFF and headless sanitizers. Both
ON jobs run all 60 tests, repeat Home/Projects QML/resize/M0 three times, and upload
Home/M0 plus Projects screenshots/logs in `ui-m0-ubuntu-24.04` and
`ui-m0-ubuntu-24.04-arm`. Complete CTest output is copied before repeat runs.

Implementation commit `206a705ded5dff73d6a5bb96e264ea7368b93e71` is linked to
[its architecture run](https://github.com/martinkoenig/mantis-studio/actions/runs/37923406905).
Application compatibility correction `6408638c9ab91c7c7adb1e6eba1affd434254eae`
passed all five jobs in [its architecture run](https://github.com/martinkoenig/mantis-studio/actions/runs/37928167428);
ARM64 passed all 60 tests plus twelve repeated transitions. This resolves the
retained M0 crash without suppressing JIT or garbage collection.
The final documentation/evidence commit is verified independently on its own SHA;
previous successful jobs do not satisfy that final gate.

The final commit's **literal exact SHA and immutable Actions run URL** are generated
by CI in each artifact's `ui-m2a/revision.json` (from `GITHUB_SHA`/`GITHUB_RUN_ID`).
This avoids an impossible self-hash embedded in a committed validation document.
The final review handoff supplies the same SHA, exact run/job/artifact links and
all five verified statuses. Resolve the run via
[Projects branch runs](https://github.com/martinkoenig/mantis-studio/actions/workflows/build.yml?query=branch%3Afeature%2Fui-m2a-projects),
compare receipts with final local/remote HEAD, and inspect both architectures'
retained full/repeat logs and generated images. READY FOR REVIEW is withheld until
all five jobs succeed for that exact commit. No branch is merged.

The final native check used the default renderer (no `QT_QUICK_BACKEND` override):

```bash
cmake --build build/debug --parallel 3
env QT_QPA_PLATFORM=wayland OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
build/debug/bin/mantis-studio-projects-tests build/ui-m2a/native-final-rhi native
```
