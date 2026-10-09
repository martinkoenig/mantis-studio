# UI-M2a Projects contract

The approved [Projects reference](reference/07-projects.webp) defines composition,
not runtime capabilities. This milestone starts from accepted Home commit
`6b07fd34ab193ee32dc52c6ffba76a22a59497ab`. Home's components/assets are preserved.

## Verified surface and actions (published before implementation)

| Surface | Public authority | UI-M2a interaction / gap |
| --- | --- | --- |
| Current project | `snapshot.project_path`, one shared daemon project | Select/inspect; runtime-host path, never a laptop filesystem catalog |
| Artifacts | Snapshot `id/type/state/chunks` | Bounded search, ID/type/state sort/filter and read-only selection; no data-plane loading |
| Jobs/events | Runtime snapshot descriptors | Daemon-wide; association with current project unavailable. Never imply project-scoped counts |
| New/Open | Public `Request.project_open(path, create)` via `Client::call` exists | **Blocked by replay-context isolation defect**, not by lack of a convenience wrapper |
| Import/Recent/catalog | No verified public catalog/import contract | Disabled with visible and accessible explanation |
| Favorites/tags/locations | No persisted public contract | Explicitly illustrative sample facets only; unavailable live |
| Versions/mesh/CAD/notes | No revision/CAD/notes contract | Mock-only illustrative contents; live unknown types stay literal, unsupported tabs explain gaps |
| Export | Public artifact export exists, project package export does not | Explicit navigation to existing Acquisition artifacts, never labelled project open/export |
| Clone/archive/delete/share/cloud | No verified public contract | Inert controls; no speculative commands |
| Demo gallery | Twelve deterministic local studies | Select, grid/list, name/tag search, supported facets/sort, illustrative contents only; no runtime IDs |
| Hybrid | Separate live-first and sample sections | Explicit selection/search/filter source; counts never merged |

## Safety investigation

The accepted public operation rejects active captures and busy jobs and locks the
next store before swapping it. Token authentication applies to the whole daemon;
there is no per-project access policy. Paths are evaluated on the runtime host.
The mutation affects **all** connected clients. `create=true` can open an existing
project; it is not an exclusive create. A response alone cannot confirm identity.

An isolated real-daemon/public-wire experiment captured and finalized a virtual
scan in A, completed a replay, opened B, and confirmed B through a second client.
`preview(oldReplayJobId)` still returned A's packet and wrote it into
`B/cache/previews`. `Runtime::open_project` clears captures but retains replay
queues and preview leases. A GUI cannot safely invalidate another client's queue.
**New/Open stay unavailable** until an independently reviewed runtime change
establishes project generations and invalidates old replay/preview context.
No daemon/store/protocol correction is included here. See
[backend proposal](projects-backend-gap.md) for the isolated follow-up.

The bridge's frontend context is separately invalidated only on a **confirmed raw
project identity change**, including an external client's switch/reconnect. Last
known values remain explicitly stale before confirmation. This narrow change
must clear old acquisition selections/render/preview state and reject late old
preview delivery; it does not make the runtime mutation safe.

## Presentation and layout strategy

A read-only ProjectsModel observes the existing bridge, with no client, watcher,
timer or command. Typed source/freshness/availability fields distinguish never
confirmed, missing identity, confirmed empty and retained stale state. Raw identity
is separate from sanitized, bounded PlainText display. Artifacts use a bounded
512-entry inspected sample and at most 128 rendered rows; search explicitly covers
that sample, not the full project. Repeated normalized snapshots emit no update.

The workspace uses remaining available width, a 188px in-workspace navigator,
adaptive gallery and 316px right inspector with 12px gaps. Optional panes collapse
at compact widths with discoverable Filters/Details controls; opened compact panes
stack in the same vertical scroll. Gallery columns increase with available width,
keeping 180px-high grid cards near reference proportions without a global content cap or zoom.
Each grid card has a 300 logical-pixel width ceiling, including a single filtered
result; the current runtime-project card has a 420 logical-pixel ceiling. List rows
use available width with fixed thumbnail geometry. Hybrid runtime contents precede
the separate sample library. A spanning navigator cannot displace the gallery
from its filter header; extra space sits below the gallery instead.
Stable wrappers/delegates preserve selection and focus across harmless resizes.
The global minimum remains 1080×720; horizontal page scrolling is disabled.

Four accepted Home subjects and its continuous QML-owned preview surface are
reused. Eight additional original procedural Blender RGBA subjects have no baked floor
or background. Offline packaging validates alpha and framing; fixed source-size
bounds limit decode variants. Blender/Pillow never enter the application or CI.

## Validation strategy

Model, actual-QML and public-client fixtures cover authority, literal hostile text,
sampling, filtering/selection, disabled actions including accessible press,
failure/restoration and external project switches. Real-daemon diagnostics verify
published project operation semantics and document the isolation blocker. Full
Qt screenshots cover nine sizes and source/freshness states; DPR 1/1.5/2 and live
resize stress exercise actual breakpoints. Existing tests remain enabled. Full
Studio ON/OFF, sanitizer and five-job exact-commit CI results are recorded in
[validation](validation-m2a.md), including native environment limitations.

## Final interaction and reference decisions

Mock has twelve read-only studies in fixed reference-inspired order, plus Name and
Study type sorts. Recent/favorite/tag facets and name/tag/type search are local
illustrative filters; Shared/Trash return explicit empty results. No favorite/tag
persistence is claimed. Filtering invalidates a hidden selection; grid/list preserves
its canonical sample key. Live uses full raw identity for canonical selection,
bounded display text for literal search, ID/Type/State sorting and actual type/state
filters. At most 512 descriptors are inspected and 128 rendered. All counts specify
snapshot, matching and shown scope. Missing or zero scalar chunks say Unavailable.

The lower pane includes Artifacts in addition to the eight reference tabs. Only
exact `org.mantis.RawCapture`/`org.mantis.PointCloud` and `org.mantis.Mesh` identities
support Scans/Meshes grouping; unknown types stay under Artifacts. Other live tabs
open explanations. UI-local tab changes never load or mutate data. Runtime
navigation is explicitly labelled **View runtime artifacts**, with no selection ID.

The inspector, controls and gallery are instantiated once. Twelve gallery slots
and 128 artifact slots survive data/mode/geometry changes outside nested Layout
caches. Tab focus and gallery arrows scroll content into view. Compact Details and
Filters are discoverable; Escape closes an opened optional pane and restores its
control focus. Header/source controls wrap and lower content scrolls vertically.
Grid cards stay bounded while columns increase (up to the available sample count),
rather than enlarging four cards across an ultrawide screen. No Home cap/scale or
minimum-window change is made.

Reference deviations are deliberate and visible: unverified cloud/sharing/size/
scanner claims are absent; live geometry is an unavailable preview, not sample art.
New/Open are disabled, and sample contents/authors/dates/sizes are explicitly
illustrative. Eight new original mechanical/palette studies replace reference
vendor casts, statue and shoe imagery. Existing Home art and composition are unchanged.
