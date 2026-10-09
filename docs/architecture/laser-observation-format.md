# LaserObservation and local semantic transport formats

L6 format specification, version 1. These formats are independent of the frozen
MANTIS01/02/03, MRUNHDR3, MRAWREC3 and MRUNOUT3 formats. No native structures,
pointers, ABI tables, JSON or variant layouts are serialized.

## Observation envelope

`MLOBS001` (eight ASCII bytes) identifies one `org.mantis.LaserObservation`,
semantic schema 1. All multibyte fields are little-endian.

| Offset | Field |
| --- | --- |
| 0 | eight-byte magic `MLOBS001` |
| 8 | u64 codec version, exactly 1 |
| 16 | u64 body length B |
| 24 | u64 FNV-1a-64 of the exact body |
| 32 | B body bytes |
| 32+B | eight-byte completion magic `MLOBEND1` |
| 40+B | u64 bitwise complement of B |

Maximum complete record length is 128 MiB. Length arithmetic is checked before
slicing or allocation. Both the footer and checksum must validate; truncation,
unsupported versions and trailing bytes are errors. FNV detects accidental
corruption; it does not authenticate a producer.

## Scalar grammar and bounds

Integers, including schema versions, dimensions, sequences, revisions, flags and
counts, occupy u64 on disk. Signed values use signed two's-complement 64-bit
representation. Narrow decoded integers must fit their canonical field type.
Strings are u64 byte length followed by the original text bytes, at most 1024 bytes;
IDs are nonempty and at most 256 bytes. Arrays are u64 count followed by ordered
values, with at most 4096 entries before more specific L1 limits apply. Presence
is one byte: 0 Unknown, 1 Unavailable, 2 Established followed by its value.
Optional fields use one byte: 0 absent, 1 present followed by its value. Booleans
use one byte 0/1. Invalid tags are errors. Established numeric zero is valid.
Doubles are exact IEEE-754 binary64 bits, little-endian. Nonfinite semantic
numbers are rejected. Core float columns use IEEE-754 binary32, little-endian.
Bulk scalar encoding requires a little-endian host; unsupported native byte order
fails explicitly rather than silently rewriting opaque extension bytes.

## Normative body field order

The field catalogs `src/data/laser_observation_fields.inc` and
`src/data/projected_light_fields.inc` are the explicit ordered v1 storage catalogs
for the following grammar. Their order and enum tags are frozen by checked-in
independent fixtures; changing the format requires a new codec version.

1. type (name, schema); ObservationKey (run Presence, producer stream, producer
   generation, observation sequence).
2. LaserObservationContext, in this exact order: source CameraFrameEvidence;
   FrameSet-key Presence; bundle-key Presence; raw-input ContentReference Presence;
   optical coordinate frame (ID, name); preprocessing Presence (nine row-major
   binary64 matrix values, content-reference Presence); requested emitter IDs;
   emitter evidence; emitter pattern identities (emitter, pattern, revision);
   program correlation Presence (ProgramReference, StepInstance); acquisition
   EvidenceKey Presence; trigger keys; clock mappings; producer ImplementationIdentity;
   parameter ContentReference Presence; exact input references; origin Optional;
   producer-completion RuntimeTimestamp Presence; packet-quality flags.
3. disposition Optional; sample count; emitter dictionary; line dictionary
   (emitter, pattern, pattern revision, local line ID); ordered attributes;
   confidence ImplementationIdentity Presence; diagnostic string.

Nested CameraFrameEvidence, calibration/content references, emitter evidence,
program references, timestamps, clock mappings, exposure and sync fields use
**every field** and the exact typed field order of the existing schema-3 semantic
catalog, documented in [RawCapture v3](rawcapture-v3.md). No context is reconstructed.
Source pixel convention is the fixed schema-1 convention: x right, y down,
origin at the center of original source pixel (0,0). There is no implicit transform.

An attribute is: name string; scalar tag byte; shape array of u64; byte-stride
array of u64; unit string; u64 buffer extent; exact buffer bytes. Rank is 1..4;
shape and stride lengths agree. There is no alignment/padding between fields.
Buffer bytes include any explicitly retained stride padding/extension contents.
Mapped decode returns a slice of the original immutable record mapping, without
column conversion or copy. Scalar tags are explicitly 0=u8, 1=i64, 2=f32, 3=f64,
4=u32. Origin tags are 0=real, 1=synthetic, 2=imported. Disposition tags are
0=success, 1=extractor_failed, 2=extractor_unavailable. All shared nested enum tags
are those explicitly listed in the schema-3 storage catalog (not C++ enum values).

L1 validation applies before writing and after reading: at most 1,000,000 samples,
128 attributes, 64 participants where applicable, 128 MiB bulk payload (the total
record bound is tighter), finite core coordinates and confidence, exact required
columns/masks/dictionaries and source/provenance consistency. Unknown future
quality bits and valid namespaced extension attributes survive exactly. N=0 has
no attributes; success, failed and unavailable dispositions remain distinct.

## Local plugin-host envelope

`MSEM0001` uses the same 48-byte envelope, with footer `MSEMEND1`. Its body starts
with a u64 member kind and contains exactly one complete immutable payload:

| Explicit tag | Payload |
| --- | --- |
| 0 | unchanged MANTIS01 flat packet or MANTIS02 FrameSet |
| 1 | unchanged MEVID001 typed AcquisitionEvidence |
| 2 | unchanged MTRIG001 typed TriggerEvent |
| 3 | unchanged MANTIS03 AcquisitionBundle |
| 4 | complete MLOBS001 LaserObservation |

Unknown tags and flat packets masquerading as typed evidence/observations are
rejected. Nesting is only the nesting accepted by the embedded codec. The complete
transport record is bounded by 128 MiB. Pixel/column copies across an isolated
process boundary are explicit file serialization; in-process ABI views share
original BufferViews and retain ownership on accepted callback output.

Writing uses bounded checksum/count passes and direct ostream serialization;
there is no whole-record staging vector. L1 validation reads core sample columns.
An observation write additionally reads bulk bytes for checksum, then serialization.
Structural sizing traverses metadata and counts buffer extents without reading
payload bytes. Local envelopes have their own integrity pass; embedded immutable
codec integrity is retained. An observation transport computes its embedded checksum
once and reuses it, for three bulk passes (inner integrity, outer integrity, direct
serialization), without a staging buffer. This extra isolated-transport I/O is measured separately from native
ProcessorV2 calls. Mapped decode verifies checksums and core values but creates
no application-level bulk copy. Filesystem/page-cache I/O is not a zero-copy claim.

## Artifact identity and recovery

`org.mantis.LaserObservation`, artifact schema 1, contains exactly one
`0.observation` object. Store publishes a fully encoded/validated `.part`, flushes
and fsyncs it, persists the existing chunk journal, renames/fsyncs the object,
commits its hash/size/index and removes the journal. Recovery checks the journal's
byte count against the complete object. Finalization validates the
object codec and chunk hash. The existing aggregate of ordered chunk-hash text
covers the complete observation, including context and all buffers. Exact context
input IDs are also retained in ordinary artifact provenance. Known project input
references must match finalized persisted type/schema/hash. No database or manifest
version changes occur.

A durable complete journal may recover a verified unindexed object. An incomplete
unjournaled provisional object cannot become FINALIZED. Complete corrupt objects
are refused; diagnostics/provisional files remain available. Packet/RawCapture
APIs explicitly reject observation artifacts. Mapped returned columns own their
mapping independently of Store lifetime.
