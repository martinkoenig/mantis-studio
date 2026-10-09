# LaserObservation processing boundary

L6 is implemented, acceptance pending. L0–L5 are FINAL ACCEPTED. L7–L8 remain
planned. This package provides processing/storage infrastructure and deterministic
contract producers. It implements no production laser extraction, laser geometry,
XYZ, triangulation, hardware controller, UI or remote streaming/security.

## Runtime and processor selection

The unchanged optional `org.mantis.processor.v2` table receives full borrowed
`MantisSemanticPacketV1` views and a finite timeout. `describe_semantic_node` checks
table size/version/callbacks, bounded descriptor strings, exact schema versions,
determinism, backend and algorithm identity/version. `process_semantic` accepts
one independently validated immutable output, retaining its own complete header
and context. Success with zero or multiple emits, failure after an emit, malformed
fields and wrong output schemas publish nothing and report structured failure.
Both native and isolated nodes enforce the registered descriptor even if a plugin
changes its advertised output type during a later callback.

`data::SemanticPacket` is an internal variant of the existing canonical models,
not a second semantic schema. `plugins::semantic::Packet` aliases it. ImageFrame,
FrameSet, AcquisitionEvidence, TriggerEvent, AcquisitionBundle and LaserObservation
remain distinguishable. The existing semantic ABI adapter checks presence,
metadata budgets, nested identities and buffers, then invokes canonical L1
validation. It retains output buffers before the borrowed callback returns.

Registry discovery records V1 and V2 descriptors separately. `Registry::node`
selects V1 explicitly; `Registry::semantic_node` selects V2 explicitly. A V2-only
plugin is discoverable without advertising V1. A V1-only plugin keeps its previous
behavior. `semantic_bridge` is an explicit flat-only adapter for a legacy node in
a semantic DAG; composite/observation ports and values are rejected. Backend
planning uses the existing compute planner. No automatic first-image selection or
input-header overwrite occurs on the V2 route. The frozen descriptor supports one
input and one output; arbitrary parameter dispatch is not advertised.

## Isolation and ownership

Unapproved manifests continue to execute through the actual `mantis-plugin-host`.
Its additive `process-semantic` operation reads/writes the typed local format in
[the format specification](laser-observation-format.md). Each invocation owns a
unique scratch directory. The host writes a checked temporary result and renames
it only after a successful call. The parent validates it independently and removes
the directory on success, failure, crash, timeout or cancellation. A crashing or
noncompliant plugin does not publish an artifact or terminate unrelated sessions.
The existing process supervisor kills/reaps cancelled or overdue hosts. Parent
lifetime includes the finite requested timeout rounded to seconds plus one second
for process startup/transport. This is local crash containment, not a security sandbox.

Explicitly approved native processors may run in process. Input views share
BufferViews without copying pixels. Output metadata is decoded into owned values;
accepted output buffers retain the host handle. Each retained native buffer also
pins the loaded library, with backing storage released before library unload.
This includes backing deleters compiled into a producing DSO. Output buffers remain
valid after callback/view/adapter/Loaded destruction. Native trusted callbacks must
honor their frozen finite deadline; a violated deadline is diagnosed after return.
Untrusted/noncompliant code requires isolation, where the parent can enforce a hard
lifetime. Native cancellation is checked before/after the bounded callback.

Semantic factories and instances retain a shared execution context (mutex, host
and scratch paths, shutdown token) and their Entry, never a borrowed Registry
pointer. Registry teardown latches shutdown: retained factories fail closed,
pending isolated hosts are killed/reaped, and a bounded trusted call can finish
with its context/library still alive but cannot publish after shutdown. The owner
token is independent of the caller's cancellation token. Output BufferViews keep
their separate library pins until released; no context/Entry ownership cycle exists.

Isolated V2 invocations share the V1 isolation failure policy. Crash, deadline
violation, callback failure and invalid output mark only that plugin FAILED, with
the process result and bounded host diagnostic visible through `statuses()`.
Retained instances and new factories refuse failed entries until explicit
re-enable. Caller cancellation and invalid caller input do not quarantine plugins.

Local isolated transport serializes bulk bytes explicitly across processes.
Mapped decode performs integrity/core validation reads and returns shared mapping
slices without an application-level column/pixel copy. It does not claim that disk
I/O or page-cache loading has no copies. Unsupported host mappings fail explicitly.

## Typed DAG and streaming

`SemanticNodeInstance`, `SemanticGraph` and `SemanticExecutionPlan` are additive;
legacy Packet pipelines remain unchanged. The semantic route reuses the existing
compiler's port/schema checks, single producer rules, backend selection, cycle
rejection and deterministic topological order. The bounded reference plan has at
most 256 nodes, 1024 edges and 64 values per declared connection. Edges are lossless.
One complete publication is in flight, carrying shared immutable references.

Per-node remaining-consumer counts release an intermediate immediately after its
last consumer completes. Fan-out keeps it until every dependent has consumed it;
all terminal outputs remain owned by ExecutionResult. Admission charges the full
backing extent of distinct Storage identities, including simultaneous consumed
inputs and the candidate output, using checked uint64 arithmetic. The explicit
per-publication budget defaults to 256 MiB and must be finite, at most 2 GiB.
Exhaustion fails explicitly before output publication. ExecutionResult reports
the retained-payload high water. Slices charge their backing allocation/mapping;
native lifetime wrappers preserve its extent and may conservatively charge aliased
wrappers separately. Accounting reads sizes/identities, never bulk bytes or copies.
This bounds engine-retained bulk payload, not opaque plugin-private allocations,
caller-held results or operating-system page cache. Metadata and graph sizes retain
their existing finite structural limits; stream queues have separate finite capacities.

