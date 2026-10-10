# Mantis Studio UI (UI-M0, Home, Projects and Devices)

UI-M0 establishes the Qt 6 / Qt Quick desktop shell, ten routes, a small design
system and an explicit presentation data contract. UI-M1 adds the dedicated
[Home dashboard](home.md); UI-M2a adds the dedicated [Projects workspace](projects.md). UI-M2b adds the read-only
[Devices workspace](devices.md). Other full workspace tools remain deferred. [Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md)
and the [master roadmap](../../ROADMAP.md) remain authoritative. This UI milestone
does not change v0.3/v0.4 implementation or hardware acceptance status.

UI-M3a adds [bounded PointCloud auto-load/retry](pointcloud-retry.md) in the existing
bridge, with no Scan UI redesign. [Validation and reproduction](validation-m3a.md)
cover request counts, source authority, manual recovery and complete local suites.

## Visual intent

The [ten approved references](reference/README.md) guide the near-black teal
palette, left navigation, top runtime bar, restrained mint accent, bordered cards
and compact typography. Reference images contain illustrative product data and
UX, including unimplemented functionality. They are never rendered as UI assets
or treated as evidence of device readiness, calibration, accuracy, laser safety,
remote support or release versions. In particular, the canonical convention is
**mm, right-handed +X right / +Y forward / +Z up**, regardless of reference text.

The remaining M0 foundation routes retain their existing workflows and planned panels.
Home presents bounded current project, logical device, job, artifact and
event summaries. Readiness, project history and utilization remain explicitly
unknown/unavailable. Mock has a clearly labelled deterministic showcase; hybrid
places illustrative projects below the live dashboard, never in its counts.
The main column now follows the approved composition: a substantive scanner/casting
hero, four substantial read-only mechanical studies in mock, four distinct Quick
Actions and one compact Recent Activity table. The standalone Home artifacts card
and its CTA are removed in every mode. Live artifacts remain grouped with events
inside Activity, with explicit provenance and unavailable dates/sizes; acquisition
artifact workflows are unchanged. Mock job details and unsupported project imports
are disabled with visible/accessibility explanations. Learn/Tips open a working
local read-only guide; Browse examples focuses the mock/hybrid showcase.
Home uses a fluid primary region and a right-edge information rail. Wide primary
regions reorganize into Hero/Activity and Projects/Actions lanes, keeping cards
proportionate. Five original transparent object PNGs sit on card-owned backgrounds;
the accepted scenic Hero JPEG stays unchanged. See
[corrective layout / image / DPI validation](evidence/corrective/README.md), and
see [asset provenance and reproduction](../../ui/home/assets/README.md) and the
[reference/before/after inventory](evidence/m1-fidelity-delta.md).

Projects follows the approved gallery/filter/inspector/contents hierarchy. It
uses an adaptive gallery and right-edge 316px inspector, with discoverable stacked
Filters/Details controls below 1120 workspace pixels. Samples are local illustrations,
never runtime project IDs. Live contains one shared daemon project, actual artifact
metadata and explicit unknown fields. New/Open use a visible safety gate: the
published operation retains old replay/preview context after a project switch.
There is no GUI filesystem catalog or mutable project operation in this milestone.
See [UI-M2a validation and review evidence](validation-m2a.md).

Devices presents the complete bounded descriptor graph, exact plugin provenance,
explicit stale/unknown states and capability-authorized calibration navigation.
Its three desktop panes adapt to discoverable compact views; mock/hybrid demo
counts and identities stay separate from live. Device configuration and projected
light controls remain disabled with explanations. See [UI-M2b validation](validation-m2b.md).

## Launch and data sources

Ordinary launches remain **live**, initially showing the existing acquisition
workspace. Mock and hybrid default to Home. No mode starts a daemon, scan, laser,
calibration activation or remote command. Modes are selected at launch, not switched
by a hardware-facing UI control.

```bash
# Fully offline, without mantisd or MANTIS_TOKEN:
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build/debug/bin/mantis-studio --ui-mode=mock --workspace=home \
  --window-size=1536x1024 --quit-after 5000 --screenshot /tmp/mantis-ui-m1-home.png

# Fully offline Projects gallery:
./build/debug/bin/mantis-studio --ui-mode=mock --workspace=projects

# Runtime Home plus a separately labelled illustrative showcase:
./build/debug/bin/mantis-studio --ui-mode=hybrid --workspace=home

# Existing real workflow, with the usual runtime endpoint/token:
./build/debug/bin/mantis-studio --ui-mode=live --workspace=scan
```

