# Control protocol v1 and data plane

> **Separate future hardware protocol:** scanners or microcontrollers that cannot
> run `mantisd` use the planned
> [Mantis Device Bridge Protocol](device-bridge.md), governed by
> [ADR-039](../adr/039-constrained-and-external-scanners-use-a-host-side-device-bridge.md).
> That hardware-facing bridge is intentionally distinct from the client/service
> protocol documented on this page.

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

## v0.2 additive acquisition operations

Device descriptors carry parent/children and generic metadata. `devices_info`,
`captures_list` and `capture_status` expose profiles, camera modes and diagnostics.
Capture snapshots distinguish produced/committed FrameSets, observed raw loss,
writer queue depth/capacity/high water/saturation, preview drops, duration, total
container bytes and payload throughput in decimal MB/s and binary MiB/s.

`preview` consumes a LATEST_ONLY observation and returns a leased immutable local
packet reference, format 2, with explicit `preview_release`. Map before release;
leases expire after 60 seconds and at most eight files can be outstanding. An
empty latest-only branch returns BUSY. Linux mappings remain valid after unlink.
`artifact_data` continues to return finalized format-1 packet references; it
rejects segmented RawCapture, which requires the replay source API.

`replay` returns a daemon job ID and supports observed host-arrival real-time
pacing, ASAP, or verification. Verification status is JSON with counts,
continuous-sequence status, integrity/replay PASS and a digest covering canonical
headers and pixels from two passes. It does not measure exposure skew.

RawCapture v2 stop drains recording and returns `finalization_job_id`; poll until
Completed before replay. `artifact_recover_async` (tag 31) returns a recovery job
ID in `result_id`; wait and query artifacts for the finalized descriptor. Existing
tag 22 retains synchronous v0.1 recovery semantics. SDK/CLI helpers handle these
waits. Interrupted/failed validation remains recoverable, never silently valid.

See [ADR-023](../adr/023-acquisition-preview-leases-and-storage-jobs.md).

## v0.3 calibration operations (M6)

Presence-capable target fields require Protobuf/protoc 3.15 or newer.
Protocol v1 adds Request tags 32–40 and Response fields 13–15, preserving all old
numbers. Calibration target/pattern/measurement requests and compact inspection
are typed; provenance message presence distinguishes absence from present empty.
Dataset/camera/rig commands return normal daemon Job IDs in `result_id`, with one
artifact in `Job.result_artifact`. Current active query uses optional binding
presence and describes future capture only; historical recorded references remain
authoritative. No observations, pixel buffers or residual vectors enter responses.
The 4 MiB frame limit remains; oversized responses produce a structured busy error.
See [Calibration API](../architecture/calibration-api.md) for all DTOs, numeric
ordering, tags, input bounds, examples and cancellation/activation semantics.

## v0.4 projected-light operations (L5)

Request tags 41–47 add explicit projected discovery, pure validation, start,
status/list, generation-fenced stop/cancel and latest bundle reference. Response
fields 16–18 add compact graph/validation/capture DTOs. Protocol root/version,
legacy camera semantics and Request 10–40 / Response 1–15 stay frozen. Typed
inline programs are bounded to 512 KiB; finalized RawCapture-3 headers are the
other exact program source. Start IDs are retryable; the daemon owns hardware,
recording and cleanup. Full evidence/pixels remain in leased MANTIS03 format-3
files, sharing the eight-lease/60-second preview budget. Existing replay dispatches
schema 3 to BundleReplay jobs, including two-pass verification. See the
[projected control contract](../architecture/projected-light-control-api.md) for
exact tags/enums, durability ordering, failures, SDK/CLI and loopback-only limits.
