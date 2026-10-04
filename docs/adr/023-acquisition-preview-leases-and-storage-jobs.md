# ADR-023: Immutable preview leases and asynchronous storage validation

Status: Accepted for v0.2 development

Dual RAW8 images are about 2 MB per observation. Sending them in control messages
would compete with capture controls and violate the existing data-plane boundary.
The daemon consumes the independent LATEST_ONLY branch and publishes a canonical
packet file under the project cache. A DataReference names that immutable file;
Studio maps it and makes grayscale Qt images only in its client worker.

Preview references have explicit release, a 60-second lease, and a maximum of
eight outstanding files. Unreleased expired files are cleaned on the next preview
request; restart removes orphan previews. Failed deletion retains the lease for
retry and preserves the capacity bound. Linux mappings retain inode ownership
after release/unlink. Clients must map before releasing. This is a local shared
filesystem transport, not a remote transport or a claim of shared-memory zero-copy.
Raw recording never waits for a client to request, map, convert or release preview.

Protocol v1 adds commands/fields without renumbering existing tags. Preview uses
DataReference format 2; finalized v0.1 packet references retain format 1. Segmented
RawCapture is consumed through replay, never advertised as a standalone packet.
Replay and verification are daemon jobs. Stop drains acquisition and schedules
RawCapture v2 finalization; Capture.finalization_job_id exposes completion. Client
SDK stop helpers wait without taking ownership of the job.

Multi-gigabyte recovery also runs as a job through additive
artifact_recover_async (tag 31), returning Response.result_id. The existing
artifact_recover command remains available with its v0.1 synchronous semantics.
CLI and SDK recovery helpers use the asynchronous operation and return the final
artifact after waiting. Timeouts/disconnection do not cancel daemon work. Explicit
job cancellation is checked between bounded segments/records; failed/cancelled
validation leaves RECOVERABLE data. Heavy scans run outside the SQLite metadata
mutex so snapshots and controls can continue.

Rejected: pixels in Protobuf, UI-owned capture, preview on the lossless writer
queue, mutable preview files, unbounded reference retention, and long synchronous
verification on the daemon's control loop. A future shared-memory transport can
reuse these reference semantics without changing acquisition or algorithms.
