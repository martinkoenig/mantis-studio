# UI-M1 Home contract

Home is a presentation-only dashboard above the public Studio bridge. It preserves
[Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md), [ADR-002](../adr/002-frontends-are-clients-rather-than-engine-owners.md),
[ADR-036](../adr/036-studio-calibration-workspace-uses-public-calibration-api.md)
and the binding [reliability standard](../architecture/reliability-performance-and-validation.md).
The [approved Home reference](reference/01-home.webp) guides composition, never data authority.

Implementation plan: (1) bounded read-only snapshot presentation, (2) dedicated
Home with original geometric hero and small reusable cards, (3) component,
public-wire integration, interaction, resize and screenshot regressions, (4)
manual visual review, full local checks and exact-final-commit five-job CI gate.

| Section | Inspected authority / available fields | Absent, stale or failed | Mock / hybrid | Click intent |
| --- | --- | --- | --- | --- |
| Hero | Local typography and geometric illustration | No hardware claim | Same art, explicitly illustrative | Go to Scan opens Acquisition; never starts capture |
| Runtime | StudioBridge.connected, hasSnapshot, busy, errorDetails (phase/kind/code/component/message) | Awaiting first confirmation; failed confirmation unconfirmed; previously confirmed snapshot last known. Operation rejection retains confirmation when snapshot succeeds | Mock never consumes bridge; hybrid real summary | Read-only diagnostics |
| Project | Snapshot project_path through bridge.project | Path unavailable; no history, sizes or dates. Retained path labelled last known | Deterministic showcase projects only in separate demo area | Current project opens Acquisition; Projects opens UI-M2 foundation |
| Devices | Bridge top-level logical descriptors: id/name/plugin/capabilities | Empty discovered list distinct from unconfirmed state; physical availability/readiness unknown | Demo scanner has no runtime capabilities or actionable identity | Devices route; calibration stays in controller-verified Devices workflow |
| Jobs | id/name/state/progress/diagnostics | Queued/Running/Completed/Failed/Cancelled explicit; missing/nonfinite/out-of-range/zero progress unavailable (proto3 scalar has no presence bit); last-known label on failure | Demo jobs separate from live counts and lists | View jobs opens Acquisition, without selection or cancellation |
| Artifacts | id/type/state/chunks (zero cannot establish scalar presence) | Empty/unknown/stale explicit; no timestamps, size or geometry inferred | Demo artifact rows clearly labelled | View artifacts opens Acquisition, without loading an ID |
| Events | sequence/kind/component/message | Ordered by sequence, not invented dates; unavailable sequence explicit | Separate deterministic illustrative events | View runtime opens Acquisition |
| System | No bridge CPU/GPU/RAM/storage metrics | Metrics not available from this runtime | Demo remains honest about unsupported telemetry | Read-only |
| Quick Actions | Existing local routes | New/Open/Import unavailable with visible UI-M2 explanation | Navigation only; acquisition/calibration controls remain disabled in mock | Acquisition, Devices, Calibration, Projects foundation |
| Help | Static workflow guidance | No packaged tutorial browser or release feed | Identical information, no fictitious releases | Read-only |

Home owns no session, transport, command method, watcher or polling timer. All
CTAs emit local route intent without any device/job/artifact identity. Calibration
entry selects no device and activates no revision. Actual recording, cancellation,
artifact loading and calibration operations remain in their existing workspaces.

Live shows only snapshot data. On loss of confirmation cached fields are retained
and explicitly labelled **Last known · current state unconfirmed**; connection is
never inferred from an error code or discovered descriptor. Structured errors
remain intact in the bridge; Home renders bounded plain-text previews by phase.
Hybrid renders live data first and a separate **Demo / Mock showcase** below; demo
counts never enter live totals. Mock has no runtime dependency or command authority.

The wide layout uses a main column (hero, project, quick actions, artifacts/events)
and a right rail (devices, system, jobs, guidance). Below 1100 content pixels the
rail stacks beneath the main column. Content scrolls vertically with an explicit
indicator; no Home horizontal scrolling. Text wraps or elides, paths and all
runtime strings use PlainText. Shared buttons retain hover/pressed/focus/disabled
states and accessible names/descriptions. Unsupported controls explain their
reason in adjacent visible text. Dynamic rows live in Qt Quick Columns/Grids
behind stable Items, outside nested Layout caches (Qt 6.4 M0 regression).

## Known follow-ups

- Before UI-M3: the existing bridge may retry automatic loading of a permanently
  unreadable PointCloud every 500 ms. Home introduces no data-plane call or retry;
  acquisition retry/backoff needs a separate reliability/performance correction.
- UI-M2 project history/browser/control, telemetry, tutorial browser and native
  desktop accessibility/hardware validation remain separate work. No calibration
  quality, scanner accuracy, firmware or physical readiness is inferred here.

## Presentation bounds

Each update inspects at most 256 rows per section, sorts that bounded sample by
stable identity (active jobs first) or descending event sequence, and displays
3 devices / 4 jobs / 6 artifacts / 4 events. Counts refer to snapshot entries,
not inferred active/connected hardware. Oversized sections announce a limited
sample; existing acquisition exposes the complete list. Optional/malformed lists
are unavailable rather than fabricated zero. Text is bounded to 192 characters
for compact fields, 512 for diagnostics, 4096 for project paths and 8 capabilities
per device, with an ellipsis for truncation. Original bridge diagnostics/data remain
intact. Zero scalar progress/chunk/sequence values are conservatively unavailable
where proto3 cannot prove presence. Runtime-controlled strings are plain text.

The bridge's additive hasSnapshot flag records whether this bridge has ever
received a confirmed snapshot, preserving last-known semantics across a mock/live
mode swap. Its busy lease lasts until worker completion is applied on the GUI
thread: a completed-but-undelivered result cannot be replaced by a subsequent
request. This narrow delivery fix preserves one watcher and no request queue.
