# ADR-026: Bounded software correspondence for independent camera streams

Status: Accepted for v0.2 development; revised after real Q6A cold-start evidence

## Evidence

Equal independently started native counters had ~51.8 ms timestamp separation.
The timestamp-nearest implementation subsequently passed real 4 ms starts:
140 paired FrameSets at -2.861 ms, and the official smoke harness at +1.686 ms,
native offset +7, startup unmatched 7/0, 123/123 FrameSets, no pairing/raw failures,
FINALIZED and deterministic replay/integrity PASS. Full validation then passed
Debug and Release 17/17 and disabled-link discovery, reached plugin-owned cold
acquisition, and failed with `Camera timestamp delta exceeds profile pairing limit`.
These favorable starts do not robustly validate 4 ms for arbitrary free-running phase.

At measured ~119.27 FPS, T = 1/FPS ≈ 8.384 ms. Two healthy independent periodic
streams can have nearest timestamp distance T/2 ≈ 4.192 ms. Independent oscillators
also drift across the nearest-neighbor boundary, requiring an occasional unmatched
observation to maintain one-to-one correspondence. The former prohibition on any
steady-state exclusion wrongly treated healthy phase drift as acquisition loss.
Neither changing startup phase nor retrying a failed start is an acceptable fix.

## Software tolerance

The software baseline is half the requested frame period, rounded upward:
`ceil(500000000 / requested_fps)` ns. Recommend 20% headroom on that half-period:
`ceil(600000000 / requested_fps)` ns. At requested 120 FPS these are 4,166,667 ns
and **5,000,000 ns**. At measured 119.27 FPS, 5 ms leaves approximately 0.808 ms
for timestamp/cadence variation beyond the half-period. This is an explicit
engineering margin, not a measured jitter distribution or hardware guarantee.

The reference software profile and Q6A harness now require 5 ms. Missing software
tolerance defaults to the period-derived recommendation; an explicitly smaller
than nominal half-period bound is rejected, never silently enlarged. Old explicit
4 ms/120 FPS profiles require operator review.

Half the nominal period plus margin is the engineering basis for choosing the
configured bound, not a per-observation acquisition invariant. Requested FPS is
a nominal mode target, not exact sensor timing evidence. Consecutive native
intervals, their rounded-up half-periods and maximum intervals remain diagnostics:
`left/right.observed_period_ns`, `observed_half_period_ns` and
`observed_max_period_ns`. A temporary interval above twice the tolerance can still
have a valid opposite-camera observation. It does not establish invalid
correspondence and must not fail capture by itself or automatically widen the
bound. Only the actual two-stream nearest/bracketing decision rejects invalid
correspondence; every selected pair must satisfy `abs(RIGHT - LEFT) <= 5 ms` in
the reference mode.

5 ms bounds **software timestamp correspondence**, not optical exposure skew.
Timestamp clock/source, sensor timestamp semantics, shutter timing and physical
triggering require independent evidence. Host arrival remains diagnostic only.
No synchronized exposure or trigger accuracy is inferred from nearest timestamps.

## Deterministic correspondence and re-alignment

Each camera retains two owned observations: front plus one successor. Compare
fronts in a shared known timestamp clock. Exact timestamps pair immediately.
Otherwise read one successor on the older side. Advance only when that successor
is strictly closer to the newer front; exact ties select the earlier observation.
Reject a bracket when neither candidate is within tolerance. Published pairs use
each observation at most once. This is a causal greedy nearest rule over unused
observations, not a global assignment optimizer or a promise of every exposure.

Startup permits at most 32 exclusions total within the profile stall timeout.
After first publication, permit **at most two exclusions total between published
pairs**, across both cameras. The exclusion budget survives across `next()` calls;
it resets only after successful publication. Every call still reads at most two
observations. Re-alignment must publish within the profile stall timeout measured
from the last publication; bounds also apply across calls. No overwritten pending
slots, unbounded searches, automatic retries or favorable-phase waits are used.
Two exclusions is a conservative bound for nearby rates, not permission to
conceal arbitrary clock jumps/rate mismatch; exceeding it fails capture.