`execute_stream` uses finite blocking/lossless input and output queues and persistent
node instances. It waits on condition variables, checks cancellation, propagates
node failures and closes both queues on exit to wake blocked producers/consumers.
A failed invocation sends no result. Cancellation is an explicit failed/cancelled
operation; it does not claim the queued prefix was all processed. Successful source
closure drains all queued publications in order. The route is downstream processing;
no queue is inserted in L3→L4 authoritative acquisition recording.

The observation consumer validates core columns, masks, dictionaries and provenance,
counts resolved/unresolved emitter/line attribution, respects disposition, and
forwards the **same** immutable observation including every unknown extension.
It produces no geometric coordinates and assigns no confidence interpretation.

## Synthetic contract producer

The test-only native DSO exercises the actual frozen ProcessorV2 interface in native
and isolated modes. Its explicit input is an immutable MLOBS001 fixture selected by
`MANTIS_SYNTHETIC_OBSERVATION_FIXTURE`. That fixture supplies producer identity,
configuration/parameter/content references, fixed completion time, source-camera
selection, synthetic coordinates, dictionaries and optional confidence. It is
intentionally not a general production plugin configuration mechanism.

The producer selects the matching camera/component and stream in the full bundle;
missing selection fails. Multiple cameras are supported through explicit selection,
never first-child fallback. It copies the exact source frame, exposure/sync/
calibration, bundle/evidence/program/step/trigger/clock correlation and selected
source's emitter evidence from the input. It does not derive optical attribution
from commands, image brightness or row ordering. Fixture-known dictionaries are
synthetic knowledge. Original inputs and all their pixels/evidence remain unchanged.
The producer output has origin synthetic. Fictional hardware evidence in test input
is preserved as input provenance; no real optical/safety/timing acceptance is claimed.

Successful N>0 and N=0, extractor_failed N=0 and extractor_unavailable N=0 remain
distinct. N=0 has no attributes. Unknown/unavailable context stays distinct;
unknown attribution uses paired 0 masks/finite zero placeholders and required flags.
Confidence absence stays absent; present values carry fixture-defined interpretation.
Fractional original image coordinates are representable without a subpixel algorithm.

## Storage, lineage and replay

The new MLOBS001 codec preserves the complete schema-1 context and extension buffers.
Its explicit field/enum catalog and independent golden fixtures are documented in
[the exact format](laser-observation-format.md). A distinct observation artifact
contains one immutable observation. Typed Store APIs begin/append/read it, with
existing chunk journaling, fsync/rename/hash/finalization/recovery; packet append and
packet read cannot reinterpret it. No SQLite or project-manifest migration occurs.

Observation append reserves its artifact exclusively under the Store metadata
mutex, then releases that mutex for encoding, fsync, readback, hashing, durable
journal publication and rename. Only provenance and chunk/state metadata commits
hold the global mutex. Same-artifact append/finalize/recover/abandon/provenance
mutation report BUSY during the reservation; other artifacts remain usable.
Finalization/recovery also retain the artifact reservation during verification.
Active append owns the shared Store implementation, including project lock and
SQLite lifetime, across wrapper teardown. No new calls may begin on a destroyed
Store. The complete journal still promises exactly one checked chunk; errors leave
RECOVERABLE and reopening replays the unchanged journal protocol. The optional
observation checkpoint seam runs outside the metadata lock and permits deterministic
concurrency/failure tests; it is unset in normal operation.

Context retains exact raw artifact/hash, bundle/evidence/frame keys, calibration
content references and producer/parameter identity. These input IDs are also added
to artifact provenance. Known project inputs must be finalized and match exact
persisted type/schema/hash. Reprocessing creates a new artifact. No active
calibration lookup occurs and no hardware is opened by replay processing.

The integration path records a finalized RawCapture-3 fixture, reads it with
BundleReplay, processes the original bundle through V2 and the typed consumer,
commits an observation artifact, maps it back and compares canonical bytes. Two
passes and isolated/native execution agree exactly. Tests prove mapped columns
remain valid after readers and Store are destroyed. Calibration and original
RawCapture bytes do not change.

## Validation and performance evidence

`laser-observation-codec` freezes six independently constructed fixtures and checks
all truncated prefixes, every single-byte corruption, footer/version/length errors,
valid-checksum malformed semantic fields, columns, masks, dictionaries, confidence,
nonfinite values, future flags/extensions and successor identity rules.
`processor-v2` loads a C-only contract DSO and tests malformed tables/descriptors,
missing/duplicate/invalid emits, callback failures, borrowed/transferred ownership,
concurrent calls, independent headers, full semantic kinds, actual isolated crash,
timeout/cancellation and explicit V1/V2 mixing. `laser-observation-artifact` covers
roundtrip, corruption, write failure, interrupted journal recovery and process death.
`semantic-pipeline-replay` covers exact double processing, source selection, distinct
N=0 outcomes, bounded 64-publication streaming, cancellation and failure closure.
The full legacy/projected/ABI suite remains required, including sanitizers.

`mantis-observation-benchmark SYNTHETIC_DSO` measures N=0, 2, 4096 and 65536:
serialization throughput, mapped decode, native V2 fixture processing, typed pipeline,
per-call heap allocation counts/bytes, streaming throughput/high water and cancellation.
Its replaceable allocation counters are benchmark-only; fixture bulk allocation
and explicit file serialization are separate from mapped output buffer sharing.
Measurements are local Debug/CPU fixtures, not scanner/Q6A throughput promises.
Per-sample error strings were found by the allocation benchmark and eliminated by
constructing diagnostic strings only on validator failure, without changing rules.
See the accompanying validation record for measured results and CI acceptance.
