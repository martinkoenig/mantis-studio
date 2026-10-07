# Extensible sensor platform

Status: Accepted future architecture; implementation deferred beyond v0.3

Normative decision: [ADR-038](../adr/038-extensible-sensor-observations-and-generic-studio-fallback.md).

Related decisions:

- [ADR-011](../adr/011-community-ui-extensions-are-declarative-by-default.md) —
  community UI extensions are declarative by default.
- [ADR-016](../adr/016-explicit-clock-domains-are-mandatory-for-sensor-timestamps.md) —
  sensor timestamps carry explicit clock domains.
- [ADR-020](../adr/020-scanner-functionality-is-represented-through-capabilities.md) —
  scanner functionality is capability based.
- [ADR-031](../adr/031-x1-remote-observation-boundary.md) —
  scanner-side observations are a normal distributed boundary.
- [ADR-032](../adr/032-distributed-runtime-and-worker-pools.md) —
  scanner runtimes retain capture authority.

## 1. Goal

A developer who adds an unusual sensor to a DIY scanner should not need to modify
Mantis core or write a complete Studio frontend before that sensor becomes useful.

The desired progression is:

```text
device can measure something
        |
        v
declare capability + typed stream
        |
        v
publish observations
        |
        +--> record / replay / inspect automatically
        |
        +--> generic Studio live view automatically
        |
        +--> optional generic spatial overlay
        |
        +--> optional custom processor / exporter / Studio extension
```

The easy path must be genuinely useful. Custom UI and domain-specific code are the
optimization path, not the admission ticket.

## 2. Architectural separation

Four concepts must remain independent.

### 2.1 Capability

A capability answers:

> What can this device or component do?

Examples:

```text
org.mantis.camera.image-stream
org.mantis.emitter.line-laser
org.example.radiation.detector
org.example.temperature.ir
```

Capabilities describe discoverable functionality and configuration surface. They
do not themselves define the measurement payload.

### 2.2 Data stream and schema

A stream answers:

> What typed observations does this capability produce?

Examples:

```text
org.mantis.ImageFrame
org.mantis.measurement.Scalar
org.mantis.measurement.Vector
org.mantis.measurement.Event
org.example.Spectrum
```

A device may expose multiple streams for one capability. A radiation detector, for
example, could expose raw detection events and a derived count-rate stream.

### 2.3 Processing

A processor answers:

> How can these observations be transformed or associated with other data?

Examples include temporal filtering, count-rate estimation, sensor calibration,
spatial association with a reconstructed surface, thermal/radiation mapping and
domain-specific export.

### 2.4 Presentation

Presentation answers:

> How should the user see or interact with these data?

Studio should provide a generic fallback for generic measurement families.
Specialized presentation is optional and follows the declarative-by-default trust
model from ADR-011.

## 3. Generic measurement families

The exact public names are to be frozen when implementation starts. The target
model should support at least the following semantic families.

### Scalar measurement

One numeric quantity per observation.

Typical uses:

- temperature;
- humidity;
- pressure;
- distance;
- voltage/current;
- radiation count rate;
- magnetic magnitude.

### Vector measurement

A fixed-size vector with declared semantics and unit.

Typical uses:

- acceleration;
- angular velocity;
- magnetic field vector;
- force vector.

### Event measurement

A timestamped occurrence with optional bounded metadata/value.

Typical uses:

- Geiger detector pulse;
- trigger edge;
- contact event;
- discrete sensor detection.

### Field or spatial layer

A derived quantity associated with reconstructed geometry or a spatial domain.

Typical uses:

- temperature mapped onto a surface;
- radiation intensity mapped onto a point cloud or mesh;
- confidence/quality layers;
- material-response maps.

Images, FrameSets, point clouds, meshes and other existing Mantis domain types
remain first-class dedicated types rather than being forced through a generic
measurement wrapper.

## 4. Measurement stream descriptor

A source should announce a bounded descriptor before or with publication. A target
descriptor model contains fields such as:

```text
stream_id
component/device identity
capability_id
data_type + schema version
quantity_id
unit
human-readable label
clock_domain
coordinate_frame (optional)
nominal/update-rate hint (optional)
temporal semantics
spatial semantics
configuration metadata
```

Identifiers are machine contracts. Labels are presentation.

Community-owned identifiers use a namespace owned by that community/vendor, for
example:

```text
io.github.alex.thermalscanner.radiation
com.acme.spectrometer.spectrum
```

They must not masquerade as `org.mantis.*`.

## 5. Observation envelope

