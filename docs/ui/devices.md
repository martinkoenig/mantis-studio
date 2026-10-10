# Devices workspace — UI-M2b

Devices is a read-only capability browser. The daemon/plugin owns discovery,
capture, projected light and physical state. The approved
[Devices concept](reference/08-devices.webp) guides composition, not command
permissions or telemetry. Existing Home, Projects, acquisition and the seven
calibration stages retain their implementations and lifetimes.

## Verified data and action authority

| Presentation / intent | Existing public evidence | Authority / absence |
| --- | --- | --- |
| Canonical identity, display name, hierarchy | `Response.devices`: `Device.id`, `name`, `parent`, `children` | Read-only exact identity; bounded literal display |
| Capabilities | `Device.capabilities` | Advertised namespaced strings; no current settings/ranges/permission inferred |
| Metadata | `Device.metadata` | Untrusted literal keys/values; never parsed as settings, executable code or telemetry |
| Plugin correspondence | Exact `Device.plugin_id` = unique `Plugin.id`; `state`, `diagnostic`, `version`, `kind`, `execution` | Plugin provenance, never physical device status or firmware |
| Freshness | Existing `StudioBridge.snapshotReady`, `connected`, `changed` | A descriptor is advertised, not physically connected, powered, ready or safe |
| Errors | `StudioBridge.errorDetails`: phase, kind, code, component, message | Preserve structured root cause and runtime scope |
| Events | `Response.events`: sequence, kind, component, message | Runtime-wide; no string-based device attribution |
| Open Calibration | Shared `CalibrationController::isCalibrationCandidate`, used by its existing `devices()` | Current confirmed, unique logical FrameSet parent with image-stream child; also requires complete trusted bounded graph |
| Existing captures / offline | Existing shell `openCalibration("")` | Preserved route, including disconnected operation; mock renders disabled existing workflow |
| Open Scan / Acquisition | Existing shell `scan` route | Navigation only in live/hybrid; no stream or capture starts |
| Refresh | Existing `StudioBridge.refresh()` | Live/hybrid only, existing busy lease and runtime-enabled guard; no extra polling or physical rediscovery |
| Add / Identify / firmware / device presets | No reviewed public service commands | Disabled, including accessibility presses, with explicit reasons |
| Camera resolution, format, exposure, gain | No `Device` values/ranges or reviewed GUI configuration API | Not reported; disabled |
| Laser power/pattern, trigger/strobe | Low-level capability names do not grant frontend command authority | Not reported; disabled; no local pretend settings |
| Serial, transport, temperature, measured FPS, interlocks, readiness | Absent from `Device` | Unknown / not reported, never fabricated values |
| Active calibration binding | Existing controller inspection occurs in Calibration; no Devices-specific current inspection generation | Always “Not inspected / Open Calibration to inspect”; never “Uncalibrated” |

Opening calibration emits the exact canonical real ID through the single existing
shell route. It resets that workflow to Device/preselection; it does not activate,
clear, solve or capture. The intent handler rechecks source and current bridge
confirmation, including direct/accessible invocation. Child cameras, emitter-only
parents, childless FrameSet descriptors, stale/unconfirmed evidence and all demo
identities cannot obtain a device-bound intent. Daemon validation remains final.

## Presentation seam and lifetime

`DevicesModel` is a GUI-thread QObject connected to the existing bridge's complete
snapshot signal and state notification. It owns no Client, network connection,
timer, worker, filesystem enumeration, persistence or preview pipeline. It stores
normalized QVariant DTOs, not borrowed protobuf pointers. `QPointer`, scoped
connections and teardown disconnection guard bridge lifetime.

`nodes` has a separate `graphChanged` notification from `data`/selection/freshness.
Selection and inspector tab changes therefore do not reset list delegates.
`data` describes source, confirmed/hasSnapshot/freshness, snapshot count, inspected
count, limits/anomalies, selected identity/detail, structured issues and scoped
events. Node DTOs include exact `id`, separate bounded `displayId`, `name`,
parent/declared children, depth/group, capabilities, metadata counts/omission,
plugin provenance, selectability and shared eligibility. QML receives no hardware
mutator. Four inspector tabs switch locally in the same persistent panel.

On attachment the model waits for the next existing snapshot signal; it does not
request a replay/refresh or treat the bridge's root-only list as complete evidence.
Production constructs it before the first asynchronous snapshot. Detachment clears
evidence. Before first confirmation, count is unknown. A confirmed empty graph
says “No devices advertised.” Failed confirmation retains selection/details as
last-known stale and disables bound intents. Confirmed recovery reconciles exact
identity. Removal/ambiguity clears selection and never chooses another device.
A confirmed project identity change clears selection, including long/hostile
project identities (hashed literally, never probed as paths).

## Graph and resource bounds