Both `--option=value` and `--option value` are supported. `--workspace` accepts the
ten route IDs below, plus `acquisition` and `calibration`. `--window-size` accepts
1080x720 through 7680x4320; defaults to 1536x1024. Invalid arguments exit with code
2 and usage guidance before loading QML. `--quit-after` requires positive integer
milliseconds. Combining it with `--screenshot` requires at least **100 ms**;
shorter combinations exit with code 2 before loading QML. A screenshot is requested
after 3500 ms, or immediately for a quit deadline of 3500 ms or less, and saved only after
`QQuickWindow::afterFrameEnd` confirms rendering (queued to the GUI thread).
Missing rendered frames, scene graph errors and PNG save failures exit with code 1.
The screenshot deadline is `--quit-after`, or 10 seconds when no quit time is set.
An ordinary rapid quit without a screenshot still accepts any positive timeout.

`--acceptance-export PATH` retains the asynchronous real capture → pipeline → PLY
workflow and overrides the initial route to `acquisition`, including when
`--workspace=home` was supplied. It is rejected in mock mode. It is an explicit
acceptance action, not ordinary launch behavior.

| Mode | Runtime status | Device data | Action authority |
| --- | --- | --- | --- |
| `live` | `StudioBridge.connected` confirms a successful authenticated snapshot; otherwise runtime state is unconfirmed | Snapshot descriptors; cached data explicitly last known after failed confirmation | Existing real controls, gated by runtime availability |
| `mock` | “Mock · runtime not used”; always disconnected | Deterministic, separately authored `Demo / Mock` fixture | Bridge polling and commands disabled; legacy acquisition/calibration controls disabled |
| `hybrid` | Actual runtime status, including disconnection | Live descriptors and a separate `Demo / Mock` example | Only real descriptors enter existing runtime controls; fixtures expose no actions |

A runtime connection or discovered descriptor does not establish hardware
readiness, calibration validity or precision. Fixture IDs never enter the bridge,
calibration device selector or client command paths. Capability lists in live
summaries come from the snapshot; the example has **no runtime capabilities**.
Hybrid never replaces missing live data with fake connected devices.

## Routing and existing workflows

The visible navigation order is Home, Scan, Process, Inspect, Reverse, Automate,
Projects, Devices, Plugins, Settings. Each changes the root `workspace`, active
selection and page title. Tab traverses controls, Up/Down traverses navigation,
and Space/Enter activate buttons. Hover, pressed, disabled and keyboard-focus
states are explicit. Accessible names are set on navigation and shared controls.

`scan` uses the preserved acquisition workspace in live/hybrid. In mock it shows
its M0 foundation, with an explicit action to inspect the disabled existing view.
`acquisition` remains a supported internal alias and initial live route.
Devices exposes the existing calibration workflow by an explicit action, plus
calibration actions for devices validated by `CalibrationController`: a composite
FrameSet-stream parent with image-stream children. A discovered parent or isolated
capability string grants no calibration permission. The separate offline entry
remains available for existing captures even without connected hardware. `calibration`
remains a supported internal route. The root `workspace`, context objects
`studio`/`calibration`, `calibrationWorkspace`, all seven `calibrationStage0…6`,
`pointCloudView`, `leftPreview` and `rightPreview` hooks are retained.

Acquisition is instantiated once, so render attachments and view state survive
navigation. On a confirmed change of raw runtime project identity, the bridge
clears old artifact/newest/replay selections and render/preview images; late preview
completion from the previous project is rejected. Unconfirmed last-known data
remains labelled stale. This frontend invalidation does not fix daemon replay isolation. It retains PointCloudView orbit/zoom, MeasurementView dual previews,
capture/stop/replay/verify, artifact selection, recipe execution, PLY export,
plugin recovery, jobs/cancellation and diagnostics. At small widths its original
three-column layout scrolls horizontally rather than hiding working controls.
Calibration logic and stage components are unchanged; the revision rail uses a constrained scroll width to keep text wrapped. Controller visibility is
true only for Devices/Calibration in live/hybrid; mock never starts its polling.

