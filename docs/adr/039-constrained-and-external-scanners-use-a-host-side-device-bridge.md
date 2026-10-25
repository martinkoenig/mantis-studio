# ADR-039: Constrained and external scanners use a host-side device bridge

Status: Accepted architecture requirement; implementation deferred beyond v0.3

## Context

Mantis must support scanners whose hardware cannot or should not run `mantisd`.

Examples include:

- microcontroller-based DIY scanners;
- ESP32/STM32/RP2040/Arduino-class devices;
- vendor scanners exposing USB, serial, TCP or another device protocol;
- vendor devices accessible only through a proprietary SDK;
- simple sensor heads that stream observations but have no general-purpose OS.

Architecture v1 already requires third-party/custom scanners, capability-based
devices and transport-independent semantics. ADR-038 adds generic observations for
unusual sensors. Neither decision defines how an entire constrained device joins a
Mantis runtime when the device itself cannot host Mantis.

Requiring Linux, a large CPU or `mantisd` on every physical scanner would create
an unnecessary ecosystem barrier and would exclude exactly the low-cost and
third-party integrations Mantis is intended to enable.

## Decision

1. **A physical Mantis-compatible device is not required to run `mantisd`.**
   `mantisd` may run on a host computer and represent external hardware through a
   device bridge or device plugin.
2. Mantis recognizes at least these integration forms:
   - **native runtime device** — Mantis runtime executes on/next to the scanner
     hardware and owns the Mantis capture session;
   - **bridged device** — host-side `mantisd` owns the Mantis session while a
     bridge adapter communicates with external firmware or a vendor API;
   - **imported/result-only device** — Mantis receives completed artifacts when no
     live integration is available.
3. A future **Mantis Device Bridge Protocol** provides a transport-neutral,
   constrained-device-friendly contract for generic bridged devices. It maps
   external device identities, capabilities, streams, commands, status and data
   into the normal Mantis Device Graph and typed data model.
4. The bridge protocol is **not** the Mantis service/control protocol used by
   Studio clients. It is a hardware-facing protocol with different constraints.
   Its wire representation must be implementable with bounded memory and a
   streaming parser on small microcontrollers.
5. USB, USB CDC/serial, TCP over Ethernet/Wi-Fi and other validated transports may
   carry the same bridge semantics. Transport choice must not change the meaning of
   capabilities, observations or timestamps.
6. The bridge contract must include explicit version negotiation, stable device and
   stream identity, sequence/generation information, clock/timestamp semantics,
   bounded framing/chunking, integrity checks, flow control/backpressure, error
   reporting and reconnect/reset behavior. Silent data loss is forbidden.
7. The bridge maps into existing capability and typed-data contracts rather than
   introducing scanner-model conditionals. Studio continues to react to
   capabilities and stream schemas, not product names.
8. Convenience SDKs such as future Arduino/ESP-IDF/C/C++ libraries are wrappers
   around the bridge protocol. They are not separate protocols and do not define
   Mantis semantics independently.
9. The common SDK path should allow high-level operations such as declaring a
   camera, line laser or scalar sensor and publishing frames/measurements without
   requiring the author to implement framing, discovery, reconnect or protocol
   negotiation manually.
10. Vendor-specific integrations may use a dedicated adapter/plugin instead of the
    generic bridge protocol. Untrusted or crash-prone vendor SDKs should normally
    execute out of process according to the existing plugin-isolation policy.
11. Authority remains explicit. In a bridged deployment, host `mantisd` owns the
    Mantis session and artifacts, but device firmware/vendor hardware remains the
    authority for hardware-local safety behavior that Mantis cannot directly
    guarantee. Mantis must not claim laser/safety control it does not possess.
12. Mantis imposes no artificial minimum measurement quality for participation in
    the ecosystem. Low-end devices may expose poor synchronization, calibration or
    uncertainty; those limitations must be measured/reported rather than hidden.
13. The same semantic model must scale from a tiny bridge device to native scanner
    runtimes and distributed industrial deployments. Different hardware classes do
    not receive incompatible Mantis data models.

## Consequences

A low-cost scanner can consist of cameras/sensors plus a small controller that
streams data to a laptop. The laptop runs `mantisd`; a generic bridge adapter
turns the external device into ordinary Mantis devices/capabilities/streams.

A third-party commercial scanner can also be represented without installing
Mantis on its firmware. Depending on accessible vendor APIs, the adapter may expose
raw frames, semantic measurements, live point clouds or only completed results.

The exact bridge wire encoding is deliberately not frozen by this ADR. It must be
designed and tested as a separate protocol before implementation is considered
stable.

See:

- [Device integration and bridge architecture](../architecture/device-integration-and-bridge.md)
- [Mantis Device Bridge Protocol](../protocol/device-bridge.md)
- [ADR-020](020-scanner-functionality-is-represented-through-capabilities.md)
- [ADR-038](038-extensible-sensor-observations-and-generic-studio-fallback.md)
