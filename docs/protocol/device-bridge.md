# Mantis Device Bridge Protocol

Status: Future protocol contract; semantic requirements accepted by ADR-039;
exact wire encoding and message numbers are not yet frozen.

This protocol is for **hardware-facing bridged devices**. It is separate from the
current Studio/client control protocol in
[`protocol/protobuf/mantis.proto`](../../protocol/protobuf/mantis.proto).

## 1. Why a separate protocol exists

The normal Mantis client protocol assumes a `mantisd` runtime on the remote side.
A microcontroller or proprietary scanner may have:

- very little RAM;
- no filesystem;
- no process model;
- no Protocol Buffers runtime;
- no ability to host Mantis services;
- highly constrained links.

The bridge must therefore be streamable, bounded and implementable without a
general-purpose OS.

## 2. Layering

Conceptually:

```text
Mantis semantic device model
        |
bridge session / descriptors / streams / commands
        |
bounded framing + integrity + flow control
        |
USB CDC | USB bulk | TCP/Wi-Fi | Ethernet | test transport
```

Transport is replaceable. Semantic identifiers and measurement meaning are not.

## 3. Required protocol concepts

The eventual wire protocol must represent at least:

### Session/negotiation

```text
HELLO
HELLO_ACK
protocol version/range
feature flags
negotiated limits
session/generation identity
```

### Device description

```text
DEVICE_DESCRIPTOR
COMPONENT_DESCRIPTOR
CAPABILITY_DESCRIPTOR
STREAM_DESCRIPTOR
```

Descriptors use stable IDs and namespaced capability/schema identifiers.

### Lifecycle/control

```text
OPEN / START
STOP / CLOSE
CONFIGURE
COMMAND
COMMAND_RESULT
```

Exact command names are not frozen here.

### Data

```text
FRAME / DATA
MEASUREMENT
EVENT
STATUS
DIAGNOSTIC
```

Large payloads must support bounded chunking rather than requiring one contiguous
allocation.

### Reliability/session state

```text
sequence identity
generation/reset identity
acknowledgement/flow-control state
explicit drop counters/reasons
heartbeat/liveness where required
structured errors
```

## 4. Resource negotiation

Peers must negotiate hard bounds such as:

- maximum frame/message/chunk size;
- number of outstanding chunks/credits;
- stream count;
- metadata/string lengths;
- optional compression/encoding features;
- supported clock/timestamp features.

A sender must not assume desktop-scale buffers on the device.

## 5. Time and synchronization

Every time-bearing stream must identify its clock domain or equivalent bridge clock
identity.

Where available, include:

- native device timestamp;
- sequence/event identity;
- host receive timestamp added by the adapter;
- integration window for accumulated measurements;
- sync/trigger evidence;
- reset generation.

Host arrival time must not silently replace a device timestamp.

## 6. Flow control

The exact mechanism remains to be selected, but it must be explicit and bounded.

Possible implementation mechanisms include credits/windows, acknowledged chunks or
per-stream bounded queues.

The semantic outcome must distinguish at least:

- delivered;
- intentionally skipped/latest-only;
- device-side drop;
- transport drop/corruption;
- host rejection/backpressure;
- reset/reconnect discontinuity.

Lossless delivery may only be claimed when the complete path can guarantee it.

## 7. Integrity and corruption

Framing must detect malformed length fields and corrupted/truncated data. Large
payload chunks need enough identity to reject duplication, reordering or mixing
across generations.

Cryptographic artifact integrity remains a higher-level concern; the bridge still
needs transport/frame integrity appropriate to the selected transport.

## 8. Security

A production network transport cannot assume that "same LAN" is an authorization
model.

Exact authentication/encryption is future protocol work. The implementation must
keep identity/authentication extensible and must not bake unauthenticated remote
control into the stable protocol.

USB/local deployments may use a different trust policy from routed network
deployments without changing measurement semantics.

## 9. Mapping examples

### Camera

```text
STREAM_DESCRIPTOR
  type = image
  role = left
  clock = camera-clock

DATA
  stream = left
  sequence = 4817
  timestamp = ...
  payload = ...
```

The exact pixel/compression representation is negotiated/declared rather than
guessed.

### Scalar sensor

```text
STREAM_DESCRIPTOR
  type = org.mantis.measurement.Scalar
  quantity = temperature
  unit = degC

MEASUREMENT
  value = 42.7
  timestamp = ...
```

### Geiger counter

The device may expose both raw events and an integrated count-rate stream. An
integrated value carries its measurement interval; it is not treated as an
instantaneous sample.

## 10. High-level library target

An Arduino/ESP convenience library should let firmware authors think in device
concepts rather than protocol frames.

Illustrative only:

```cpp
MantisDevice dev("DIY Scanner");
auto left = dev.addCamera("left");
auto laser = dev.addLineLaser("laser");
auto radiation =
    dev.addScalarSensor("radiation", "radiation.count_rate", "cps");

dev.begin(...);
left.publishFrame(...);
radiation.publish(37.0);
```

The library handles protocol negotiation, descriptors, framing, sequencing,
chunking, flow control and reconnect.

## 11. What this protocol is not

It is not:

- the Studio UI protocol;
- a replacement for `mantisd`;
- a promise that every device exposes raw camera access;
- a scanner-quality certification;
- a reason to hard-code Arduino/ESP concepts into Mantis core.

## 12. Validation requirements

A future v1 bridge protocol should be tested with:

- a desktop simulator/fuzzer;
- deliberate fragmentation and short reads;
- corrupt/truncated frames;
- slow consumer/backpressure;
- reconnect and device reboot during capture;
- sequence wrap/reset behavior;
- low-memory sender limits;
- multiple simultaneous streams;
- Wi-Fi interruption/reordering where relevant;
- at least one constrained-device implementation;
- at least one high-bandwidth image-stream example.

Until those tests and the exact wire schema are complete, this document defines
architecture constraints rather than a frozen wire format.