## Component and provider boundaries

- `ui/design/Theme.qml` is a local singleton for colors, type, spacing, radii and
  interaction sizes. The QML resource list includes its `qmldir`.
- `ui/components/` contains reusable panels, buttons, navigation, source/status
  treatment and device summaries. Icons are original Canvas strokes in a
  24-unit coordinate space; they scale without network assets, icon fonts,
  Qt SVG or new dependencies.
- `ui/state/AppUiState.qml` maps the existing bridge into presentation metadata
  (`source`, `synthetic`, discovery, unknown availability/readiness,
  action-specific permissions and explicit capability lists) and holds compact
  route descriptors. It owns no capture state, client transport or polling timer.
- `ui/state/MockFixtures.qml` owns the deterministic illustrative fixture. It is
  isolated from `StudioBridge`; it has no command methods.
- `HomeWorkspace.qml` composes the dedicated dashboard and separately labelled
  hybrid showcase. `HomeModel` owns bounded, read-only snapshot formatting and
  emits no notification for equal normalized results; mock detaches it from the
  bridge. `ui/home/` contains original static illustrations, the single activity table, local guide and reusable cards.
- `DevicesWorkspace.qml` composes the read-only DevicesModel, isolated local demo,
  grouped navigator, responsive overview and four-tab inspector. No additional
  client/transport/timer/preview is added.
- `FoundationWorkspace.qml` renders route descriptors and provider data through
  the shared components. New full workspaces should receive presentation models
  and emit intents; runtime commands belong in frontend controllers using the
  public client API.
- Snapshot failures preserve project/artifact data and last-known capture metadata.
  `StudioBridge.capturing` requires a current connection; acquisition labels cached
  state and diagnostics as last-known and disables runtime intents. Reconnection
  refreshes from the daemon, including active/idle transitions. Client disconnect
  sends no stop/cancel command and adds no polling path.
- Operation and artifact failures preserve the client's structured code, component
  and message in `StudioBridge.errorDetails`, tagged by phase. They do not establish
  loss of connectivity. A snapshot confirms usable runtime access after operations;
  artifact failures trigger one bounded snapshot confirmation because `Client.data`
  combines a control call with local mapped-file access. Failed confirmation retains
  last-known data and labels runtime state unconfirmed. Mutating operations are never
  retried. Operation/artifact diagnostics survive unrelated successful refreshes until
  another attempt in the same phase resolves or replaces them; snapshot diagnostics
  clear on successful reconnection. Untyped exceptions retain their message without
  a fabricated transport status.
- `AcquisitionWorkspace.qml` is the compatibility workspace until UI-M3.
  Calibration continues using its existing client/controller seam.

Use Layouts with fill/preferred/minimum sizes, wrapped/elided text, clipped lists
and scroll areas. Avoid fixed full-window coordinates. New action controls must
use real capabilities and source authority, not a screenshot's implied features.
Do not copy fixture values into live models. Add files to `MANTIS_STUDIO_QML_FILES`
so application and both QML test bundles load the same resources.

## Coverage and migration

These are UI follow-up boundaries, not backend release commitments. Every route
renders in M0; tools listed as planned require independent capability/evidence
work from the master roadmap.

