# Project storage and recovery

The project root contains `manifest.json`, `project.sqlite`, `objects/`, `capture/`, `journal/`, `cache/`, `diagnostics.log` and a process-held `runtime.lock`.

`capture/` is reserved for future capture indexing; the v0.1 raw chunks are ordinary objects. SQLite contains metadata and chunk indexes, not image or point-cloud blobs. The lock prevents two runtimes from opening the same project for writing.

## Append protocol

1. Write the next packet to `objects/ID/N.packet.part` and fsync it.
2. Write a journal intent containing artifact ID, chunk index, relative filename, checksum and byte count; fsync and atomically rename the intent; fsync the journal directory.
3. Atomically rename the packet to `N.packet`; fsync the object directory.
4. In a SQLite transaction, register the chunk and update artifact byte/chunk counts. SQLite uses WAL and `synchronous=FULL`.
5. Remove the journal intent and sync its directory.

Finalization changes OPEN to FINALIZING, verifies contiguous indexed chunks and their checksums, then publishes FINALIZED with an aggregate checksum. The store rejects append/finalize mutation of finalized artifacts. A processing change creates another artifact.

At startup, journal replay can finish a completely written, checksum-valid pending chunk. Invalid trailing work is not added to the index. Remaining OPEN/FINALIZING metadata is marked RECOVERABLE. Explicit recovery validates the committed prefix before finalizing it. Invalid journal metadata is retained with `.invalid` for diagnostics. Unjournaled `.part` files are ignored and may be removed during a future maintenance pass.

The checksummed packet encoding is versioned (`MANTIS01`), little endian, length bounded, and records type/schema, time/calibration/frame references, metadata and extensible attribute shapes/strides/units. Payloads are 64-byte aligned in the file and mapped read-only by consumers.

## Limits

Tests cover interrupted capture, a corrupt trailing partial chunk, store locking, immutable artifacts, and daemon kill/restart. They do not constitute power-loss certification on every filesystem or device. Windows directory durability and removable media require their own validation. FNV-1a-64 detects accidental content changes and is not tamper protection.

Project format and metadata schema are version 1. Newer versions are rejected. The initial schema creation is the only migration currently required; future upgrades must be sequential and transactional, with fixtures before they are accepted. Large immutable objects must not be rewritten just to update metadata indexes.

## RawCapture schema 2 (v0.2)

Schema 1 above remains readable and unchanged. Schema 2 applies only to explicit
FrameSet captures. `objects/ID/N.segment` stores many records; the current segment
is `N.segment.part`. SQLite chunks index segments, so `chunks` is segment count,
not FrameSet count. No SQLite transaction or directory mutation occurs per frame.
The default segment threshold is 64 MiB (a last record may exceed the threshold,
with a 128 MiB record bound). Tests use smaller thresholds. No entire-capture
pixel/index buffer is allocated.

A record is little-endian:

| Field | Encoding |
| --- | --- |
| magic | 8 bytes `MRAWREC2` |
| payload length | uint64, at most 128 MiB |
| payload checksum | uint64 FNV-1a-64 |
| complemented length | uint64 bitwise complement |
| payload | MANTIS02 parent and MANTIS01 image children |
| completion footer | uint64 length XOR `0x4d414e5449533032` |

Payload header/attributes use the existing explicit encoding. Image children
retain independent clock, receive time, calibration, sync, identity, role,
fourcc, stride and raw bytes. The record checksum includes metadata and padding.
Probe serialization counts/checksums existing spans without pixel staging; actual
serialization writes those spans sequentially. This adds an integrity read pass,
not another acquisition memcpy. Segment finalization also hashes the segment.

Each complete record is flushed to the OS, without fsync. At a segment boundary,
close/sync, rename, sync directory, then commit the existing SQLite chunk row.
Stop seals the last segment and validates order/integrity before FINALIZED.
A failed capture is abandoned to RECOVERABLE. Restart classifies OPEN/FINALIZING
captures as RECOVERABLE. Recovery discovers closed unindexed segments and scans
the provisional tail. It discards only incomplete trailing bytes; corruption
fails explicitly and leaves the verified prefix available for diagnosis.

Process-kill recovery includes complete OS-visible records in the active
segment. Power-loss guarantees cover synchronized segments; the active OS/device
cache cannot be guaranteed durable. The index can be rebuilt from segments.
Indexed checksums must agree, and gaps/conflicting files are rejected.

Capture metadata is in existing artifact provenance: UUID/capture ID, start time,
plugin/version, full profile, logical device ID, physical identities and modes,
storage parameters. Clock/calibration/sync metadata is preserved in each record
and in the profile. An absent calibration/control stays unavailable.

The first FrameSet initializes capture-level `initial_observations`: actual
image metadata, clock domains, calibration references, shapes and strides. This
is one capture-initialization metadata update, never a recurring frame
transaction. Later driver/control changes remain explicit in each observation.
Component descriptors and the loaded producer version are recorded generically;
storage does not require two cameras or LEFT/RIGHT metadata keys.

The generic CaptureReader and RecordedSource return the same immutable Published
FrameSet values as acquisition. Replay mappings retain ownership in returned
packets. Sequential iteration holds one segment index at a time. Real-time
replay follows recorded host-arrival deltas, preserving original timestamps;
ASAP replay changes delivery speed only.

Finalization and explicit asynchronous recovery run as daemon jobs. The client
can continue polling control snapshots while immutable segments are validated;
only metadata updates briefly hold the SQLite mutex. Cancellation checks between
segments/records leave the artifact RECOVERABLE and retryable. CLI/SDK helpers
wait for completion; a disconnected client does not cancel these operations.
