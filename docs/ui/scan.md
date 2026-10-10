# Scan workspace — UI-M3b foundation

`--workspace=scan` renders the dedicated Scan foundation in mock, live and hybrid.
Live startup still defaults to `acquisition`. **Open Classic Acquisition** is an
ordinary navigation action in live/hybrid; it never starts or stops capture.
Mock cannot invoke this route through the new page. `--acceptance-export` continues
to force the unchanged Classic Acquisition workflow.

## Ownership and authority

The shell eagerly instantiates one ScanWorkspace and one AcquisitionWorkspace.
Scan contains bounded camera placeholders, a static grid/axes viewport, setup and
notes, capture/processing status, and Sequence/Timeline/Markers panes. At narrow
widths, visible Viewport/Cameras/Setup/Sequence controls select persistent panes.
The center grows with workspace width; rails stay bounded and the setup rail
remains on the right edge. Settings and camera cards scroll independently. Notes
remain visible outside the settings scroller. Escape returns compact focus to
Viewport. The dock uses separate local tabs when three panels would be too narrow.

Only Classic Acquisition owns the original PointCloudView and two MeasurementViews.
Their identities and attachment targets persist across route changes. Its existing
visibility handler may idempotently assign the same preview QPointers; Scan never
calls attachView/attachPreview or introduces a view, frame copy or preview path.
The existing bridge's 500 ms snapshot and 66 ms preview timers remain unchanged.
The dormant Scan layout retains finite internal geometry before its first route
activation. This avoids zero-size Qt layout/font fallback work while preserving
all component identities and the existing application minimum size. Status cells
use explicit bounded positions instead of nested repeated ColumnLayouts, avoiding
the Qt 6.4 hidden-layout polish cycle.
Capture is daemon owned and survives navigation. M3a generation-checked cloud
retry/loading and project/preview invalidation remain unchanged.

| Presentation | Source | Authority |
| --- | --- | --- |
| Runtime connection/capture | Confirmed bridge snapshot | Read-only; capture active only while confirmed |
| Device summary | Advertised capture-capable top-level descriptors | Descriptor discovered, hardware readiness unknown |
| Artifacts/jobs/issues | Existing structured bridge properties | Passive bounded source-order rows; no replay/load/cancel |
| Live Scanner/Replay, display/setup/dock tabs | Local QML selection | Presentation only; no execution change |
| Classic Acquisition entry | Shell route | Navigation permission, separate from command permission |
| Mock or hybrid Demo / Mock | Dedicated ScanDemo fixture | Illustrative, permanently non-actionable |
| Tracking, RGB, markers, timing, rates, power/settings, notes | No supported source/API | Explicit unavailable/unknown; mutation controls disabled |

No metric is inferred from acquisitionText, device names or capabilities. Point
counts belong to Classic's renderer, not generic artifact metadata. There is no
waveform, artificial session numbering, CPU/GPU percentage or simulated readiness.
Camera and housing studies reuse approved standalone Home assets, clearly labelled
Illustrative; the reference screenshot itself is never rendered. Canonical axes
remain mm, right-handed +X right / +Y forward / +Z up.

## ScanModel contract

ScanModel is an internal QObject registered in `Mantis.Studio 1.0`. `bridge` is an
injectable QObject reference held by QPointer. `data` is a read-only QVariantMap;
`changed` fires only when the normalized map differs. It observes the bridge's
GUI-thread `changed()` after its complete DTO projection; snapshotReady alone is
too early for those properties. Replacement disconnects the old source. Detach or
destruction clears all retained source data and invalidates presentation identity.
There are no commands, client, RPC, timer, thread, pixel/Published buffers or renderer.

| Data fields | Meaning |
| --- | --- |
| source, hasSnapshot, confirmed, freshness | Model source is live; strict bool snapshot AND connection confirms current state; otherwise waiting or explicit last-known |
| project, projectEpoch, identityValid | Sanitized display identity; monotonic invalidation token for full raw identity transitions including A→B→A and source replacement |
| captureActive, lastKnownCaptureActive, captureStatus, captureStatusText | Confirmed active/idle or unknown, separately retaining last-known evidence |
| busy, runtimeError, issues | Existing bridge operation/error state; issues preserve phase/kind/integer code/component/message; unrelated success cannot erase bridge phase policy |
| devices, artifacts, jobs, diagnostics | Bounded live/source-tagged read-only inventories, preserving source order |
| selectedArtifact, latestPointCloud | Advertised finalized cloud status labels only; “last advertised” means source order, not timestamp/chronology; never loads a packet |
| readiness, previewStatus | Readiness unknown; existing preview remains in Classic; connection does not imply sensor readiness |
| canOpenClassicAcquisition, commandsAllowed | Navigation allowed for the live provider; command permissions permanently false; QML separately rejects mock navigation |

Inventories inspect at most 256 entries and emit at most 12 rows (issues 8,
diagnostics 12). `devicesLimited`, `artifactsLimited`, `jobsLimited` conservatively
signal bounded samples. Only QStringList capture capabilities qualify devices;
a loose captureSupported bool is insufficient. Artifacts are only advertised
RawCapture/PointCloud records. Missing/wrong map/list/string/bool types have
explicit empty/unknown semantics. Display strings are bounded (normally 192,
identity labels 80, status 64, errors/diagnostics/project 512) and plain text;
control/bidi format characters are removed from display.

Exact inventory IDs up to 4096 UTF-16 code units are retained, never truncated into
keys; oversized/empty IDs are omitted, repeated IDs deduplicated in source order.
The raw project identity is retained separately up to 65536 code units; larger
identity input is rejected and inventories withheld. A complete inventory is
required before advertising the last PointCloud; an incomplete bounded sample
cannot claim a latest cloud. QML local row selection resets on project/source
changes and reconciles removals. No selection can issue an artifact request.

Mock never binds ScanModel to a bridge. Hybrid starts with live evidence and offers
an explicit separate Demo / Mock view. Selecting demo detaches the model, with no
copying of live IDs or status into fixtures. Returning live rebinds the existing
bridge. The shell's existing live bridge may continue its own polling while the
hybrid demo is visible; the demo itself has no transport or command authority.

## Migration boundaries

M3c camera/geometry integration, M3d replay and M3e supported setup are deferred.
The reference's hardware controls are unavailable with accessible explanations.
Notes are read-only and have no storage or local draft. No preferences/project
writes are introduced. The backend project/replay isolation gate remains pending;
this presentation model does not repair or bypass it. See [verification](validation-m3b.md)
and the [route table](README.md#routing-and-existing-workflows).
