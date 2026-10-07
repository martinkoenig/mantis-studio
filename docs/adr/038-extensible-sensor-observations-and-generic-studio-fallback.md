# ADR-038: Extensible sensor observations are first-class typed data with generic Studio fallback

Status: Accepted architecture requirement; implementation deferred beyond v0.3

## Context

Mantis is intended to support third-party and community-built scanners, including
devices with sensors that Mantis Studio did not know about when it was shipped.
Examples include IR thermometers, Geiger counters, magnetic-field probes, gas
sensors, spectrometers, ultrasonic sensors, force probes and future sensor types.

ADR-020 already requires scanner functionality to be represented through
versioned namespaced capabilities. A capability descriptor alone is not enough:
the runtime also needs a stable way to transport, persist, replay, process and
present the data produced by that capability.

Requiring every community sensor author to implement Mantis core support, custom
Studio code and a custom processing stack before the first measurement can be
used would create an unnecessary ecosystem barrier.

## Decision

1. **Capabilities, data schemas, processing and presentation are separate
   contracts.** A capability describes what a device can do. A typed data stream
   describes what it emits. Processor plugins describe optional interpretation or
   transformation. Studio presentation may be generic or extended separately.
2. Device and sensor capabilities use stable, versioned, namespaced identifiers.
   Community identifiers must not occupy the `org.mantis.*` namespace.
3. Mantis will provide a small set of generic measurement families for common
   extension cases, including at least scalar, vector and event observations.
   Existing image, point-cloud and other domain packet types remain separate.
4. Generic measurement packets carry explicit source and stream identity, schema
   version, clock-domain timestamp and declared quantity/unit metadata. They may
   additionally carry a measurement interval/integration window, coordinate frame,
   spatial footprint or sensitivity geometry, quality, uncertainty, calibration
   reference and provenance when applicable.
5. Unknown but valid versioned schemas are not silently discarded. The runtime
   must be able to preserve, record, replay and forward them according to their
   declared storage/transport contract even when no specialized algorithm or UI is
   installed.
6. Studio provides **generic fallback presentation** for supported generic
   measurement families. A scalar stream should require no sensor-specific Studio
   code to obtain a live value, unit, status and basic history/plot view.
7. Spatial visualization is also generic where possible. A processor may associate
   observations with reconstructed geometry and produce a typed spatial scalar
   layer. Studio can expose that layer as a view mode without requiring native UI
   code from the sensor author.
8. Specialized processors, exporters and Studio extensions remain optional. They
   are used when a sensor needs domain-specific processing or presentation beyond
   the generic path.
9. Community UI extensibility continues to follow ADR-011: declarative,
   data-driven extensions are preferred; unrestricted native UI code is not the
   default trust model.
10. Plugin packages may bundle a device integration, schema declarations,
    processors, exporters and declarative Studio presentation metadata, but these
    pieces remain independently versioned and optional.
11. Quantities and units must be machine-readable. Mantis must never infer a
    scientifically different quantity from a convenient display label. In
    particular, radiation count rate (for example CPS/CPM) must not be converted to
    dose rate unless a valid detector calibration/conversion model explicitly
    supports that operation.
12. New native plugin functionality is added through versioned queried interfaces
    or equivalent backward-compatible extension mechanisms. The existing stable C
    ABI is not broken merely to add a new sensor family.

## Consequences

A community author can integrate a useful scalar sensor with a small amount of
device-side code and receive recording, replay and baseline Studio presentation
without writing a custom GUI.

Mantis core remains independent of specific exotic sensors while preserving their
typed data and provenance. Domain-specific value is added incrementally through
processors and declarative views rather than by expanding a hard-coded scanner
class hierarchy.

The generic path must validate bounds, schemas, units and metadata. "Unknown" does
not mean "trusted" and does not permit arbitrary native UI execution.

The exact public C/C++ API, quantity/unit registry and spatial-layer packet schema
are implementation work for a later milestone and must receive ABI, replay,
round-trip and malformed-input tests before being frozen.

See
[Extensible sensor platform](../architecture/extensible-sensor-platform.md),
[Custom sensor integration](../plugin-development/custom-sensors.md),
[ADR-011](011-community-ui-extensions-are-declarative-by-default.md) and
[ADR-020](020-scanner-functionality-is-represented-through-capabilities.md).
