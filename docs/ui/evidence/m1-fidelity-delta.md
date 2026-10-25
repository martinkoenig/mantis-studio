# UI-M1 visual delta inventory (before implementation)

Approved target: [01-home.webp](../reference/01-home.webp), 1536×1024.
Baseline: reviewed `090fa253675699ce459f5876f0a249616c36e0dc`; actual Qt 6.4
software/offscreen capture `build/ui-m1-fidelity/before/actual-mock-1536.png`.
Existing full state matrix preserved in `build/ui-m1-fidelity/before/`.
Coordinates below measured from the full-size images; approximate, not pixel gates.

1. **Shell** — reference navigation ≈166, bar 54; current navigation 176,
   bar 56, footer 30. Preserve shell/runtime identity, remove development-only
   footer branding in navigation; account for the footer in content heights.
2. **Hero** — reference x176–1160/y54–352 (984×298), cinematic scanner/casting,
   strong 30px heading and substantial actions; current x188–1168/y68–262
   (980×194), gear and broad empty field. Target 280px, original offline rendered
   scanner/casting composition, layered dark atmosphere, restrained heading and
   three honest navigation/planned actions. Artwork never proves hardware status.
3. **Projects** — reference y371 heading, y402–600 cards (≈230×198), object art
   ≈100px tall plus two metadata lines; current y320 heading, y346–488 cards
   (236×142), art only 78px. Use four distinct substantial original shaded 3D
   objects in ≈198px cards, larger artwork and illustrative metadata. Live uses
   a similarly sized current-project/history-unavailable feature, never four fake
   historical projects. Hybrid samples stay below separately labelled live data.
4. **Actions** — reference y621 heading, y650–790 four ≈140px cards with distinct
   device/bolt/learning/example visual language; current y505/y529–650 generic
   121px cards. Target 150px, device illustration, primary Scan button, working
   read-only learning view and sample focus (disabled in live with explanation).
5. **Activity** — reference y810 heading, y834–1006 compact five-column table;
   current artifacts y665–926 and activity only starts y941, no visible rows.
   Delete standalone artifacts panel and `homeArtifacts` CTA in every mode.
   Recent Activity follows actions directly, with Name/Type/Status/Date/Size rows.
   Live events and artifacts remain grouped by provenance, sequence vs ID order;
   date/size explicitly unavailable. Aim heading ≈850 and actual rows <950.
6. **Rail** — reference x1172–1520 (~348px), scanner 252px, metrics 170px,
   jobs 190px, illustrated tips 290px; current x1184–1520 (~336px), repeated
   badge panels and bulky jobs 412px. Use differentiated scanner illustration
   (mock only), dense logical live devices, mock gauges/live unknowns, compact
   diagnostic jobs, illustrated static tips with a working local view. Remove
   repeated mock badges while retaining global and section/row source boundaries.

First viewport sketch (existing 176px shell + 56px bar + 30px footer):

```text
x188                    main ~980                 gap16  rail ~330
68   ┌ hero 280: heading/copy/actions + scanner/casting ┐ ┌ scanner ~252 ┐
348  └────────────────────────────────────────────────┘ │             │
     source/freshness (20)                               └─────────────┘
     Recent Projects heading + 4 cards ~198               ┌ system ~160 ┐
     [housing] [rotor] [bracket] [cover]                   └─────────────┘
     Quick Actions heading + 4 cards ~150                 ┌ jobs ~280   ┐
     [devices] [scan] [learn] [examples]                   └─────────────┘
~850 Recent Activity heading / table header               ┌ tips ~190   ┐
~900 actual activity rows                                 └─────────────┘
994  shell footer; later rows vertically scroll
```

Verification will compare reference/baseline/new side-by-side at full size and
inspect 1080/1536/1920 mock/live/hybrid, stale/empty/wire screenshots. Qt 6.4
layout-cache isolation, source authority and acquisition contracts stay frozen.

## Final comparison / inspection checklist

Full-size [reference / before / after](fidelity/reference-before-after.jpg),
[old 1536](fidelity/before-mock-1536.png), [new 1536](fidelity/after-mock-1536.png).

| Section | Final observed change | Difference from approved concept |
| --- | --- | --- |
| Shell | Original 176px navigation, 56px bar; development milestone branding replaced with product/workspace label | Retained real runtime/source bar and 30px footer; no fabricated avatar or ready state |
| Hero | 972×280 at x188/y68, original scanner/casting with depth, light, dark atmosphere and 29px heading; three aligned actions | Original simpler mechanical forms/studio backdrop; working navigation and planned Import replace unsupported creation/import |
| Projects | Heading y387, four substantial 234×198 cards at y414–612; 120px object art and sample metadata | Read-only illustrative boundary; live current-project feature/history unavailable; no fictional live history |
| Actions | Four distinct 150px cards y653–803, scanner/bolt/book/object cues and primary Scan treatment | Scan opens acquisition without starting; Learn opens local static guide; examples focus showcase |
| Activity | Heading y815, header y845, first actual row y876–906; three complete rows plus start of fourth before content edge y982 | Live provenance groups and unknown Date/Size replace fictional globally timed activity. No artifacts card/CTA |
| Rail | Scanner, gauges, compact diagnostic jobs, illustrated tips all present in first primary mock viewport; no horizontal overflow | No live utilization/physical claims; populated live's multiple logical devices/diagnostics can require vertical scrolling |

- [x] Compared actual primary image with the approved concept, not just baseline.
- [x] Four different original mechanical silhouettes; larger art, material, shadow and depth.
- [x] No standalone artifacts panel in mock/live/hybrid; Activity directly after actions.
- [x] Primary mock has substantive hero, four projects/actions and visible actual Activity rows.
- [x] Primary live confirmed/no-runtime/stale and hybrid separation inspected.
- [x] 1080 mock, long/empty live and hybrid showcase/scroll inspected; controls reachable through vertical scrolling.
- [x] 1920 mock/live/hybrid inspected; initial cropped hero corrected to preserve full scanner/casting.
- [x] Re-rendered brighter/grainier first assets with more samples; fixed initial cropped bracket.
- [x] Native Qt 6.9 Wayland/OpenGL window additionally inspected (1.25 device pixel ratio stated separately).

Remaining visual differences are the original, simpler CAD forms and studio
backdrop, Qt/system typography, real source/status metadata and shell footer,
and authority-driven live empty/history/telemetry fields. These are explicit
review scope; this record does not claim pixel-identical or independent visual
acceptance. No physical scanner/metrology acceptance is inferred.
