# ADR-026: Bounded timestamp pairing for independent camera streams

Status: Accepted for v0.2 development

## Evidence and decision

User Q6A testing found a persistent approximately 51.8 ms V4L2 timestamp offset
between equal native counters on independently started OV9281 streams. Native
counters identify observations within each camera; they are not cross-camera
exposure identifiers. Raising the pairing tolerance to 100 ms is rejected.

The X1 plugin pairs software observations in a shared, known clock domain by
nearest timestamps. Each camera has a fixed two-slot pending queue (front plus
one lookahead). Compare the two fronts. Exact timestamps pair immediately.
Otherwise read one successor on the older side to establish the closest
candidate; monotonic timestamps mean no later candidate can beat a successor
that has passed the newer front. Advance toward a strictly closer successor;
ties select the earlier observation. The selected pair must satisfy the profile
tolerance, which remains 4 ms in the reference profile. Incomparable clocks,
backward timestamps and camera-native discontinuities fail explicitly.

Before the first pair only, advancing an older observation increments the
corresponding `startup_unmatched_left/right` count. Startup permits at most 32
exclusions total and is limited by the profile stall timeout. After the first
pair, any required advancement/exclusion fails capture; software pairing never
hides steady-state loss. Each `next` call reads at most two observations and
respects the caller timeout. Pending saturation fails instead of overwriting.

Hardware-configured mode additionally requires equal native counters and checks
the same timestamp tolerance without startup counter realignment. This is a
strict driver-counter policy, not proof of trigger or exposure synchronization.
SyncQuality remains software and exposure skew remains unavailable in both modes.

## Diagnostics and compatibility

FrameSet metadata persists pairing mode, selected native counters, signed modular
LEFT−RIGHT counter offset, paired RIGHT−LEFT V4L2 delta, host delta, startup
counts, tolerance and bounds. Native counter equality is explicitly named
`native_counter_equality`; it is not the software pairing success indicator.
`v4l2_delta_ns` remains an alias for the selected pair delta. Stop/failure accounts
for retained lookahead in `shutdown_unmatched_left/right`. Received observations
excluded at startup or retained at shutdown are not recorded; zero recorder
drops does not mean every camera observation was included. All published
FrameSets continue through the unchanged bounded LOSSLESS recording branch.

The change uses existing plugin and immutable metadata extension points. C ABI
v1, native camera setup, RAW8/Y10P layouts, RawCapture schema 2, replay semantics,
SQLite/project schema 1 and historical artifacts are unchanged. Replay returns
the recorded associations and metadata and never re-pairs observations.

## Alternatives

Equal counters and host-arrival ordering are rejected as software correspondence
rules. Unbounded timestamp search and steady-state silent drops violate bounded,
loss-aware acquisition. A larger tolerance conceals incorrect correspondence.
Clock mapping, physical trigger programming and optical skew instrumentation
require separate evidence and are outside this fix. The corrected 4 ms pairing
requires user execution on Q6A before any hardware success claim.
