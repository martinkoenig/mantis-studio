# Projected-light daemon sequencer (L3)

Status: **L3 implemented, acceptance pending**. L0, L1 and L2 are **FINAL
ACCEPTED**; L4–L8 remain planned. This runtime proves software supervision with
synthetic executors. It provides no physical laser, settling, exposure-skew or
trigger-delivery acceptance.

`device::ProjectedRun` in `mantis-device-runtime` owns one
`device::ProjectedExecutor`, an immutable program and graph snapshot, finite
configuration, and distinct run/execution-generation identities. It never opens
camera-only acquisition, calls C tables directly, or writes emitters/triggers.
The executor receives the whole program through validate/prepare/start and owns
its resources and physical timing. The frozen L2 headers and fixtures are unchanged.

## Lifecycle and identities

Construction exposes VALIDATING. `prepare()` runs canonical validation, generic
capability/bound checks and reservation, then executor validation/preparation;
success reaches READY. Preparation failure reaches FAILED directly, without start
or abort/ON work, and attempts to release the opened resources through stop/close.
`start()` is admitted once from READY; executor success reaches RUNNING. Completion,
normal stop, cancellation and runtime faults pass through STOPPING before terminal
COMPLETED, CANCELLED or FAILED. The bounded transition history is diagnostic.
Original structured plugin validation and preparation results are retained in the
run snapshot.
Repeated lifecycle submissions are rejected; stop/cancel are idempotent. There is
no retry, restart or re-arm. Cancellation can supersede a normal stop while cleanup
is pending; any fault supersedes either. A terminal instance cannot restart.

The default identity factory calls `Id::random()` separately for RunId and execution
GenerationId. An injected factory must return fresh nonempty identities for every
new run. Controller, sensor stream, clock and native-trigger generations remain
separate typed domains; the daemon never manufactures their values.

## Reservation, output and deadlines

Preflight checks every declared run bound against the graph, including expanded
steps, cameras, per-step duration, command/event/byte/in-flight limits and all
selected modes/participants. Obvious inaccessible evidence methods/scopes and
trigger endpoints are rejected before calling the executor. Device-specific
combinations still require executor validation and preparation. Advertisement is
availability, not physical proof.

Configuration declares positive finite operation, abort, cleanup and publication
bounds (at most 60 seconds), a positive queue capacity no larger than the graph's
pending-bundle limit, and a finite correlation-entry reservation. Checked canonical
step expansion precedes allocation. A two-bit-per-instance table uses at most
250,000 bytes for one million steps. Correlation tables and queue slots are reserved
before start. Step evidence must fit the correlation-entry reservation. Smaller
configured reservations intentionally constrain the accepted run.

The authoritative output is a preallocated lossless ring of shared immutable
AcquisitionBundles. It has no drop/latest policy. Images remain shared BufferViews;
no payload copy is introduced. `next(timeout_ms)` drains publications, also after
terminal cleanup, and has a finite consumer deadline (zero polls). Queue diagnostics
report capacity, occupancy, high water, accepted/produced publications, consumed
publications and saturation failures. An unqueued or rejected publication is retained
in the terminal summary, with its initiating fault. Full-queue publication waits have
a finite bound and wake immediately on stop/fault; saturation aborts and fails the
run. No consumer progress is required for abort or cleanup.

In-memory semantic admission charges bound retained correlation/publication metadata
conservatively, including shared-snapshot bookkeeping, together with cumulative
published attribute payload bytes, against `max_bytes`. These are memory/resource
charges, **not serialized record sizes**. Entry, event, command and pending-evidence
exhaustion also fail explicitly. Persistence/record sizing remains L4.

The run deadline starts at successful executor start and uses monotonic time.
Ordinary calls use the minimum of remaining run time, advertised call timeout and
configured operation bound; a fractional millisecond permits only a zero-time poll.
NOT_READY never extends the deadline. A separate supervisor can initiate timeout
abort while the ordinary worker is blocked. Stop/close also receive the remaining
run bound where a run started. Priority abort retains its own finite cleanup bound
so expiration cannot prevent initiating OFF/inhibit. Successful completion after
the run deadline fails. Required evidence waits use a finite window of the step's advertised program
`max_duration` from its first authoritative evidence, capped by the run deadline;
late proof after this window fails even if the supervisor has not yet run. Executor
physical step/settle/continuous-ON constraints remain executor responsibilities.
There are no userspace sleeps claiming physical timing. A short condition-variable
wait only paces NOT_READY polling.

Tests inject a thread-safe monotonic clock; its owner calls
`notify_clock_advanced()` to wake deadline/queue waits. The clock callback must be
nonblocking and nondecreasing. Production uses `steady_clock`.