A generic observation needs enough context to remain meaningful after recording,
replay and remote transport.

Required or conditionally required concepts are:

```text
source/component identity
stream identity
schema version
device/clock-domain timestamp
value or payload
declared quantity
declared unit
```

Optional but first-class context includes:

```text
measurement_start
measurement_end
integration_time
host receive time
coordinate frame
sensor pose reference
spatial footprint / field of view / sensitivity geometry
quality flags
uncertainty
calibration reference
provenance
sequence/event identity
```

The temporal interval matters for sensors whose result represents integration over
time rather than an instantaneous sample.

## 6. Unknown-schema behavior

Mantis must distinguish three cases.

### Known generic family

Studio and the runtime understand enough semantics to offer baseline behavior.

Example: a valid scalar measurement with a declared unit.

### Unknown specialized schema with a valid contract

Mantis can preserve, hash, record, replay and forward the packet even if it cannot
interpret the payload semantically. Studio can show schema/source/provenance and
bounded metadata through a generic inspector.

### Invalid or unsupported contract

Validation fails explicitly with diagnostics. Mantis must not silently reinterpret
or truncate malformed data.

This rule prevents ecosystem lock-in while preserving correctness.

## 7. Generic Studio behavior

The first useful sensor integration must not require a Studio plugin.

For a generic scalar stream Studio should be able to provide:

```text
Sensor / stream name
current value + unit
valid/degraded/stale state
basic min/mean/max summary
basic time history
source/component identity
timestamp/integration window
quality/uncertainty when available
```

A device with an unfamiliar capability can therefore still be useful immediately.

Studio may derive view names from descriptors, but it must not invent measurement
semantics from labels.

## 8. Generic spatial scalar layers

A large class of exotic sensors becomes valuable when measurements can be mapped
onto scan geometry. This should be a platform feature rather than a radiation-only
feature.

A future typed spatial scalar layer should reference:

- the geometry/artifact or spatial domain it annotates;
- quantity and unit;
- scalar values;
- association method or provenance;
- coverage/validity;
- uncertainty/confidence when available;
- exact input artifacts, transforms, calibration and algorithm version.

Studio can expose compatible layers as generic view modes:

```text
RGB
Normals
Confidence
Coverage
Temperature
Radiation
Magnetic field magnitude
...
```

The sensor author may provide declarative presentation hints such as a preferred
label or color-map family, but semantic data must remain independent of a color
choice.

A specialized Studio extension can add richer domain controls later.

## 9. Reference scenario: DIY scanner with Geiger counter

Consider a Raspberry Pi 5 scanner containing:

```text
2x Raspberry camera
1x line laser
1x radiation detector / Geiger counter
```

The device integration might expose:

```text
camera.left
camera.right
laser.line
radiation.detector
```

and publish:

```text
FrameSet
RadiationEvent        (optional raw pulses)
ScalarMeasurement     radiation.count_rate, unit=cps
```

A count-rate sample should carry the actual integration interval. For example:

```text
quantity        = radiation.count_rate
unit            = cps
value           = 37
measurement_start = t0
measurement_end   = t1
frame             = scanner/geiger
quality            = valid
```

Mantis must not pretend that this value is an instantaneous point measurement if
it represents counts accumulated while the scanner moved between t0 and t1.

A downstream processor can combine:

```text
count-rate observations
+ scanner/sensor trajectory
+ transform graph
+ reconstructed geometry
        |
        v
spatial scalar layer: radiation.count_rate
```

Studio can then expose a generic **Radiation** view. A useful default may use a
relative low-to-high color scale (for example violet through red) with an explicit
legend and numeric range. Relative rendering does not change or replace the stored
physical unit.

If the detector only reports counts/CPS/CPM, Mantis must not label the result as
Sv/h or another dose quantity. Dose conversion requires explicit detector
calibration and a valid conversion model.

## 10. Community integration levels

The SDK should optimize for progressive disclosure.

### Level A — publish a sensor

Goal: minutes, not days.

The author declares a generic stream and publishes samples. Mantis provides
recording, replay and generic Studio presentation.

### Level B — spatially associate it

The author supplies a coordinate frame/sensitivity model and uses or implements a
processor that produces a spatial layer.

### Level C — domain-specific processing

The author adds processor/exporter plugins for calibration, filtering, mapping or
specialized analysis.

### Level D — specialized Studio experience

The author adds declarative panels, controls or view metadata. Native UI execution
is not required for ordinary integrations and remains a higher-trust exception.

## 11. Target SDK ergonomics

The eventual C++ wrapper should make the common case compact. The following is
**illustrative target API, not current v0.3 code**:

