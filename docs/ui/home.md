# UI-M1 Home contract

Home is a presentation-only dashboard above the public Studio bridge. It preserves
[Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md), [ADR-002](../adr/002-frontends-are-clients-rather-than-engine-owners.md),
[ADR-036](../adr/036-studio-calibration-workspace-uses-public-calibration-api.md)
and the binding [reliability standard](../architecture/reliability-performance-and-validation.md).
The [approved Home reference](reference/01-home.webp) is the composition target;
its fictional product data never establishes runtime authority.

The main column contains a 280px illustrated hero, Recent Projects (four 198px
read-only sample cards in mock), four 150px Quick Actions, then **one Recent Activity
table**. The standalone Example artifacts / Current project artifacts card and
`homeArtifacts` CTA are removed in **mock, live and hybrid**. No replacement
artifacts panel is added elsewhere on Home. Artifacts remain in the bridge/model
and in Acquisition's working browsing, replay, processing and export controls.

The right rail contains Scanner, System Status, Running Jobs and Tips & Updates.
Below 1100 workspace-content pixels it stacks beneath the primary region. Otherwise
its 348px preferred width stays at the **right usable workspace edge**, with a
16px gutter. The primary region takes the remaining width; Home has no global cap.
Below 1720 primary pixels it retains the approved vertical composition. Above that
threshold a stable inner GridLayout places Hero/Activity in the left lane and
Projects/current project/Quick Actions in the right lane, with a 24px gutter.
Each lane receives half the primary width. Project/action cards remain 150–430px
wide across supported viewports; project preview height grows modestly from 120
up to 180px, with the existing 78px metadata body. The separate Hybrid gallery has
a **local** 1680px reading bound, independent of the rail/dashboard layout.

Dimensions remain logical pixels at HiDPI, without global zoom or a minimum-size
change. Home scrolls vertically with no horizontal scroll. At 1536×1024 mock,
Activity and substantive rows remain above the footer, as in the approved layout.
See [corrective evidence](evidence/corrective/README.md) for measurements, full-size
reference/baseline/after comparisons, DPI, maximize/restore and resize validation.

| Section | Live authority / unavailable fields | Mock / hybrid | Click intent |
| --- | --- | --- | --- |
| Hero | Static original scanner/casting art; explicitly illustrative, never a feed or connected-hardware claim | Same art | Go to Scan → Acquisition; Browse Projects → UI-M2 foundation; Import disabled with visible/planned and accessible explanation |
| Runtime | `connected`, `hasSnapshot`, `busy`, structured errors; confirmed vs never-confirmed vs last known | Global mode label; mock detaches HomeModel and never polls runtime | Read-only diagnostics |
| Projects | Current `project_path` only; history, dates, sizes unavailable. Substantial current-project feature and UI-M2 history explanation | Mock has four explicitly illustrative read-only mechanical studies; hybrid samples remain below the live dashboard in a separate labelled area | Current runtime → Acquisition; sample cards have no menus/open/import IDs |
| Scanner | Bounded logical descriptors and advertised capabilities; discovery does not prove physical readiness. Firmware, serial, calibration validity, temperature unknown | Scanner picture only in explicitly illustrative mock summary, with no runtime identity/capabilities | Devices → Devices; Calibration → existing guided workflow, without selection or activation |
| System | No utilization API: gauges empty, values em dash, explanation visible | Mock-only illustrative gauges; hybrid uses live unavailable values | Read-only, no OS polling |
| Jobs | Real name/state/progress/diagnostics. Zero, nonfinite or out-of-range progress unavailable; failure/cancellation distinct; stale labelled | Bounded illustrative jobs with empty IDs; mock detail button disabled with visible/accessibility reason | Live/hybrid View jobs → Acquisition, without cancellation/selection |
| Activity | Events `(sequence, kind, component, message)` ordered by descending sequence, then separate artifacts `(id, type, state, chunks)` in deterministic ID order. Date/Size em dash; row types and group order explicit, no combined chronology | Mock Scan/Mesh/Texture/Export rows have fictional dates/sizes only inside the illustrative table; no actionable IDs. Hybrid table stays live | Read-only; no artifact loading from Home |
| Quick Actions | Devices and Scan are navigation; Learn opens a packaged read-only guide; Example Projects unavailable in live with visible/accessibility reason | Mock/hybrid Browse examples scrolls to and focuses labelled sample gallery | No capture, pairing, calibration activation or import |
| Tips | Static workflow guidance and decorative local object thumbnail; no video/release-feed claims | Same real guidance | Opens the same local guide; guide's Devices button routes only |

Home owns no session, client transport, watcher, timer or polling. Enabled runtime
CTAs emit only route strings, without device/job/artifact identity. Actual commands
remain in existing workspaces, including mock command gating. Guide/examples
controls preserve keyboard focus and accessible names. Unsupported controls
resist mouse, keyboard and accessibility press invocation.

Live never replaces unavailable data with samples. On loss of confirmation,
retained fields say **Last known · current state unconfirmed**, alongside bounded
structured diagnostics. Operation rejection with successful snapshot confirmation
retains connected state. Runtime-controlled strings use PlainText, wrap/elide
and retain accessible bounded text. Dynamic delegates stay behind stable Items
outside nested Layout caches, preserving the Qt 6.4 removal/resize workaround.

## Presentation bounds and assets

The accepted HomeModel is unchanged: each notification inspects at most 256 rows
per section, orders that bounded sample and displays 3 devices / 4 jobs / 6 artifacts /
4 events. Counts describe snapshot entries. Invalid lists are unavailable, never
fabricated zero. Text bounds: 192 compact characters, 512 diagnostics, 4096 project
path characters, eight device capabilities, three structured issues. Equal normalized
snapshots emit no update. Proto3 zero progress/chunks/sequence cannot prove presence.
The bridge retains its GUI-result-delivery busy lease, one watcher and no queue.

Six [original offline assets](../../ui/home/assets/README.md) retain the original
procedural models and accepted scenic Hero JPEG. Five genuine RGBA PNG foregrounds
replace baked-background JPEG subjects. `HomePreview` owns the neutral graphite
backdrop, 12–22px safe area, aspect-fit image and static contact shadow. Scanner,
Actions and Tips also use transparent subjects; Hybrid reuses the project surface.
Packaging validates alpha/framing and preserves RGBA. Total packaged bytes are
765,878; native RGBA8 pixels occupy 5,609,480 bytes, plus bounded Qt cache/renderer
overhead. No authoring tool, reference pixel, network image, extra render worker or
animation timer is added. Application and tests share `MANTIS_STUDIO_QML_FILES`;
Studio OFF remains Qt-free.

## Known follow-ups

- Before UI-M3: the bridge may retry automatic loading of a permanently unreadable
  PointCloud every 500ms. Home adds no loading/retry; bounded retry/backoff remains
  a separate acquisition reliability correction.
- UI-M2 project history/browser/control and telemetry remain planned. This task
  adds a static local guide, not a tutorial browser, marketplace or release feed.
- Physical scanner, optical accuracy, calibration quality, native OS screen-reader,
  Windows/macOS and metrology acceptance remain separately scoped. Software
  screenshots and logical discovery establish none of those claims.
