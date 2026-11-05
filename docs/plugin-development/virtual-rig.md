# Deterministic virtual acquisition rig (SIM-01A)

The separate `org.mantis.virtual-rig` plugin queries the existing
`MantisAcquisitionV1`. It is absent from discovery unless
`MANTIS_VIRTUAL_RIG_PROFILE` names an explicit valid profile. Legacy
`virtual-scanner` discovery and pixels are unchanged. No hardware, Qt or simulator
service is required. This is a software model, with no physical synchronization,
optical accuracy or sustained Recorder throughput qualification.

For example, start the normal daemon with:

```sh
MANTIS_VIRTUAL_RIG_PROFILE="$PWD/profiles/virtual-rig-community.json" \
  build/headless/bin/mantisd --project /tmp/VirtualRig.mantis
```

Supply the normal daemon authentication environment as described in BUILDING.md.
`virtual-rig-community.json` models 640×480 GREY stereo at 30 and 29.97 FPS.
`virtual-rig-trigger.json` describes opt-in dual 1280×720 packed Y10P at nominal
120 FPS. Set both rational rates to 30/1 for the medium-resolution-rate profile.
The latter profile is configuration, not evidence of measured 120 FPS or
276.48 MB/s recording. Sustained qualification belongs to #93.

## Profile contract

Profiles have version 1, at most 16 KiB and eight nesting levels. Unknown fields,
duplicate JSON keys, wrong scalar types and out-of-range values fail before open.
All fields shown in the examples are required. Numbers are integers; rational
`rate_num/rate_den` permits independent rates without floating-point scheduling.

- One or two cameras, ordered `left`, then `right`; distinct stable camera,
  stream and native-clock identities. Open the parent, not a component.
- Width 16–1280, height 8–720; `GREY` or `Y10P`. Packed width is a multiple of
  four. Stride is at least the active row size and at most that size +4096.
  Every frame contains exactly stride × height bytes, with zero row padding.
- Capacity 2–16 captured samples per camera, plus two ADR-026 pending samples
  and one in-flight sample per camera. Nominal rate is 0.1–240 FPS, numerator
  ≤240000, denominator ≤1001. A generation lasts at most 24 hours. Arithmetic
  uses bounded operands and quotient/remainder rate/drift calculations.
- Phase, trigger latency and delivery delay are 0–10 s. Native offset is
  ±1000 s, drift is ±100000 ppm and deterministic jitter is 0–1/8 nominal
  period. Jitter adds a seeded integer delay to exposure, keeping event order.
- Trigger mode requires one shared rational rate and zero free-running phases.
  Free-running stereo tolerance must be at least the larger nominal half-period.
  A bound is never widened to obtain a pair.
- Calibration is exactly `org.mantis.virtual-rig.pinhole-WxH.v1`, revision 1,
  for the configured dimensions. The analytic immutable calibration is defined
  below and also exposed in discovery metadata. This reference is a built-in
  test calibration, not a solved/active project calibration artifact.

Up to 32 explicit faults are permitted, with distinct `(camera, at)` positions.
Camera indexes are zero based; `at` is an exposure/trigger index from 1 through
20736000. `delay_ns` is required, zero except for positive `delay`/`stall` (≤10 s).
Fault kinds are `drop`, `missing-trigger`, `duplicate-trigger`, `stale-trigger`,
`duplicate-counter`, `reverse-counter`, `reset-counter`, `generation-change`,
`missing-time`, `incomparable-time`, `delay`, `stall` and `disconnect`.
Trigger faults require trigger mode. A stall holds deliveries until its explicit
release time while exposures continue and finite source buffers may overrun.
Delay can cause out-of-order delivery, which fails explicitly on receipt.
Counter/clock/trigger-generation violations fail rather than fabricate continuity.

## Exposure, clocks and ownership

Each event retains its requested trigger time (trigger mode only), modeled
exposure time, native camera time, availability time, generation and exact
trigger association. Native time is exposure + offset + exposure × drift/1e6,
with integer truncation. Each start resets this native timeline to its configured
offset/phase/latency; it does not continue the preceding run's native clock.
The profile's stable `clock` remains discovery provenance and is also emitted as
`native_clock_id` metadata. The packet's effective native `clock_id` is
`<configured-clock>/epoch/<generation>`: stable within one acquisition generation
and distinct after restart, reopen or DSO reload. Stable parent, component and
stream identities do not change with the epoch. Retained packets and schema-2
RawCapture/replay keep their original effective clock IDs and timestamps.

