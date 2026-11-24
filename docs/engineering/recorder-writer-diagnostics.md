# Internal recorder writer-cycle diagnostics (v2)

This is an **opt-in software qualification tool**, not product telemetry, a new
transport, a RawCapture schema, or a causal explanation of historical recorder
failures. Issue #12 remains the evidence ledger. Existing queue, timeout, source
pacing, first-error, storage synchronization and recovery contracts are unchanged.
A passing new run characterizes that identified run only.

## Activation and isolation

Linux `BUILD_TESTING=ON` builds isolated copies of the actual Session and Store
in `mantis-recorder-tests` and `mantis-recorder-mantisd`. Only those copies define
`MANTIS_RECORDER_DIAGNOSTICS`. The ordinary libraries, daemon, SDKs and non-testing
target graph do not contain the observer, selector, hooks or strings. In
particular the disabled product path adds no per-frame allocations, clock reads,
queue snapshots, logging, synchronization or image copies. This does not remove
or rewrite any pre-existing diagnostic work.

The isolated copies observe only when `MANTIS_RECORDER_OBSERVE_DIR` names an
already provisioned output directory. Initialization and serialization failures
are reported as UNAVAILABLE/INCOMPLETE without replacing the capture error.
Do not set this variable on a production binary and expect observations: there is
no product selector. The deterministic accounting gate is private test code;
`mantis-recorder-mantisd` never installs that gate.

Run the registered controls after building the normal project:

```sh
ctest --test-dir build/headless -j1 --output-on-failure \
  -R '^recorder-(writer-diagnostics|observed-acceptance|diagnostic-boundaries)$'
```

These focused controls supplement, not replace, full Studio ON/OFF, strict
sanitizer, detector, UI-repetition, boundary and non-testing checks required by
CONTRIBUTING. They do not qualify hardware, another platform, physical timing,
long-running throughput, a quiescent T4 environment or independent acceptance.

## Identity and records

A start marker is written before any capture worker starts. It binds version,
capture ID, source descriptor IDs, PID, process-local observation generation and opening timestamp,
clock domain, diagnostic source/build-input digest and unchanged workload
constants. The generation is an **observer/Session generation**, not an inferred
hardware acquisition generation. Hardware/source generation not supplied by the
existing source contract remains unavailable.

Each record contains a lane-local ordinal, steady-clock nanoseconds, stage,
optional scalar, frame key and Store artifact ID where applicable. The frame key
is `(sequence, device timestamp, received timestamp, device clock ID)`. Source
identity comes from the actual stream at acquisition. The independent decoder
joins writer and Store records to that acquisition within the exact
capture/PID/observation generation. Missing or ambiguous source keys are rejected;
sequence numbers alone are not used as cross-capture identities. Device timestamp
values are never subtracted from process-clock timestamps. Composite packet keys
identify the delivered FrameSet, not independently measured physical exposures.

Three preallocated single-writer lanes retain at most 8192 records each. Producer,
writer and cleanup have separate ownership; no event lock, heap allocation,
formatting, logging or payload retention is performed per record. The header
reports actual record size and reserved record bytes. The process admits at most
eight observed captures in its entire lifetime, bounding both live retention and
output-file count; exceeding that limit reports UNAVAILABLE, never success.
Identity strings are limited to 95 bytes plus terminator; overflow/NUL identity,
record overflow and failed test observer callbacks invalidate the evidence.
Virtual source-identity queries, including their argument evaluation, run inside
the observer exception boundary. A throwing descriptor query leaves acquisition,
Store acknowledgement and replay intact while explicitly invalidating the trace.

Serialization occurs after both worker joins and source cleanup, outside Session
and Store locks. It is bounded by retained records; no live reader races with
writers. A complete closed temporary file is renamed to `.json`. The `.started`
marker remains. Missing final output, a `.part`, missing marker, truncation,
overflow, failed serialization or observer retirement cannot be treated as a
complete trace. There is no background flusher, observer process or unbounded
rolling log. A hard-killed daemon generally leaves only a start marker: its
chronology is **INCOMPLETE**, not reconstructed from a later pass.
Use a fresh task-owned output directory for each invocation. Existing markers,
finals and partial files are not truncated or reused; a collision reports
UNAVAILABLE and leaves prior evidence intact. The opening timestamp is matched
between marker/final output and bounds all event timestamps. This establishes
internal consistency, not authenticity of arbitrarily supplied trace files.

## Covered boundaries and limits

- Source `next` entry/return, delivered source/frame identity and admission
  entry/outcome are matched. Queue occupancy snapshots are observational and may
  change concurrently; they are not claimed as atomic enqueue/pop depths.
  The return explicitly distinguishes a frame, an empty result, a returned error
  and a thrown error. A delivered frame must match its returned key. Admission
  **entry** precedes consumption in the coherent process clock; the outcome read
  may legitimately follow the pop or even completion of its writer cycle.
- An actual nonblocking queue pop distinguishes immediately available work from
  an empty queue followed by the existing blocking pop. Only the isolated
  instrumented path uses this extra probe. Its extra queue lock/perturbation is
  explicit; the product still calls the original blocking pop directly.
- Callback entry/success/failure is separate from payload bookkeeping, accounting
  mutex request/acquisition, first-frame/accounting notification, release of the
  writer's frame reference, cycle completion and the following pop boundary.
  The complete cycle therefore includes work omitted by `recorder_max_append_ns`.