| Boundary | Finite budget / behavior |
| --- | --- |
| Device descriptors inspected/renderable | First 512; snapshot total remains explicit; excess disables bound calibration |
| Hierarchy | Iterative traversal, depth 8; cycles/deeper nodes shown under Graph anomalies |
| Child references | 64 per descriptor; exact unique IDs, reported contradictions/orphans/self references |
| Canonical identity | At most 1024 UTF-8 bytes; empty, invalid UTF-8, control/format/bidi identities rejected, never sanitized into actionable aliases |
| Display strings | Name 128, identifier/plugin/capability 192 characters; literal plain text, control/format/invalid UTF-8 replaced; ellipsis and diagnostics disclose limits |
| Capabilities | First 32, deduplicated and sorted; unknown names preserved |
| Metadata | Up to 128 entries inspected, literal keys sorted; 32 displayed, keys 128/value 512 characters; more than 128 entries omitted with explicit explanation |
| Plugins | First 128; correspondence requires exactly one matching ID and complete inventory; otherwise unknown; status 64/diagnostic 512 characters |
| Runtime-wide events | Last 12, exact uint64 sequences as strings; counts/limits disclosed; message 512 characters |
| Structured errors | First 8, bounded text; phase/code/component retained |
| Graph diagnostics | Up to 16 distinct explanations |
| Rendering | Virtualized navigator; three persistent panes; finite selected text; accepted static image assets with 520px decode bound |

Duplicate descriptor IDs make every occurrence non-selectable. No first-wins
command alias. Parent references determine placement; both relationship directions
must agree for trusted calibration evidence. Descriptors with anomalous topology
remain discoverable. Any graph/text/capability/metadata truncation or sanitation
conservatively disables device-bound calibration for that snapshot. This stronger
presentation guard leaves the controller's existing eligibility semantics intact.

A length-prefixed bounded field digest skips protobuf-to-Qt graph conversion on
identical evidence. It adds no serialization/image encoding. Map iteration changes
may require normalization, but normalized equality still suppresses notifications.
Changed snapshots cost bounded `O(N·D + N·P + N²)` (N≤512, D≤8, P≤128): the N²
term is the existing shared candidate rule's search for image children. Indexed
adjacency traverses each retained edge once. Selection/state refresh is `O(N)`;
metadata conversion does not run on bridge state-only notifications. Published
control responses already have the Client API's 4 MiB bound. Tests include larger
injected DTOs specifically to prove presentation caps, not network capacity claims.

## Sources, composition and interaction

Mock has seven separately authored illustrative descriptors: scanner with two
imaging components, rotary table, unavailable linear-stage example, trigger box
and auxiliary camera. It advertises no runtime capabilities. Existing original
Home scanner/rotor/housing and Projects mechanical foregrounds are reused unchanged,
with provenance in their existing asset READMEs. Preview studies are explicitly
“DEMO ART · NO CAMERA PIXELS”; no projected lines or live telemetry are fabricated.
Live uses a generic descriptor icon and directs preview work to Scan/Acquisition.

Hybrid starts on Live. Its explicit Live / Demo switch retains independent
selections and never combines counts or grants real authority to the fixture. A
real descriptor can deliberately have the same ID as a sample; provenance, not an
ID prefix, guards the intent. Mock disconnects the model and disables refresh and
legacy runtime workflows; no daemon/token is required.

At workspace width ≥1180, navigator (246px), fluid center and inspector (340px)
span the usable width using persistent anchored panes; the inspector's right-edge
gap is zero. At smaller widths,
Overview / All Devices / Inspector buttons expose the same persistent panes.
Escape returns to Overview. At center width ≥1300 the overview occupies the left
lane and preview/capabilities the right lane, avoiding enormous ultrawide cards.
The shell's existing 1080×720 minimum is unchanged.

Navigator, overview and inspector scroll independently and only vertically. Native
Qt Flickable/ScrollBar behavior is used; no second gesture interceptor or animated
wheel adapter is added. Up/Down moves navigator focus; Space activates exact
selection. Tab/Backtab retain standard traversal; focused controls reveal in their
own viewport. Selected/hover/pressed/focus states, screen-reader labels, disabled
explanations and literal text selection/copy are provided. Hardware status is
never communicated by a readiness-colored dot.

## Backend gaps and deliberate visual deviations

Future editable configuration requires reviewed typed services with presence,
supported ranges, permissions and explicit outcomes. Device creation/discovery,
identify, presets and firmware likewise need real public contracts. Emitter/timing
control additionally requires the accepted daemon ownership, safe-state and
independent physical interlock contracts; a low-level interface is insufficient.
A Devices preview would require reviewed lease/data-plane ownership rather than a
second pipeline. These are future backend work, not UI-M2b implementations.

The reference's X1 product render, real-looking camera feed, health/temperature/FPS,
serial/USB/firmware and interactive laser settings are deliberately replaced by
original demo art and truthful absence. Compact disclosure, font/spacing balance
and richer category illustrations can join the approved global visual-polish pass.
Layout/focus/overflow defects are treated as functional failures, not polish.
See [validation and retained screenshots](validation-m2b.md).
