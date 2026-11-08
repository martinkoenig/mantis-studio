# Physical X1 role setup in Studio (CAL-03 / 67B)

Implementation status: software implementation; independent acceptance pending. This UI uses the accepted CAL-03 daemon API. It does not
qualify physical LEFT/RIGHT accuracy, hardware timing, current Qt security or M8.

Open **Devices → Calibration → Configure physical X1 roles**, or the Calibration
workspace's **Device** stage. Select the exact configured X1 parent. The role panel
shows capability/daemon opt-in, discovery token, base digest, generation and the
complete current and proposed sensor/bus/route identities. A resolved video node
is displayed as a resolution, never used as physical identity. Unknown, malformed,
unsupported, stale, busy or blocked evidence disables configuration.

The operator proposes an exchange of the complete two tuples. **Validate proposal**
submits an ordinary authenticated daemon Job. Validation refreshes discovery even
without changing generation; only a completed, retired, tracked validation with
unchanged physical evidence enables review of the new discovery token. **Review
validated change** freezes the exact parent, generation, discovery and both
assignments. A separate acknowledgement and **Confirm and apply roles** submit the
explicit desired order. Background changes, navigation, selection, disconnect or
changed discovery invalidate confirmation. Apply rechecks the project and exact
queried evidence before sending the public request; the daemon performs final
admission, rig, persistence and rollback checks.

An active rig blocks role changes. **Review active rig / explicit clear** only
navigates to the existing Review & Activate stage. Clearing remains that stage's
separate confirmed calibration API operation. No binding, historical capture or
immutable calibration artifact is silently cleared, rewritten or inferred.

Query, validation/apply submission and cancellation use QtConcurrent and the public
Client. Bridge snapshot notifications trigger bounded refreshes while Device setup is
visible or an acknowledged Job remains outstanding. Queued/running Jobs use
ordinary Job snapshots; role state is queried after terminal status so discovery
replacement cannot invalidate the originating proposal halfway through its Job. Navigation does not
cancel daemon work. Cancellation acknowledgement never means ownership has
retired; a running commit can still complete. Terminal Job status and authoritative
post-Job query are required before reporting an applied mapping. Lost apply replies
are reported as unconfirmed outcomes and resolved by query; mutations are never
automatically retried. Operation errors survive unrelated successful queries.

The panel is scrollable inside the existing workspace, wraps literal identity
text, exposes keyboard controls and scrolls focused controls into view. Preview
surfaces retain their existing canonical packet-role routing and ownership.
Production Studio never opens or writes a profile or creates an override directory;
only the daemon's separately provisioned opt-in authority can persist a change.
See [ADR-045](../adr/045-x1-role-override-ownership.md) and
[build/startup requirements](../../BUILDING.md#opt-in-x1-role-configuration-cal-03-backend).

## Verification scope

The new registrations are `studio-x1-role-controller`, `studio-x1-role-qml` and
Linux `studio-x1-role-daemon`. Their source covers exact request/proposal identity,
64-bit generation preservation, stale confirmation/discovery, capability/schema
and topology refusal, active-rig refusal, terminal/retired Jobs, cancellation,
late query/apply/cancel replies, teardown, lost acknowledgement, keyboard
confirmation, resize and canonical preview/header/sensor-pixel composition.
The daemon fixture uses the existing isolated software X1 plugin, never hardware.
Existing role-service and calibration suites retain persistence/rollback/replay
and geometric-binding coverage; the frontend adds no calibration mathematics.

Qualification requires focused native execution and a representative restored
negative-sensitivity control, complete Studio ON/OFF and strict sanitizer suites,
detector controls, the existing 30 UI repetitions and required exact-head/exact-
integration CI. Actual outcomes belong to the exact-source/commit evidence; test
definitions alone do not establish a passing execution.
No check or platform is accepted merely by this document or worker completion.
