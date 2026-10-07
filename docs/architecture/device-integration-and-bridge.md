# Device integration and bridge architecture

Status: Accepted future architecture; implementation deferred beyond v0.3

Normative decision:
[ADR-039](../adr/039-constrained-and-external-scanners-use-a-host-side-device-bridge.md).

Related architecture:

- [ADR-020](../adr/020-scanner-functionality-is-represented-through-capabilities.md)
  — devices are capability based.
- [ADR-038](../adr/038-extensible-sensor-observations-and-generic-studio-fallback.md)
  — unknown sensors publish extensible typed observations.
- [Distributed runtime](distributed-runtime.md) — many scanner/runtime nodes may
  share workers without moving capture ownership into the UI.
- [Device Bridge Protocol](../protocol/device-bridge.md) — future hardware-facing
  protocol requirements.

## 1. Goal

Mantis must support the full practical range of scanner hardware without changing
its semantic architecture:

```text
very small DIY device                           industrial installation
ESP32 / STM32 / RP2040                         many scanner heads
few MB RAM                                     GPU worker pool
slow/limited transport                         high-throughput networking
coarse timing                                  precise clocks/triggers
        |                                               |
        +---------------- Mantis contracts -------------+
```

Measurement quality can differ by orders of magnitude. Participation in the
platform must not depend on the scanner being powerful enough to run Linux or
`mantisd`.

## 2. Integration forms

### 2.1 Native runtime device

Example: Mantis X1/X1 Pro.

```text
scanner
+-- mantisd
+-- device integration
+-- hardware
```

The scanner runtime owns the Mantis capture session directly.

### 2.2 Bridged device

Example: a microcontroller scanner or an external commercial scanner.

```text
external scanner                     host PC
+-- firmware / vendor runtime         +-- Mantis Studio
+-- cameras / emitters / sensors <--> +-- mantisd
                                      +-- bridge adapter/device plugin
```

The external device does not run Mantis. The host bridge translates the external
protocol into normal Mantis devices, capabilities and typed streams.

### 2.3 Result-only/imported device

If no live device protocol is accessible:

```text
vendor software/device
        |
        v
PLY / OBJ / STL / images / other artifact
        |
        v
Mantis importer
```

This is the least integrated form, but it still allows Mantis processing,
inspection, project management and export.

## 3. Integration capability levels

Live integrations should describe what they actually expose rather than pretending
to be equivalent.

| Level | Typical access | Mantis can do |
| --- | --- | --- |
| Native/full | raw frames, timing, controls, sensors | full validated Mantis acquisition path |
| Bridged raw | frames + controls over bridge/vendor API | Mantis-side calibration/processing where supported |
| Bridged semantic | points/poses/observations | downstream processing, fusion, inspection |
| Bridged result | start/stop + completed result | project/processing workflow around vendor scan |
| Import only | exported files | post-processing only |

Capabilities are authoritative. A scanner physically containing a camera or laser
does not imply Mantis can access or control it.

## 4. Host-side bridge boundary

For a generic bridged device:

```text
Device firmware
     |
     | Mantis Device Bridge Protocol
     v
Generic bridge adapter
     |
     | Mantis Device API / typed data
     v
Device Runtime
     |
     v
mantisd
```

The bridge adapter is responsible for translating transport/protocol mechanics,
not for redefining Mantis semantics.

A vendor-specific integration may substitute a vendor adapter:

```text
mantisd
  |
  v
isolated vendor adapter
  |
  v
vendor SDK / proprietary protocol
  |
  v
scanner
```

If the vendor SDK crashes, the fault should be contained according to the plugin
isolation policy.

## 5. Authority model

Native and bridged deployments differ in where authority exists.

### Native runtime

The scanner-local Mantis runtime can own:

- session state;
- hardware coordination;
- timing metadata;
- local buffering;
- Mantis safety orchestration where the hardware contract supports it.

### Bridged runtime

The host `mantisd` owns:

- Mantis session state;
- project/artifact state;
- Mantis processing;
- host-side queues and diagnostics.

The external firmware/vendor device still owns any hardware-local state that cannot
be delegated safely.

For example, a bridge command may request:

```text
laser.enable = true
```

but the firmware must remain free to reject that request because of its own
interlocks. A host disconnect must not leave safety-critical hardware in an
undefined state.

## 6. Reference low-cost scanner

A deliberately extreme community example:

```text
ESP32-CAM left  ----\
                     \
ESP32-CAM right ------> controller ESP32 ---- USB/Wi-Fi ---- laptop
                     /         |
line laser ----------/          +-- laser control
```

The controller might expose:

```text
device: diy-scanner
capabilities:
  camera.left
  camera.right
  emitter.line-laser

streams:
  left-image
  right-image
  status
```

The laptop runs `mantisd` and the generic bridge adapter. Mantis can then bind the
streams into capture/pipeline data just like any other device integration.

This device may have poor rolling-shutter behavior, weak synchronization, low
frame rate and large calibration uncertainty. Those are quality properties, not a
reason to create a separate software architecture.

## 7. Synchronization and quality

Low-cost hardware makes explicit timing especially important.

A bridged source must not imply synchronization merely because two frames have
similar sequence numbers. It should publish the best evidence it has:

- device timestamp and clock domain;
- host receive timestamp;
- hardware-trigger evidence when available;
- software pairing information;
- measured/estimated skew;
- synchronization quality/state;
- drops/reordering/reset generation.

Mantis can then reject, degrade or annotate processing according to algorithm
requirements.

## 8. Bridge data mapping

The bridge should map external concepts into existing/future Mantis types.

Examples:

