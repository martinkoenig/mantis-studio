# ADR-042: Projected-light device and processing contracts use additive queried interfaces

Status: Accepted v0.4 L0 architecture baseline; concrete C tables planned in L2

## Context

MantisAcquisitionV1 is an accepted FrameSet-only interface on the unchanged ABI-v1
root. MantisProcessorV1 exposes flat packet attributes and the current adapter
replaces output headers with input headers. These contracts cannot carry complete
bundle/timing/evidence semantics by being reinterpreted or modified in place.

## Decision

1. Preserve all existing root/device/acquisition/observation/FrameSet/packet/
   processor C layouts and meanings. Existing v0.1/v0.2 plugins continue to load
   and capture without supporting projected light.
2. Reserve `org.mantis.projected-light.v1` as a new optional queried interface.
   It owns a selected parent instance and all participating resources, reports
   capabilities/limits, validates/prepares a finite program, starts a run, publishes
   bounded full acquisition bundles, exposes structured status and performs
   abort/inhibit/OFF and stop/destroy. It does not rely on an independently opened
   MantisAcquisitionV1 handle. A plugin may expose both; requested mode selects one.
3. Reserve `org.mantis.processor.v2` for full immutable semantic packet processing,
   including bundle/FrameSet input and LaserObservation output. Interface version 2
   does not bump root ABI version 1. L2 specifies the shared semantic packet-view
   responsibility; L6 implements the processing adapter and synthetic producer.
4. Tables use fixed-width C fields, opaque handles, size/version metadata, explicit
   presence, synchronous borrowed descriptor callbacks and explicit buffer retention/
   release. C++ wrappers remain outside the stable binary contract. New functionality
   does not require C++ virtual/STL/Qt/Linux types to cross the ABI.
5. Calls have finite deadlines. The new abort side path is explicitly thread-safe
   while next is pending; inhibit/OFF cannot wait behind a blocked data-plane call.
   Unload/destroy waits for calls/callbacks to finish within documented bounds.
   Emergency hardware action must have independent execution where required.
6. Enumerated capability/control relationships and camera participant selection
   stay generic. New emitter/controller children are excluded from camera calibration
   checks by image participation, never by an X1 name conditional.
7. Missing new interfaces disable that mode only; incompatible/malformed output fails
   explicitly. Preserve exception containment, ownership and plugin-isolation policy.
   Isolated device streaming and full bridge transport are not implemented by L0.

See the [v0.4 plugin strategy](../architecture/v0.4-laser-acquisition.md#11-plugindevice-compatibility-strategy).
No SDK header, table layout or runtime adapter is changed in L0.

## Consequences

L2 can implement/test concrete tables against frozen responsibilities without
breaking old DSOs. Legacy free-running captures expose their actual software evidence,
not fabricated projected-light capability. Query/manifest/version checks and ARM64
compatibility fixtures remain release gates.

## Alternatives considered

Appending required functions to MantisAcquisitionV1, overloading its metadata with
hardware commands, flattening bundle processing through MantisProcessorV1 and exposing
plugin-private X1 controller APIs would break compatibility or semantic boundaries.