| Route | Existing real functionality retained | Planned UI / milestone | Migration to live |
| --- | --- | --- | --- |
| `home` | UI-M1: truthful project/devices/jobs/artifacts/events, stale/error states and navigation | History, telemetry and tutorial browser remain unavailable | Read-only HomeModel consumes existing bridge notifications; no extra transport |
| `scan` | All acquisition, dual preview, replay and viewport tools | Redesigned scan setup/review · UI-M3 | Wrap existing bridge/controller intents; replace legacy layout incrementally |
| `process` | Recipe execution and jobs in acquisition | Recipes, lineage and stages · UI-M4 | Bind supported pipeline/job descriptors to a workspace model |
| `inspect` | Geometry viewing in acquisition; no metrology tools | Selection/measurement evidence · UI-M5 | Feed versioned analysis artifacts, units and validity into visual components |
| `reverse` | No CAD fitting workflow | Fitting/sections/CAD handoff · UI-M5 | Bind capability-backed derived artifacts; no UI-owned geometry computation |
| `automate` | Existing jobs/cancellation in acquisition | Sequences and execution history · UI-M6 | Introduce public service-backed intent controller when supported |
| `projects` | UI-M2a: current runtime project, bounded artifact search/filter/selection, separate twelve-study mock gallery and inspector | Catalog/CRUD/revisions unavailable; New/Open blocked by verified replay isolation defect | ProjectsModel observes the existing bridge; [safety gate and future public APIs](projects-backend-gap.md) |
| `devices` | UI-M2b: bounded read-only graph, inspector, plugin diagnostics and safe seven-stage calibration entry | Editable configuration/firmware/presets unavailable | DevicesModel observes existing snapshot; shared controller eligibility, explicit source/freshness guards |
| `plugins` | Runtime inventory/status/recovery in acquisition | Inventory and permissions · UI-M6 | Bind existing plugin descriptors/commands; marketplace is deferred |
| `settings` | Fixed canonical units, launch mode/viewport | Preferences/appearance/diagnostics · UI-M7 | Add local preferences provider; backend settings require supported services |

## Verification

Build prerequisites and normal commands are in [BUILDING.md](../../BUILDING.md).

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --output-on-failure
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless --output-on-failure
```

`studio-ui-m0-qml` tests real mouse and keyboard navigation, all route titles,
source/capability transitions with a presentation stub, disconnected live/hybrid
behavior, mock action boundaries, calibration eligibility/intent routing, preserved viewport
hooks, last-known disconnect/reconnect transitions, shared-button interaction
and contrast, rendered-frame screenshot deadlines and absence of QML warnings.
Immediate device-model changes followed by resize cover the proven Qt 6.4 layout
cache regression; malformed/empty property lists produce diagnostic failures. It writes PNGs for all ten mock routes, live
and hybrid Home, and Home/acquisition/calibration at 1080x720, 1536x1024 and
1920x1080 to `build/debug/ui-m0/screenshots/`.

`studio-ui-m0-cli` checks invalid options, token-free startup independent of runtime endpoint variables, short-timeout
screenshots and twelve mock initial routes against a listening socket trap.
`studio-calibration-qml`, `studio-calibration-controller`, calibration daemon and
`acceptance` tests remain in place; acceptance additionally starts with `--workspace=home` at 1080x720 to prove the real viewport override. Tests need no physical scanner
or laser activation. Screenshots are review artifacts; there is no brittle pixel
comparison against illustrative concept imagery.

`studio-bridge-errors` exercises the real public C++ client against a bounded,
deterministic wire fixture. It covers rejected operations (including status `io`),
transport loss, authentication rejection/missing credentials, missing/corrupt
mapped artifacts, failed confirmation, retained diagnostics, refreshed capture
state and recovery. The fixture asserts each mutating operation occurs exactly once.

See [the M0 validation record](validation.md) for checks actually executed and
remaining limits. Linux ARM64 remains an architectural target; this local run
provides x86_64 evidence only.


Correction evidence is in [validation.md](validation.md#additional-correction-record--2026-10-08-review-gate)
and [the retained Qt 6.4 backtrace](evidence/qt64-baseline-crash.txt).
`studio-home-qml` adds source/presence/error contracts, all Home CTAs, accessibility,
large/invalid inputs, alternate font metrics and 90 immediate model/mode/route/resize
transitions. `studio-home-wire` drives the production asynchronous bridge through
the public client and deterministic wire fault fixture. Home captures are in
`build/debug/ui-m1/{screenshots,wire}/`; run both with:

```bash
ctest --test-dir build/debug -R '^studio-home-(qml|wire)$' --output-on-failure
```

See [M1 validation](validation-m1.md) for executed checks and exact-SHA CI evidence.
Studio ON CI jobs upload generated screenshots and CTest diagnostics as
`ui-m0-ubuntu-24.04` and `ui-m0-ubuntu-24.04-arm` artifacts on the matching Actions run.
These captures are software presentation evidence, never physical scanner results.

`studio-devices-{model,qml,matrix,stress,wire,dpi-1.5,dpi-2}` adds graph,
authority, public-wire, accessibility and nine-viewport/DPR evidence using the
same QML resource list. See [UI-M2b validation](validation-m2b.md) for actual results.