```cpp
auto radiation = device.add_scalar_stream({
    .id = "radiation",
    .capability = "io.github.alex.radiation.detector",
    .quantity = "radiation.count_rate",
    .unit = "cps",
    .frame = "scanner/geiger",
});

radiation.publish({
    .value = 37.0,
    .measurement_start = t0,
    .measurement_end = t1,
    .quality = measurement_quality::valid,
});
```

The stable C ABI should expose equivalent functionality through a new versioned
queried interface. The C++ SDK should wrap it without exporting C++ ABI objects.

## 12. Plugin packaging

A community package may contain:

```text
DIY Scanner package
|
+-- device integration
|   +-- cameras
|   +-- laser
|   +-- exotic sensor source
|
+-- schema declarations (only when generic families are insufficient)
|
+-- processors (optional)
|   +-- filtering/calibration
|   +-- spatial mapper
|
+-- exporters (optional)
|
+-- Studio declarative extension (optional)
    +-- specialized panel
    +-- view metadata
```

The generic path works without the optional pieces.

## 13. Distributed placement

Sensor ownership follows the same deployment rule as other scanner hardware.

```text
scanner runtime
  acquisition
  hardware control
  timestamping
  source-local aggregation when required
        |
        v
typed observations
        |
        +--> local processing (standalone)
        |
        +--> remote worker/laptop processing
```

USB, Wi-Fi and local IPC must not change the semantic meaning of the observation.

Disconnect/backpressure policy must be explicit and bounded. Sensor data must not
silently disappear merely because a Studio client disconnected.

## 14. Trust, validation and resource limits

Extensibility must not weaken reliability.

The implementation must bound and validate at least:

- descriptor and metadata sizes;
- dimensions/counts;
- string lengths and identifier syntax;
- sample payload sizes;
- declared schema versions;
- units/quantity identifiers;
- event rates and queue/backpressure policy;
- timestamp/interval consistency;
- spatial references;
- malformed or stale plugin output.

Community processors remain subject to plugin isolation policy. Generic Studio
presentation consumes validated data and does not execute arbitrary community QML
or native UI code.

## 15. Implementation plan

This is intentionally split into small development rounds.

### S1 — measurement domain model

Add versioned generic measurement descriptors and packet families in foundation
modules. Define validation, time semantics, source identity, quantity/unit fields
and optional spatial metadata.

### S2 — plugin publication interface

Add a backward-compatible queried C interface for measurement stream discovery and
publication plus ergonomic C++ SDK wrappers. Provide a tiny scalar-sensor example.

### S3 — persistence and replay

Guarantee generic measurements survive artifact storage, deterministic replay,
hashing and protocol/data-plane forwarding without semantic loss.

### S4 — generic Studio sensor inspector

Discover generic streams automatically and provide live numeric/status/history
views without sensor-specific Studio code.

### S5 — spatial scalar layer

Define the typed geometry-association artifact/packet and a generic Studio scalar
overlay/view mode with legend, range and provenance.

### S6 — reference community sensor

Implement a sample/reference plugin using a simulated or inexpensive sensor. A
Geiger/radiation example is particularly valuable because it exercises event
streams, integration windows, spatial association, uncertainty and generic view
modes.

### S7 — developer-experience and hardening

Add templates, examples, malformed-input tests, ABI compatibility fixtures,
record/replay round trips, disconnect/backpressure tests and documentation.

## 16. Acceptance criteria

The first implementation should not be considered complete until all of the
following are demonstrated:

- a plugin-defined generic scalar sensor appears in Studio with no Studio-specific
  code;
- its data records and replays deterministically;
- unknown specialized schemas are preserved or rejected explicitly, never silently
  dropped;
- timestamps and integration windows survive round trips;
- optional coordinate frames survive round trips;
- a processor can produce a spatial scalar layer from recorded observations;
- Studio can display that layer as a generic view mode;
- a plugin compiled against the pre-extension C ABI remains loadable;
- malformed descriptors and oversized data fail with diagnostics;
- plugin or UI extension failure cannot corrupt finalized project artifacts.

## 17. Current v0.3 reality

The current v0.3 branch already provides important prerequisites: immutable typed
packets, namespaced schemas/attributes, explicit timestamps and coordinate frames,
capability-based devices, plugin ABI isolation, Artifact Store/replay and typed DAG
processing.

It does **not** yet provide the generic measurement-source API, generic sensor
Studio inspector, spatial scalar layer contract or community sensor reference
implementation described here. Those are future implementation work.