## Evidence and causality

The runtime reuses L1 canonical and successor validation. It relies on the accepted
executor boundary for exact prepared ProgramReference, graph/participant/stream/role/
dimension binding, step membership and abort emitter completeness. No C ABI decoder
or replacement program model is introduced.

Declared step indices are mapped explicitly to dense slots; vector position is
never a semantic step index. Coverage requires control-only evidence for no-capture
steps and captured evidence for the exact requested camera set otherwise. Late
proof for an older slot is allowed and never counts as another execution. Standalone
late command/acknowledgement facts may resolve the remembered capture without
republishing frames. Startup frames with unknown step context establish no execution
coverage; later explicit context is correlated by their exact immutable source keys.
A second capture key for the same slot/camera is contradictory. Missing coverage or unresolved
requirements at completed terminal evidence fails the run.

Commanded-only requires an established matching emitter command. Controller-
acknowledged requires that command and matching successful acknowledgement at the
required scope. Acceptance and completion remain the actual reported distinct
stages; either successful stage can satisfy this evidence class, without inventing
completion. Exposure-effective requires established matching state at the required
scope for every requested source frame and emitter. L1 checks exact frame identity
and complete exposure interval coverage. Unknown/Unavailable cannot satisfy a
required established value, and no weaker class is substituted. Established
command/observed/effective contradictions fail without modifying publications.
Later facts refine bounded internal correlation summaries; original bundles remain
immutable, including their reported presence and acknowledgement stage.

Evidence predecessor ordinals must resolve to actual earlier evidence from this
run, not merely lower counters or nearby timestamps. Trigger request/controller/
endpoints/run/step are checked against intent, with requested-event accounting for
each hardware-capture slot. Free-running capture needs no trigger and cannot invent
one. Typed trigger/frame associations may resolve late, but must resolve to the
same step and exact source identity. Unresolved trigger/frame/request references
fail successful completion. Successful completion acknowledgements or actual typed
observations resolve pending request outcomes scoped to their target and step
instance; request/acceptance alone does not prove delivery. Contradictory reused source/command identities and generation/reset
or event-sequence reuse are rejected across intervening publications as well as
adjacent ones. Causal order never comes from sorting cross-domain timestamps.

## Priority cleanup and terminal summary

One cleanup sequence latches reason/error and STOPPING before calling `abort()`.
Abort is called outside the state/queue mutex and independently of the ordinary
worker. A stop/cancel/fault caller or deadline supervisor can therefore inhibit/
request OFF while `next()` is pending or publication is blocked. Repeated requests
cannot issue a second abort. Cleanup waits for the finite abort result, then the
ordinary worker calls stop and close after its in-flight ordinary call returns.
It refuses close while a side callback remains active. Resource ownership and
callback lifetimes retain the accepted L2 quiescence contract.

A typed terminal snapshot separately retains initiating reason/error, full
AbortOutcome, abort error, stop error, close error, executor terminal bundle and
unqueued/faulting bundle. Fault/cleanup failure takes precedence over cancellation,
which takes precedence over natural completion/normal stop. Cleanup never overwrites
the initiating fault. User stop remains a nonfault termination reason. Completed
executor evidence cannot bypass coverage checks or the common inhibit/OFF cleanup.
No terminal AcquisitionBundle is fabricated by the daemon.

An executor must honor its L2 synchronous finite-call contract. In-process C++ code
cannot forcibly preempt a plugin that violates it; ordinary worker destruction joins
its calls, and L2 retains/quarantines refused destruction ownership. Callers must
finish concurrent public method invocations before destroying the run object.
Unknown/Unavailable command/readback/effective AbortOutcome fields survive unchanged.
Established false inhibit/fence/OFF-request outcomes are cleanup faults; Unknown
and Unavailable remain explicit. Software OFF request success never becomes
observed or optical OFF evidence.

## Validation boundary

`tests/unit/projected_run.cpp` contains a scripted generic executor, fake clock,
condition-variable gates and finite lifecycle, evidence, causal, resource and abort
checks. It includes blocked-next cancellation, full-queue abort, destructor quiescence,
fault/cancellation races and distinct cleanup errors. The CTest target runs in the
existing x86_64/ARM64 and sanitizer matrices. CMake also verifies that device-runtime
cannot reach Qt, protobuf or services. Camera-only code and frozen L2/legacy fixtures remain regression gates. Recording/replay, service/protobuf/UI controls,
processor-v2 integration and all production hardware/extraction/triangulation remain
outside L3.