Each advancement increments `startup_unmatched_left/right` before first publication
or **`steady_state_unmatched_left/right`** thereafter. Native per-camera counter
continuity is checked on every received observation **before** the pairing rule:
gaps, repeats/reversals, changed clocks and backward timestamps still fail.
Intentional pairing exclusions never increment recorder-drop counters. The
existing LOSSLESS recorder records every published FrameSet or fails explicitly.

## Strict hardware path

`hardware_sync_configured=true` retains equal native counters and its explicitly
configured timestamp limit, with no startup or steady-state re-alignment. Its
missing-tolerance default remains **4 ms**, independent of requested FPS. The
software half-period recommendation does not widen hardware bounds.
An operator assertion alone does not prove physical synchronization; SyncQuality
remains `software` and optical exposure skew remains `unavailable` in both paths.
Future verified hardware-sync semantics must be established separately.

## Persistence, accounting and compatibility

FrameSet metadata stores policy version 2, mode, exact selected native counters,
signed LEFT−RIGHT offset, selected RIGHT−LEFT timestamp delta, candidates/lookahead,
configured tolerance, nominal/recommended bounds, observed periods and cumulative
startup/steady-state counts. `pairing_exclusions` records each excluded observation
since the preceding publication (role, startup/steady phase, native sequence,
timestamp and clock), bounded to 32 at startup or two in steady state. The list is
reset at publication and never carried into another pair's exclusion list.

Failure diagnostics also retain the candidate LEFT/RIGHT timestamps, available
lookahead timestamps, front and lookahead distances, the nearest available
candidate distance, whether candidates bracket the newer front, configured
bound, failure reason and latest/max observed native intervals. Missing
observations remain `unavailable`; an unavailable bracket is not proof of a
nearest candidate. If acquisition fails before the first FrameSet and
`capture.start()` cannot return a handle, the JSON `capture.diagnostics` event
preserves this snapshot in the project's `diagnostics.log`. The harness copies
that failure snapshot to `pairing.json` and `summary.json`.

Stop/failure counts retained lookahead as `shutdown_unmatched_left/right`.
Exclusions after the last published pair are exposed separately as
`unpublished_pairing_exclusions` in final diagnostics; they cannot be associated
with a FrameSet that was never published. For each healthy camera:

`received = published_pairs + startup_unmatched + steady_state_unmatched + shutdown_unmatched`.

A zero recorder-drop count covers published FrameSets. It does not claim all
camera observations are recorded. Replay's continuous sequence claim refers to
FrameSets; recorded child native counters can skip **explicitly excluded** frames,
while native acquisition gap detection still uses the full received sequence.

RawCapture persists each published association, pixels and decision metadata
exactly; replay never discovers cameras, re-pairs or fabricates an observation.
ABI v1, RAW8/Y10P layouts, RawCapture schema 2, project schema 1 and old recordings
remain unchanged. This change adds metadata using existing extension points.

## Validation and remaining evidence

Deterministic tests sweep both signs of arbitrary phase, exact ties and 4.0–4.3 ms
nearest distances. Long traces at 25 ppm in either direction cross multiple
neighbor boundaries. Loaded RAW8/Y10P acquisition through the real bounded
recorder checks exclusion identities, complete accounting, zero recorder loss,
strict counter/gap failures, the two-exclusion bound and exact two-pass replay.
Isolated 12.384 ms intervals on either camera remain accepted when the actual
pair is within 5 ms, including zero-loss RAW8/Y10P recording and replay. Brackets
with both nearest distances at 6 ms still fail and preserve structured evidence.

The real Q6A full run on `4ec71d9d1553216b6a69f942802760e299150567`
used the 5 ms reference, passed Debug/Release 17/17 and disabled-link discovery,
and reached plugin-owned acquisition. `capture.start()` failed with
`left.observed half-period exceeds software pairing tolerance`. This newly
introduced conservative guard blocked acquisition before actual cross-camera
correspondence could be validated; it is different from the earlier 4 ms nearest
pair rejection. Therefore the **5 ms pairing policy itself has not failed on
Q6A**. Real Q6A execution after removing this guard remains pending. The favorable
4 ms starts and later cold 4 ms pairing failure remain recorded honestly.
Sustained-storage acceptance, physical synchronization and optical timing are
still pending; changing this software bound does not establish any of them.
