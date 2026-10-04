# ADR-022: Sequential RawCapture v2 and generic replay source

Status: Accepted for v0.2 development

The v0.1 per-packet fsync/journal/rename/SQLite path cannot sustain dual high-rate
measurement streams. RawCapture schema 2 uses sequential segments, normally
64 MiB, and indexes segments with the existing SQLite chunk table. Project
manifest and metadata schema remain version 1. Existing RawCapture schema 1
uses its unchanged packet/chunk path. No project migration or pixel rewrite is
needed: only a separately versioned artifact representation is added.

Capture provenance stores generic component descriptors and the actual producer
manifest version. One initial observation metadata update records active modes,
clock domains and calibration references. Repeated FrameSets do not transact
against SQLite until a segment boundary.

Records carry magic, bounded payload length, checksum, complemented length,
canonical FrameSet encoding and a completion footer. Segment boundary sync and
one SQLite transaction publish a segment. A rename-before-index interruption
leaves a discoverable closed segment. An active `.segment.part` is flushed to
the OS for complete-record process-kill recovery. Explicit recovery scans it,
refuses corrupt complete records, and truncates only a structurally incomplete
trailing record. Indexed segment checksum failures are never overwritten.

Power loss can lose the unsynchronized active segment/cache; finalized or
previously synchronized segments are the durability boundary. This is separate
from process-kill recovery. No filesystem/storage-device power-loss certification
is implied. FNV-1a-64 is accidental-corruption detection, not tamper protection.

The integrity pass reads payload once, then serialization writes existing spans
without a packet-sized temporary copy. This cost is measured, not labelled free.
Segment mappings and record indexes are bounded per segment. CaptureReader owns
its Store lifetime; returned packets own their read-only mapping independently.

RecordedSource implements the same ImageStream/Published semantic contract as
live sources. Storage parsing stays in CaptureReader. Replay supports observed
host-arrival pacing and as-fast-as-possible iteration; both retain the recorded
headers, identities and pixels. Algorithms consume FrameSets and never parse
container files. Offline verification validates record/segment checksums and
replays twice, comparing the digest of preserved headers and native bytes.

Rejected: frame files, SQLite pixel blobs, removed durability, unbounded capture
buffers, and product-specific replay in algorithms. DMABUF remains deferred.
