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
