# Control protocol v1 and data plane

[`protocol/protobuf/mantis.proto`](../../protocol/protobuf/mantis.proto) is the source of truth for generated wire messages. The schema is transport independent. Commands cover device discovery, capture start/stop, pipelines, project create/open, artifacts/data references/recovery/export, jobs/cancellation, plugin state and diagnostics/events.

## Local framing

The current transport is IPv4 loopback TCP, default port 47321. Each request uses one connection. A message is a four-byte unsigned big-endian length followed by binary Protobuf. The maximum control message is 4 MiB. Runtime version is explicit; request UUIDs are echoed to prevent response confusion. Missing commands, incompatible protocol versions and invalid tokens produce structured errors.

Every request contains the token from `MANTIS_TOKEN`. This is a local development authorization mechanism; there is no remote TLS service. The daemon serializes service calls and leaves processing/export to its job worker.

All commands are UI independent. Events have a sequence, kind, component and message. Clients can request events after a sequence or a bounded full snapshot. v0.1 retains 512 events in memory; if a client falls behind it must refresh a full snapshot. Text diagnostics are also appended to `diagnostics.log`.

## Data plane

`DataReference` carries artifact identity, transport identifier, format version and locator. It contains no frame/point payload. The current `local-mapped-file` transport resolves a finalized packet file and maps it read-only. It requires a shared local filesystem. Raw images and point arrays are never serialized into ordinary command messages.

Isolated plugin input/output likewise uses files on the data plane, with paths passed to the host. The portable semantic format and buffer abstraction leave room for shared memory handles, external device memory and remote chunk transport.

## Compatibility policy

Add optional fields without reusing tags; reserve removed names and numbers. Preserve unknown fields by using binary Protobuf round-trips. Unknown commands must fail rather than default to an unrelated operation. Algorithm determinism is checked using canonical payload bytes, not a claim that arbitrary Protobuf serialization is canonical.

References: [Proto3 guide](https://protobuf.dev/programming-guides/proto3/), [serialization is not canonical](https://protobuf.dev/programming-guides/serialization-not-canonical/).
