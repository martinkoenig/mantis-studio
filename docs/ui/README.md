# Mantis Studio UI foundation (UI-M0)

UI-M0 establishes the Qt 6 / Qt Quick desktop shell, ten routes, a small design
system and an explicit presentation data contract. Full workspace tools and the
Home dashboard are deferred. [Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md)
and the [master roadmap](../../ROADMAP.md) remain authoritative. This UI milestone
does not change v0.3/v0.4 implementation or hardware acceptance status.

## Visual intent

The [ten approved references](reference/README.md) guide the near-black teal
palette, left navigation, top runtime bar, restrained mint accent, bordered cards
and compact typography. Reference images contain illustrative product data and
UX, including unimplemented functionality. They are never rendered as UI assets
or treated as evidence of device readiness, calibration, accuracy, laser safety,
remote support or release versions. In particular, the canonical convention is
**mm, right-handed +X right / +Y forward / +Z up**, regardless of reference text.

M0 pages deliberately show structured future-workspace panels. The Home page is
a foundation, not the UI-M1 dashboard. There are no fabricated measurements,
project histories, active jobs or hardware controls in these panels.

## Launch and data sources

Ordinary launches remain **live**, initially showing the existing acquisition
workspace. Mock and hybrid default to Home. No mode starts a daemon, scan, laser,
calibration activation or remote command. Modes are selected at launch, not switched
by a hardware-facing UI control.

```bash
# Fully offline, without mantisd or MANTIS_TOKEN:
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build/debug/bin/mantis-studio --ui-mode=mock --workspace=home \
  --window-size=1536x1024 --quit-after 5000 --screenshot /tmp/mantis-ui-m0-home.png

# Runtime data plus a separately labelled illustrative device:
./build/debug/bin/mantis-studio --ui-mode=hybrid --workspace=devices

# Existing real workflow, with the usual runtime endpoint/token:
./build/debug/bin/mantis-studio --ui-mode=live --workspace=scan
```

Both `--option=value` and `--option value` are supported. `--workspace` accepts the
ten route IDs below, plus `acquisition` and `calibration`. `--window-size` accepts
1080x720 through 7680x4320; defaults to 1536x1024. Invalid arguments exit with code
2 and usage guidance before loading QML. `--quit-after` requires positive integer
milliseconds. A screenshot is taken after 3500 ms, or 100 ms before a shorter quit
timeout. Screenshot save failures exit with code 1.

`--acceptance-export PATH` retains the asynchronous real capture → pipeline → PLY
workflow and overrides the initial route to `acquisition`, including when
`--workspace=home` was supplied. It is rejected in mock mode. It is an explicit
acceptance action, not ordinary launch behavior.

| Mode | Runtime status | Device data | Action authority |
| --- | --- | --- | --- |
| `live` | Actual `StudioBridge.connected`; disconnected remains disconnected | Current runtime descriptors only; empty when disconnected | Existing real controls, gated by runtime availability |
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
calibration actions for advertised FrameSet-capable live devices. `calibration`
remains a supported internal route. The root `workspace`, context objects
`studio`/`calibration`, `calibrationWorkspace`, all seven `calibrationStage0…6`,
`pointCloudView`, `leftPreview` and `rightPreview` hooks are retained.

Acquisition is instantiated once, so render attachments and view state survive
navigation. It retains PointCloudView orbit/zoom, MeasurementView dual previews,
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
  (`source`, `synthetic`, `actionable`, explicit capability lists) and holds compact
  route descriptors. It owns no capture state, client transport or polling timer.
- `ui/state/MockFixtures.qml` owns the deterministic illustrative fixture. It is
  isolated from `StudioBridge`; it has no command methods.
- `FoundationWorkspace.qml` renders route descriptors and provider data through
  the shared components. New full workspaces should receive presentation models
  and emit intents; runtime commands belong in frontend controllers using the
  public client API.
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
| `home` | Runtime status and discovered device summary | Welcome, recent projects/activity · UI-M1 | Add project/job presentation providers; reuse cards and source badges |
| `scan` | All acquisition, dual preview, replay and viewport tools | Redesigned scan setup/review · UI-M3 | Wrap existing bridge/controller intents; replace legacy layout incrementally |
| `process` | Recipe execution and jobs in acquisition | Recipes, lineage and stages · UI-M4 | Bind supported pipeline/job descriptors to a workspace model |
| `inspect` | Geometry viewing in acquisition; no metrology tools | Selection/measurement evidence · UI-M5 | Feed versioned analysis artifacts, units and validity into visual components |
| `reverse` | No CAD fitting workflow | Fitting/sections/CAD handoff · UI-M5 | Bind capability-backed derived artifacts; no UI-owned geometry computation |
| `automate` | Existing jobs/cancellation in acquisition | Sequences and execution history · UI-M6 | Introduce public service-backed intent controller when supported |
| `projects` | Current project and immutable artifacts in acquisition | Browser and revisions · UI-M2 | Bind project/artifact provider without changing card contracts |
| `devices` | Runtime descriptors; existing seven-stage calibration | Device detail/configuration · UI-M2 | Map real advertised capabilities and evidence; keep controller authority |
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
behavior, mock action boundaries, calibration accessibility, preserved viewport
hooks and absence of QML warnings. It writes PNGs for all ten mock routes, live
and hybrid Home, and Home/acquisition/calibration at 1080x720, 1536x1024 and
1920x1080 to `build/debug/ui-m0/screenshots/`.

`studio-ui-m0-cli` checks invalid options, token-free startup independent of runtime endpoint variables, short-timeout
screenshots and twelve mock initial routes against a listening socket trap.
`studio-calibration-qml`, `studio-calibration-controller`, calibration daemon and
`acceptance` tests remain in place; acceptance additionally starts with `--workspace=home` at 1080x720 to prove the real viewport override. Tests need no physical scanner
or laser activation. Screenshots are review artifacts; there is no brittle pixel
comparison against illustrative concept imagery.

See [the M0 validation record](validation.md) for checks actually executed and
remaining limits. Linux ARM64 remains an architectural target; this local run
provides x86_64 evidence only.
