# Calibration-bound L1 ray/plane reference (ALG-03)

This internal CPU algorithm consumes one persisted typed LaserObservation and
explicit immutable camera, rig, declared-plane and policy references. It is not
an optical calibration, physical plane estimate, service or Plugin/Client SDK.
The implementation follows the independently reviewed Issue #63 geometry
contract at `072972e9c3b58ab282d2ad824a815dd9a3622980` (PR #120).

`prepare` validates the exact processing dependency graph, physical camera/role,
component applicability, original-image dimensions, frame IDs/names and declared
line identity. `recorded_effective` requires the captured effective calibration;
`explicit_processing` records deliberate alternative geometry without rewriting
history. Neither mode consults active calibration. Accepted ALG-02 parameter
fingerprints remain opaque source provenance, not fabricated Store artifacts.
Unavailable historical acquisition RAW/dark does not require image transport;
selected M4 calibration metadata validation remains mandatory.

The numeric reference uses stored binary64 geometry and exact widened f32 source
pixels, bounded Brown5 inversion, strong-monotonicity/root certification and
outward interval propagation. It preserves distinct arithmetic L2 radius,
source-rounding cell bound and optional declared parameter bounds. None is a
measured scanner accuracy or covariance. Policy limits and ill-conditioned,
uncertified, parallel, behind-camera, backside, ambiguous and mismatched samples
have explicit refusal/rejection outcomes; no fallback plane or silent filtering.

Each result is one generic Tensor Packet: manifest plus all 16 ordered row
columns (121 bytes per source row). All rows, duplicate coordinates, source flags
and rejected reasons remain in source order. N=0 is manifest-only. Typed replay
validates provenance/layout and recomputes deterministic arithmetic; a generic
FINALIZED descriptor is not itself typed geometric success.

Prepared state owns immutable geometry and source buffers. Individual node
instances are serial; separate instances can share immutable prepared state.
Source backing is charged through the semantic graph's retained extent and node
resource budget. Geometry resolution reserves canonical/decoded/typed working
storage before JSON allocation, using a nonallocating lexical pass: 16 bytes per
encoded byte plus 512 bytes per value/key, conservatively covering parser/DOM
nodes, map/vector growth, temporary serialization and fixed-schema typed copies.
The reservation persists across all retained graph documents. Resource refusal
is `busy`, never an invitation to retry until favorable memory conditions.

Persistence validates before one Store reservation and performs typed readback
after finalization. Post-reservation failure/cancellation retains exact artifact
identity, observed Store state and initiating error. It does not reserve again,
claim rollback or convert committed generic storage into typed success. Recovery
must be explicit and must revalidate the same retained object.

Tests include actual standalone dependency refusals, low-cap/large-shallow decode
admission, entered-state post-reservation failure/cancellation and FINALIZED
readback failure/revalidation. The Decimal enclosure oracle uses independent
100-digit fixed-point Brown inversion of stored input pixels rather than the
production Newton/Jacobian. Real optics/HIL, Windows/macOS native qualification,
long soak and physical calibration remain separate evidence gates.
