# UI-M3a: bounded PointCloud auto-load and manual retry

The bridge previously fetched an unchanged unreadable FINALIZED PointCloud on
every 500-ms snapshot cycle because only successful loads advanced its displayed
identity. The frontend now separates confirmed candidate discovery from data
access. Snapshot and preview timers, command serialization, public client, daemon,
protocol, storage, renderer and QML workflows retain their existing contracts.

## Candidate and bound

`CloudRetry` holds exactly one candidate key and no packet: **exact raw canonical
runtime project path, exact artifact ID, published hash, schema_version, bytes and
chunks**. These fields exist in the current Artifact message; there is no invented
revision token or normalization of opaque IDs. Selection remains the last
nonempty FINALIZED PointCloud in the confirmed snapshot's existing order, not an
assertion of a new chronology API. The GUI display QString is never the key.

A confirmed removal, different project/candidate or changed published metadata
evicts the single previous record and starts a new observation tenure. Identical
snapshots retain its failure count/deadline. The count saturates at six, and there
is no per-project history, LRU, retry queue or unbounded cache. Key string bytes
are bounded by the existing 4-MiB control-message limit in `protocol::receive`;
there are only three retained strings and fixed-size counters. Strings are not
truncated into potentially coincident authority keys. Temporary worker/result
copies are bounded by the one outstanding request lease.
Discovery scans descriptor pointers and copies metadata only for the selected
candidate, avoiding repeated project/hash copies for every PointCloud entry.

## State transitions

| State | Automatic decision | Transition |
| --- | --- | --- |
| Ready | First confirmed observation eligible immediately | Successful map → Loaded; failure → Cooling or Suppressed |
| Cooling | No fetch before the monotonic deadline | Eligible attempt succeeds → Loaded; fails → next delay or Suppressed |
| Suppressed | Zero automatic data requests for unchanged evidence | Successful explicit retry → Loaded; changed/removed key → Ready |
| Loaded | Zero automatic data requests for unchanged evidence | Changed/removed key → Ready; failed explicit retry → Suppressed |

Permanent `invalid_argument`, `not_found`, `incompatible`, `unsupported` and
`corrupt` failures suppress immediately. A fresh advertised finalized descriptor
with a refused/missing artifact or unsupported/corrupt mapped packet is not useful
to probe every tick. If a file is repaired without changed published metadata,
only an explicit retry establishes recovery; unchanged snapshots do not prove it.

`io`, `busy`, `cancelled`, `plugin_failed`, unknown numeric codes and untyped local
exceptions use a finite transient budget: first attempt immediately, then delays
**2, 4, 8, 16 and 30 seconds** after successive failures. The sixth failure holds
until manual success or changed evidence. With immediate failures this means
attempts at 0/2/6/14/30/60 seconds, then zero further automatic attempts. A manual
failure consumes the existing budget and cannot downgrade terminal suppression
or restart a successful identity's automatic loading.

`steady_clock` supplies millisecond deadlines. A callable clock is injected into
tests; no additional timer, sleeping GUI thread or busy wait is introduced.
Unavailable, throwing, negative or overflowing clock values permit a newly
observed candidate's first attempt, then fail closed without an automatic deadline
until explicit retry or changed evidence. Deadline arithmetic never wraps.

## Access and diagnostic authority

An explicit selection is guarded by the existing runtime-enabled, connected and
busy checks. Its worker confirms a fresh snapshot, matches the captured exact
project identity (including an empty identity), then requires exactly one
advertised nonempty FINALIZED PointCloud ID. Unknown, raw, duplicate or
mismatched-project requests make **zero** artifact-data calls and do not mutate
capture. A mapped packet must have the supported PointCloud type/version and
agree with a nonzero advertised schema version. One authorized explicit request
may bypass suppression once. Loading an older advertised cloud does not clear
another automatic candidate's failure budget.

An artifact mapping failure retains its original structured phase/code/component
and makes one additional snapshot confirmation. Only a confirmed snapshot asserts
usable runtime connectivity; the shared Failure status cannot distinguish a local
mapping fault from a socket/daemon fault. Failed confirmation retains last-known
evidence but sets connected false. Later unconfirmed refreshes do not fetch data
from retained entries. Mutating operations are executed once, never replayed.

Skipped automatic fetches set no artifact-attempt acknowledgement, so the existing
artifact issue remains unchanged; they neither clear it nor create a new failure.
Successful requests preserve the existing phase-specific clearing behavior.
Identical polls emit only the existing bounded change notifications, without an
extra retry notification source or UI status pretending to be daemon telemetry.

Workers capture client, project, generation, policy and clock by value. Policy
changes install on the GUI thread under the existing watcher lease, which now
extends through `applyResult` and its snapshot signal. Reentrant controls cannot
enqueue extra work. Every completion is generation checked before touching
connectivity, diagnostics, cloud, selection or policy, including A → B → A.
Confirmed project transitions retain the existing replay/preview/cloud invalidation.
This GUI guard cannot repair daemon-wide project/replay isolation or prevent a
different client switching the store between two public RPCs; that existing
backend limitation remains documented in [the backend follow-up](projects-backend-gap.md).

See [regression and full-suite evidence](validation-m3a.md). No hardware timing,
accuracy, laser safety, Windows/macOS or new Scan workspace acceptance is claimed.
