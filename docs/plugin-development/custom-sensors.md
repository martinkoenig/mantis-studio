# Integrating custom and exotic sensors

Status: Developer guide for accepted future architecture; APIs shown here are
targets and are not yet available in v0.3.

Normative architecture:
[ADR-038](../adr/038-extensible-sensor-observations-and-generic-studio-fallback.md)
and
[Extensible sensor platform](../architecture/extensible-sensor-platform.md).

## The short version

A community scanner author should normally need to do only three things:

1. announce what the sensor can do;
2. declare the typed stream it produces;
3. publish timestamped observations.

Mantis should then provide storage, replay and a baseline Studio view
automatically.

Custom processors and custom Studio presentation are optional.

## Example scanner

Assume a DIY Raspberry Pi 5 scanner contains:

- two Raspberry cameras;
- one line laser;
- one IR thermometer.

The scanner device plugin still owns the hardware-specific work: camera access,
laser control and talking to the thermometer over I2C/SPI/UART or another local
bus.

The temperature sensor additionally exposes a capability and one typed scalar
measurement stream.

Conceptually:

```text
DIY device plugin
|
+-- left camera  -> ImageFrame
+-- right camera -> ImageFrame
+-- line laser   -> emitter/control capability
+-- IR sensor
    +-- capability: io.github.alex.temperature.ir
    +-- stream: object-temperature
        +-- generic scalar measurement
        +-- quantity: temperature
        +-- unit: degC
```

Studio does not need an `if (device == "AlexScanner")` branch.

## Target fast path

The final SDK should make a simple sensor look approximately like this. This is
pseudocode for design guidance, not a v0.3 API contract:

```cpp
auto temperature = device.add_scalar_stream({
    .id = "object-temperature",
    .capability = "io.github.alex.temperature.ir",
    .quantity = "temperature",
    .unit = "degC",
    .frame = "scanner/ir-sensor",
});

temperature.publish({
    .value = 42.7,
    .timestamp = sensor_time,
    .quality = measurement_quality::valid,
});
```

That should already be enough for:

- discovery;
- live value and unit in Studio;
- timestamped recording;
- replay;
- generic plotting/history;
- access from processors and client SDKs.

No custom Studio code should be required.

## When to use a generic measurement

Use a generic scalar/vector/event family when the sensor meaning fits one of those
shapes without losing important information.

Good scalar examples:

```text
temperature
humidity
pressure
distance
radiation.count_rate
voltage
current
magnetic_field.magnitude
```

Good event examples:

```text
radiation detection pulse
trigger edge
contact detection
limit switch event
```

Use a custom namespaced schema when the payload has domain structure that should
not be flattened merely to fit a generic type, for example a spectrum with
calibrated wavelength bins.

## Namespaces

Do not create new `org.mantis.*` identifiers in a community plugin.

Prefer an identifier you control, for example:

```text
io.github.username.project.temperature.ir
com.company.product.spectrum
```

Schema and capability identifiers are stable machine contracts. A display label is
not an identifier.

## Time semantics

Ask what the value actually represents.

An IR thermometer may provide a near-instantaneous sampled value.

A Geiger counter count rate normally represents events accumulated over an
interval. Publish that interval:

```text
measurement_start = t0
measurement_end   = t1
value             = 37
unit              = cps
```

Do not collapse an integrated measurement into a fake instantaneous point when the
distinction matters.

Use explicit clock domains as required by ADR-016.

## Spatial semantics

If a measurement should later be mapped onto 3D geometry, give the sensor a stable
coordinate frame and calibrate its transform into the scanner/device graph.

Example:

```text
scanner
|
+-- camera/left
+-- camera/right
+-- laser/L1
+-- sensor/ir
```

For directional or area sensors, also describe the relevant field of view,
sensitivity geometry or footprint when the model is known.

This does not force every sensor to be spatial. A battery voltage or enclosure
temperature stream can remain non-spatial.

## Generic Studio fallback

For a supported generic scalar stream, Studio should be able to create a basic
sensor card automatically:

```text
Object temperature
42.7 degC

State: valid
Updated: ...
History: [generic plot]
```

For a specialized unknown schema, Studio should at least expose schema identity,
source/provenance and safe bounded metadata in a generic inspector.

Unknown data must not be silently dropped.

## Spatial overlays

If you want a temperature, radiation or similar map on the reconstructed object,
the preferred architecture is:

```text
sensor observations
+ transforms / trajectory
+ reconstructed geometry
        |
        v
processor
        |
        v
spatial scalar layer
        |
        v
generic Studio view mode
```

This avoids writing a one-off renderer for every scalar sensor.

The generic view can provide range, legend, relative/absolute scaling policy and a
standard color map. A specialized declarative Studio extension can add domain
controls later.

## Radiation / Geiger reference pattern

A Geiger integration is a useful stress test for the platform.

A device plugin could publish raw events:

```text
RadiationEvent
timestamp = ...
detector = scanner/geiger
```

and/or an aggregated generic stream:

```text
quantity          = radiation.count_rate
value             = 37
unit              = cps
measurement_start = t0
measurement_end   = t1
frame             = scanner/geiger
```

A processor maps the time-windowed observations to geometry using the sensor
trajectory and transform graph.

Studio can then expose a generic `Radiation` view alongside modes such as
Coverage. A relative low-to-high visualization can, for example, run from violet
to red.

Keep physical semantics honest: CPS/CPM are count-rate quantities. Do not label
them as dose rate without detector-specific calibration/conversion evidence.

## Optional custom processing

Add a processor plugin when you need logic such as:

- filtering;
- sensor calibration;
- compensation;
- temporal integration;
- event-to-rate conversion;
- spatial mapping;
- fusion with camera/laser observations;
- specialized derived quantities.

The processor consumes typed packets and emits another typed packet/artifact. It
should not reach into Studio internals.

## Optional custom Studio integration

Most sensor authors should not need this.

When a richer UI is valuable, prefer declarative extension points:

- custom labels and descriptions;
- parameter forms;
- sensor dashboard metadata;
- supported view types;
- legends/ranges;
- domain-specific actions routed through public services.

Do not assume that arbitrary community QML or C++ UI code will execute inside the
Studio process. ADR-011 deliberately keeps that a higher-trust exception.

## Packaging model

A complete community package may eventually look like:

```text
my-scanner/
  manifest(s)
  device-plugin
  schemas/                  # only if needed
  processors/               # optional
  exporters/                # optional
  studio-extension/         # optional/declarative
  README
```

A package containing only a device integration and generic stream declarations
must still be useful.

## What Mantis provides vs. what you provide

| Concern | Mantis platform | Sensor/plugin author |
| --- | --- | --- |
| Typed transport | yes | declare stream |
| Buffer lifetime | yes | obey SDK contract |
| Artifact persistence | yes | publish valid data |
| Replay | yes | deterministic semantics |
| Generic scalar UI | target platform feature | metadata only |
| Device bus/protocol | no | yes |
| Sensor calibration model | generic hooks | sensor-specific values/model |
| Domain algorithm | framework | optional plugin |
| Generic spatial overlay | target platform feature | provide/map spatial data |
| Specialized UI | declarative extension host | optional extension |

## Implementation note for v0.3 developers

Do not implement examples in this document by adding sensor-specific branches to
Studio or `mantis-core`.

The current v0.3 tree does not yet contain the generic measurement publication
interface described here. The intended implementation path is listed in
[Extensible sensor platform §15](../architecture/extensible-sensor-platform.md#15-implementation-plan).

Until that work exists, treat the code snippets above as ergonomics targets and
the architecture document/ADR as the source of truth.
