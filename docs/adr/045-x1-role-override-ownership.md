# ADR-045: Opt-in daemon ownership of whole-X1 camera role overrides

Status: Owner-approved direction for CAL-03/67A; implementation and independent
integration acceptance pending. [Root decision](https://github.com/martinkoenig/mantis-studio/issues/67#issuecomment-6720577653).
This record does not accept physical mapping, M8/M8b, Studio integration or release readiness.

## Context

The first-party X1 plugin reads an explicit `MANTIS_X1_PROFILE`. Profile2
`runtime_setup.ownership=selected-routes` grants media setup ownership, not
filesystem write authority. Physical identity is bus/sensor, while the historical
logical parent orders those identities by LEFT/RIGHT. Stopped adapters can retain
the plugin lease; Session inactivity precedes actual producer/recorder retirement.
The serial daemon control dispatcher must remain available during role persistence.

## Decision

1. Only explicit daemon startup `--x1-role-state-dir` enables this feature.
   Deployment provisions an absolute, non-symlink, daemon-owned private leaf,
   for example `/var/lib/mantis/device-overrides`, below trusted ancestry.
   There is no implicit default, packaged-profile update, client-selected path or
   automatic directory provisioning. Legacy opt-out discovery/capture is retained.
2. One lifetime exclusive configuration-owner lock protects that directory.
   Deployment establishes one authority for the physical X1. A second root,
   external camera process or another host is outside this lock's exclusion;
   no external authority or distributed ownership claim is made.
3. Only profile2 selected-routes with exact distinct buses/sensors/routes and no
   base calibration stamp is eligible. The override binds the exact base-profile
   byte checksum and complete approved pair. It permits identity or exchange of
   the two complete tuples, changing no mode, timing, sensor selection or calibration.
   The existing `fnv1a64` content-checksum convention detects changed inputs; it is
   not authentication. Trusted filesystem ownership supplies write authority.
4. Store one bounded version-1 `x1-role-map.v1.json`, a lifetime owner lock and at
   most one exclusive transaction temporary in the approved directory. Reject
   unsafe file types/links/ownership, duplicate JSON keys, unsupported versions,
   stale bases, ambiguous topology and malformed state. Invalid present state
   never falls back to the base mapping. Base profile bytes remain unchanged.
5. Preserve historical logical/child IDs, packet optical-frame names, physical
   identities and artifact/replay semantics. The internal X1 lease key sorts
   length-delimited physical bus/sensor tuples independently of assigned roles.
   The daemon's one configured-pair reservation excludes conflicting admission
   and state changes across discovery, persistence, refresh and rollback.
6. Public v1 query is an in-memory snapshot. Validate and apply return ordinary
   daemon Jobs and carry explicit desired order, expected generation, exact
   current parent and discovery token. Both perform fresh discovery and rig checks
   off the control dispatcher. A changed node resolution requires a fresh query;
   it does not identify a camera. Requests are desired-state operations, never
   toggle commands. Generation exhaustion and stale/incarnation tokens fail.
7. Require actual completed camera cleanup, released adapters, projected cleanup,
   resolved close and recorder retirement, no other reservation, no calibration
   in flight and idle Jobs. The worker excludes only its own exact Job ID.
   Initial scope conservatively rejects all other busy Jobs. Never call a blocking
   stop to manufacture quiescence. Queued cancellation releases the reservation
   when the task is actually discarded. Once entered, cancellation cannot preempt
   I/O or acknowledge returned ownership before retirement. No forced timeout
   releases ownership. CAL-02 live admission must join this same guard when released.
8. Write an exclusive same-directory temporary, complete bounded bytes, sync the
   file, atomically replace, sync the directory, validate readback and rebuild
   discovery through the same resolver used by camera/projected opens. All this
   work, including adapter destruction, runs on the generic Job worker. Query,
   Job status and cancellation do not join it. Completion logging uses separate
   I/O/snapshot locks and retains captured ownership until it retires.
9. On failure restore the previous complete payload or prior absence, sync and
   rediscover before reopening admission. A failed/uncertain rollback quarantines
   admission with explicit incomplete/error state; it is not successful rollback.
   Restart validates the actual complete committed state before discovery. Crash
   or lost reply before acknowledgement can leave old or new complete state;
   query after restart is authoritative. A foreign/stale transaction temporary is
   never followed, overwritten or silently removed; reconciliation is explicit.
10. Refuse a role change while the current project's old parent has an active
    rig. Operator uses existing explicit calibration clear first. Validate any
    prospective-parent binding against the desired roles, physical identities,
    dimensions and supported schema. Do not couple SQLite clear/migration to the
    file transaction. Other projects' exact-parent bindings remain unchanged and
    are interpreted under their recorded mapping and ordinary compatibility rules.
    No automatic cross-project invalidation, history rewrite or binding transfer.
11. Authorization remains the authenticated local daemon token. Capability
    `org.mantis.x1.role-config.v1` advertises support only; it is not a credential.
    Unsupported peers, absent opt-in and unknown schemas fail explicitly.
    Request tags 80–82 and response field 31 are additive, leaving CAL-02's lower
    candidate range available. Frozen plugin C ABI and packet/catalog definitions
    are unchanged. New capture provenance records the effective profile and role
    base/generation metadata, without rewriting old rows or packets.
12. Studio UI is later 67B. It must use this accepted public seam asynchronously,
    provide explicit confirm/apply, show sensor/bus/route provenance, reject stale
    results and verify labels against canonical packets/rig coordinates. Unknown,
    stale or unsupported state disables Apply; picture direction is no oracle.

## Validation and limits

R1 tests cover writer confinement, lock exclusion, exact pair permutation,
pre/post-rename faults, rollback uncertainty, durable restart, base changes,
active-rig refusal/explicit clear, stale discovery, control responsiveness during
blocked apply I/O, recorder retirement and immutable historical replay. Canonical
new packets must match independent sensor-coded fixture pixels and optical frames.
Test sensitivity must demonstrate that realistic generation/retirement regressions
fail, with safe barrier release on assertion unwind. Required complete Studio
ON/OFF, sanitizer and native x86/ARM CI gates remain unchanged. Software fixtures
supply no physical LEFT/RIGHT, Q6A durability, timing or measurement qualification.

Rejected alternatives: UI-only labels; writing the environment-selected profile;
a generic unrestricted hardware command; role-ordered exclusion keys; blocking
persistence/refresh on the control dispatcher; automatic rig rebinding or historical
rewrite; calling an uncertain filesystem transition successfully rolled back.
