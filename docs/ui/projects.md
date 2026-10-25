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

The workspace fills its available width; it never centers a capped island. A
188 logical-pixel navigator and an adaptive 320–420px inspector frame the browser.
The inspector reaches about 361px at 1536×1024 and 420px on large windows, and
remains pinned to the right edge. The multipane breakpoint follows the minimum
usable combination of rails and browser (1120px of workspace width).

Desktop gallery filters, navigator, inspector and bottom contents are docked.
A clipped, vertical-only native Flickable owns the gallery. Separate Flickables
own the table body and any navigator/inspector overflow; no outer page scroller
exists. The bottom panel receives 31% of the body height, bounded to 188–300px,
independent of tab, row count and inspector metadata. It ends at the workspace
bottom, with the existing 12px shell/footer inset. Empty tabs use that same region.
Compact Filters/Details remain discoverable and stack inside the browser viewport;
the table stays docked. Persistent pane components move between stable dock and
stack slots rather than rebuilding delegates.

Grid columns depend on actual gallery width: a readable 174px minimum card plus
12px gap, with a hard six-column limit. The approved desktop size gets four;
large and ultrawide sizes get five or six. Cards grow to 420px, with single/two
results capped at 320px. Preview height is 128–230px, preserving subject aspect
and safe margins. List thumbnails remain bounded and the row fills its viewport.
Six semantic tag colors are shared by dots and chips: Housing blue, Prototype
purple, Customer A orange, R&D green, Quality Control yellow, Tutorial gray.
Counts derive only from the twelve illustrative studies. Live hides these tags
and counts and states that project tags are unavailable.

Grid/list preserves canonical selection, query and facets. The numeric gallery
offset is retained where it fits; native bounds clamp it when the new content is
shorter. Keyboard-focused rows anchor within their own viewport after layout
settles. Explicit tab/source changes start their own table at the top; they never
reset the gallery. Harmless equal snapshots cause no update.

Precise phased touchpad pixels move the targeted viewport immediately, preserving
platform direction and OS momentum pixels without animation. Standard stepped
wheel, unphased input and touch dragging retain Qt's Flickable handling. A scoped
presentation-only adapter requests one public `flick()` after ScrollEnd only for
recent phased motion without OS momentum, averaging at most four pixel/time
samples. The 100ms pause cutoff follows Qt 6.4's native release cutoff. A measured
6000 logical px/s ceiling preserves gentle versus fast swipes that Qt's default
2500 ceiling flattened. Qt owns release deceleration, trajectory and bounds.
There are no product timers or position animations. Scrollable desktop viewport
boundaries consume precise pixels; no page ancestor or sibling scroll fallback
exists. Compact stacked panels without their own overflow allow the enclosing
browser to scroll.
The global minimum remains 1080×720; horizontal workspace scrolling is disabled.

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

The inspector, controls and gallery are instantiated once. Twelve gallery slots,
128 artifact slots and six hidden-in-Live tag slots survive geometry/source changes
outside nested Layout caches. Gallery/table arrows, Tab and Page/Home/End reveal
within the correct viewport. Escape closes compact Details/Filters and returns
focus to the toggle. Header/source controls wrap; no project data is loaded for
this presentation. See the V2 record in [validation](validation-m2a.md) for
native-input limitations and the region-by-region visual audit.

Reference deviations are deliberate and visible: unverified cloud/sharing/size/
scanner claims are absent; live geometry is an unavailable preview, not sample art.
New/Open are disabled, and sample contents/authors/dates/sizes are explicitly
illustrative. Eight new original mechanical/palette studies replace reference
vendor casts, statue and shoe imagery. Existing Home art and composition are unchanged.