Capture and availability name a generation-scoped
simulation reference clock; host origin, receipt clock and processing time are
separate fields. Real host receipt/processing uses `std::chrono::steady_clock`,
identified as `std.chrono.steady_clock`, without asserting a platform-specific
clock implementation. Injected host time uses `<rig-id>/host/<generation>`
because it also resets at start. No host receipt is substituted for camera time.
Free-running pairing uses the explicitly known analytic mapping into the simulation reference;
unknown/missing mappings or timestamps cannot establish stereo correspondence.

A source thread advances the same model with steady time independently of
`next()`, rendering and consumer callbacks. The plugin-private queried clock
control in `virtual-rig/control.hpp` selects injected virtual time before start
and advances exposures independently of consumption. It also exposes observed
wait entry/cancellation for deterministic lifecycle tests; it is not a public
SDK interface or a hardware capability. Tests exercise the actual compiled DSO.

Source queues drop the oldest captured sample with explicit `source_overruns`.
Large clock advances use binary search and materialize at most capacity samples
per camera, accounting for omitted exposures without historical deadline debt.
Work per advance is bounded by two cameras, ≤25 search steps, capacity ≤16 and
the ≤32-entry fault table. Selecting the retained tail probes at most capacity
+ fault-count indexes, keeping actual captures across dropped/missing events.
Global event order is exposure time followed by camera index; a disconnect ends
the generation after accounting all earlier events on both cameras. A sample is
an immutable analytic scene observation; pixels are materialized only after
selection, not reacquired at delivery time.
`next()` reads at most two observations per call and uses the existing X1
ADR-026 nearest/bracketing policy, startup/steady exclusion budgets and exact
exclusion identities. Trigger mode additionally validates per-exposure trigger
identity/generation. Neither mode advertises hardware SyncQuality or a physical
trigger capability. Modeled exposure delta is distinct from unavailable physical
exposure skew. Free-running output has no fabricated trigger group or ID.

For each camera:

```text
exposures = source_drops + source_overruns + published
          + startup_unmatched + steady_unmatched + shutdown_unmatched
          + source_depth + pending_depth + inflight_depth
```

Missing triggers are counted separately because they produce no exposure.
Stop/failure accounts retained samples; start/reopen creates a fresh generation
with empty queues, trigger state and counters. Generation identity uses the host
steady epoch plus a monotonic increment, so
unloading the plugin cannot reset it to a previous generation. Reproducibility
compares scene samples/event ordering/native times; generation identity and real
host scheduling remain deliberately distinct between capture runs. Acquisition
calls are serial;
stop/destroy follow `next()` per the unchanged SDK. Waits have finite deadlines;
the producer is stopped, notified and joined before destruction.

Payload bytes are written directly into one host SDK allocation per selected
image, with no intermediate image copy. Published SDK buffers cannot safely be
recycled by a plugin: v1 provides neither mutable republishing nor a query of
external retention. Consumers may retain immutable buffers beyond callback and
plugin destruction. The plugin releases its own references after the callback,
including allocation/map/publish/emit failures. Its own simultaneous image
ownership is at most two images (maximum 2 × 4,101,120 bytes with padded rows);
retained consumer/Recorder memory is owned and bounded by those consumers.
Source/pending/in-flight records contain no pixel arrays. Full-resolution
functional tests are bounded samples, not a throughput or long-soak claim.

## Immutable analytic scene/calibration v1

Both cameras have fx=fy=width pixels and principal point
`(floor(width/2), floor(height/2))`, with zero distortion and parallel optical
axes. Right is translated +40 mm along left optical X. A fronto-parallel plane
is at Z=10×width mm. The point `(X,Y,Z)=(40,0,10×width)` mm projects at
`(cx+4,cy)` in left and `(cx,cy)` in right: independently predicted disparity
is exactly four pixels. This is a static synthetic plane/point, not optical
metrology or a calibrated real camera.

At image `(x,y)`, let plane coordinate `s=x+4*camera_index`. The feature above
has 10-bit intensity 1023; elsewhere intensity is
`(13*s + 7*y + seed%251)%251`. GREY contains intensity/4. Y10P stores each
group of four samples as four high-eight-bit bytes followed by their low two
bits in lanes 0,2,4,6 (MIPI RAW10 / ADR-025). The independently predicted shared
texture and feature correspondence distinguish this scene from unrelated
camera gradients. Complex optics and laser models remain #94.

The first four pixels of row zero are a diagnostic identity strip, excluded from
scene correspondence. Their GREY values encode successive low-order bytes of
the captured exposure index; Y10P stores those values multiplied by four. This
witness detects stale/wrong-frame bytes and makes retention tests sensitive to
later image writes while leaving the independently projected target unchanged.