- Store dispatch and mutex acquisition are matched to the callback frame. Schema-1
  packet write/sync, journal publication, transactional chunk commit and successful
  final journal-directory sync are separate stages. Existing schema-2 `RawStage`
  regions are reused on every observed append, including first initialization,
  record write and segment sealing. No second storage profiler is introduced.
- Worker begin/end and actual cleanup begin/end establish ownership boundaries.
  Finalization performed after Session retirement is outside this trace, rather
  than silently attributed to the last callback.

Callback success is not power-loss durability. `store.schema1.durable` is emitted
only after the existing complete schema-1 append path returns from its final
sync. `store.segment.durable` acknowledges the existing synchronized/indexed
segment boundary and reports records in that segment, not one durable frame for
each callback. A current schema-2 active segment has no corresponding durable
acknowledgement merely because callbacks succeeded. Recovery and the verified
recoverable prefix require actual Store reopen/recovery and two real replays;
the decoder explicitly reports them UNAVAILABLE. Physical power-loss certification
remains separate under ADR-019/022.

Version 2 additionally records the actual segment index, configured byte limit,
post-write byte position and record count. The decoder requires seal/durable
stages exactly at the existing byte/record boundary, with the matched index and
record count. An independent qualification oracle scans the **actual Store-indexed
segment files before finalization**, retaining their size, complete record count
and last frame key plus actual Store configuration/capture/artifact identity.
It must agree with the observed acknowledgements; callback totals are not this
oracle. Absent Store evidence is UNAVAILABLE, not zero durable segments. Offline
schema-2 decoding therefore requires that oracle as a second JSON-file argument;
schema-1 needs no segment oracle. A post-Session finalization seal is deliberately
excluded from both the append-cycle trace and this pre-finalization oracle.

The decoder reports COMPLETE_CHRONOLOGY, never capture success or causal closure.
Failed callbacks and rejected admissions are explicit outcomes within otherwise
complete chronology. Every residual wall-time interval is UNKNOWN: timestamps
alone cannot prove scheduler descheduling, ownership by an unrelated actor, Store
causality, a particular system call or a historical initiating mechanism.

## Decisive controls

The real Session regression holds the **actual accounting mutex at the actual
post-callback boundary**. Gate entry/release and matched callback/accounting/cycle
events prove inclusion by ordering, not a sleep or arbitrary duration threshold.
All gates release before Session destruction/joins, including assertion unwind.
The original append-only boundary cannot satisfy the new gate/complete-cycle
oracle. A removed cycle end or mispaired span is rejected rather than normalized.

Real Store fixtures cover schema-1/2 positive recording, bounded queue failure,
writer failure and a transactional SQLite publication failure. Combined observer,
logger and cleanup exceptions must preserve the initiating writer/storage error.
The valid unacknowledged schema-1 journal tail is distinguished from callbacks
and recovered exactly once; fixed pixel oracles and two real readers verify the
result. No callback count is substituted for recovered data.

Standalone decoder mutations deliberately remove, duplicate, reorder or mispair
source, admission, clock, Store and cycle events. Controls also execute actual
observer overflow, abrupt observer exit, failed serialization, unavailable output
and throwing observer callback. Any decoder experiment that clears an invalid
flag is explicitly a hypothetical verifier control, not valid source evidence.

`recorder-observed-acceptance` executes the existing
`tests/integration/acceptance.py` workload in process with the explicitly selected
diagnostic daemon. It preserves every existing assertion, deadline, isolation
fault and real-process failure helper. The wrapper retains complete invocation
output and a receipt binding the workload/helper/decoder, build configuration,
executables, plugins, recipes and current source/diff identity. Natural failures
are retained once, never repeated until favorable. The workload's deliberate
hard daemon kill must remain visibly incomplete even when earlier finalized
sessions have complete matched chronology. Every serialized final trace must
validate; an incomplete sibling cannot be hidden by one successful observation.
The expected Session roster comes independently from actual runtime snapshots,
cross-checked against existing Store capture provenance, not from surviving trace
files. The observer inventory includes markers, finals and partials; an entire
missing marker+final pair is still rejected by the roster. Observer UNAVAILABLE
or INCOMPLETE outcomes are retained before temporary daemon-log removal and
invalidate qualification even when healthy siblings exist. Missing final output
is allowed only for the **exact active capture** at the workload's deliberate owned
SIGKILL, not every Session in that killed process. All owned daemon exits must be
available after retirement.
Wrong-owner, invalid-sibling and missing-without-kill controls exercise this
collection oracle separately from the real workload. Any retained private output
must be screened before public publication.

The controls include impossible returned/delivered keys and admission entry after
consumption, while retaining legal late admission-outcome interleavings. Whole
seal-group omission and mispaired segment identity are rejected against real
Store files. The disposable native omitted-seal observer retains actual Store
flush/sync/commit behavior and must fail its intended diagnostic assertion;
the exact observer restoration must be GREEN. A real two-Session control fails
initialization only for the second observer: both recordings and two replays
remain healthy, but the missing Session observation makes qualification INCOMPLETE.
Native mutation helpers have separate `build` and `test` phases so compilation
does not overlap an exclusive functional test campaign.

Observer-enabled/disabled fixed-workload results are functional equivalence and
bounded retention/perturbation observations, not universal performance claims.
Concurrent permitted LIGHT work is recorded in the campaign handoff. No new
performance acceptance threshold, build-throughput study or queue/deadline
relaxation is introduced.