```text
external camera frame  -> ImageFrame
stereo pair            -> FrameSet or host-side pairing input
temperature            -> ScalarMeasurement
Geiger pulse           -> EventMeasurement
count rate             -> ScalarMeasurement
IMU vector             -> VectorMeasurement
live points            -> PointCloud
button/trigger          -> Event/Input capability
laser control           -> emitter capability + command
```

Unknown specialized payloads use namespaced schemas as defined by ADR-038.

## 9. Progressive developer experience

A developer should be able to choose the lowest-complexity integration that fits
the hardware.

### Path A — high-level microcontroller library

Target experience:

```cpp
#include <Mantis.h>

MantisDevice scanner("MyScanner");
auto left  = scanner.addCamera("left");
auto right = scanner.addCamera("right");
auto laser = scanner.addLineLaser("laser");
auto temp  = scanner.addScalarSensor("temperature", "temperature", "degC");

void setup() {
    scanner.begin(...);
}

void loop() {
    left.publishFrame(...);
    right.publishFrame(...);
    temp.publish(42.7);
}
```

Illustrative only; this API does not exist in v0.3.

The library hides:

- handshake/version negotiation;
- descriptors/capabilities;
- framing/chunking;
- sequence numbers;
- checks/integrity metadata;
- flow control;
- reconnect/reset generation;
- common timestamp plumbing.

### Path B — direct bridge implementation

For developers using Rust, C, bare-metal firmware or another environment, the
bridge specification is the interoperability contract.

### Path C — native Mantis plugin

Use the stable plugin SDK when the device runs on a capable host and maximum
performance or custom integration is needed.

## 10. Libraries are bindings, not standards

Future convenience libraries may include:

```text
Arduino library
ESP-IDF library
portable C library
C++ wrapper
Python simulator/reference tool
Rust/community bindings
```

They all implement the same bridge protocol. A bug or design choice in one library
must not become an undocumented protocol requirement.

## 11. Transport choices

The semantic protocol is transport independent.

Expected useful transports include:

- USB CDC/serial for simple control/low-rate data;
- TCP over Ethernet or Wi-Fi;
- USB bulk or another higher-throughput transport for image-heavy devices;
- local process/pipe transports for simulators/adapters where appropriate.

A transport is selected according to throughput and latency needs. Mantis must not
force camera images through a slow serial link merely because the same protocol can
carry scalar sensors there.

## 12. Backpressure and bounded devices

Microcontrollers cannot absorb unbounded host stalls.

The bridge contract therefore needs explicit bounded behavior:

- finite transmit queues;
- stream priority/QoS;
- flow-control or credit/window semantics;
- explicit dropped/skipped sample accounting;
- lossless modes only where the device can guarantee them;
- latest-only modes where appropriate;
- chunk sizes bounded by negotiated limits.

A device reset or reconnect starts a new generation so stale packets cannot be
mistaken for continuation of the previous session.

## 13. Product-independent Studio behavior

Studio must continue to build controls from capabilities:

```text
if capability(scan.control)       -> show Start/Stop
if capability(camera.exposure)    -> show Exposure
if capability(emitter.control)    -> show emitter controls
if stream(scalar measurement)     -> generic sensor card
if stream(point cloud)            -> geometry preview
```

Avoid:

```text
if scanner == "X1" ...
if scanner == "VendorModel" ...
```

unless deliberately implementing a product-specific optional UX layer.

## 14. Maker-to-industry continuity

The same architecture must remain valid for:

```text
one ESP32 scanner -> one laptop
one vendor scanner -> one workstation
one X1 -> embedded standalone
many scanner heads -> shared worker pool
50+ sessions -> industrial scheduling/monitoring
```

What changes is deployment, throughput, timing quality, resource scheduling and
reliability policy. Device identity, capabilities, typed observations, calibration,
artifacts and pipeline semantics remain compatible.

## 15. Implementation roadmap

This is future work and should be introduced incrementally.

### B1 — bridge semantic model

Freeze device/session/stream descriptors, command/result semantics, reset
generation, timestamps and error vocabulary.

### B2 — framing/reference implementation

Implement a host simulator and a streaming parser with strict memory/size bounds.
Do not start with hardware-specific shortcuts.

### B3 — generic host adapter

Expose bridge devices through the existing Mantis Device API/runtime without
product-specific branches.

### B4 — portable constrained-device library

Provide a small C core suitable for microcontrollers.

### B5 — Arduino/ESP convenience SDK

Wrap the C/protocol layer in high-level device/camera/sensor APIs and examples.

### B6 — reference DIY scanner

Validate with a deliberately low-resource scanner/simulator, including reconnect,
drops, poor timing and limited throughput.

### B7 — vendor-adapter validation

Demonstrate that a commercial external scanner/vendor SDK can map into the same
host-side model without changing Mantis core.

## 16. Acceptance criteria

Before the bridge is considered stable:

- a device that cannot run `mantisd` is discoverable through host `mantisd`;
- the same device works over at least two transports without semantic changes;
- reconnect/reset cannot splice stale data into a new generation;
- flow control is bounded and loss/drop accounting is visible;
- timestamps/clock quality survive into Mantis packets;
- generic sensors from ADR-038 work over the bridge;
- an image stream can be transported without using the Studio control protocol;
- malformed/oversized frames cannot exhaust host memory;
- loss of Studio does not make the external hardware state undefined;
- old native plugins remain compatible;
- no product-name conditionals are required in Mantis core/Studio.

## 17. Current v0.3 reality

The v0.3 tree does **not** implement the Device Bridge Protocol, generic bridge
adapter, microcontroller libraries or vendor bridge described here.

Existing capability/device/plugin/data/time contracts are prerequisites and should
not be narrowed in ways that would prevent this architecture.
