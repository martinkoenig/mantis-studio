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

## One recorded-bundle increment (Refs #64)

`process_recorded` in the internal `l1_recorded.hpp` composes the existing
cancellable `BundleCaptureReader`, ALG-02 extractor and ALG-03 typed node. Supply
an exact finalized local RawCapture-3 reference in the extractor configuration,
a zero-based record ordinal (including evidence-only records), explicit camera/
emitter/stream/line and subpixel configuration, and immutable plane, policy,
camera and rig references. Control-only selections are refused; no first-image
or active-calibration fallback is made. The reader validates the prefix through
the selection, not the unvisited suffix or successful completion of the run.

Two lossless typed plans straddle the existing immutable observation boundary:
extract once, persist the exact observation, prepare its geometry, then execute
triangulation and persist the existing result type. `persist_result` owns typed
readback and the success boundary; the wrapper performs no redundant readback or
cancellation check afterward. Success returns both artifact references. Repeating
creates distinct artifact IDs, with identical observation bytes and metric columns;
only the derived observation ID differs in the metric manifest. RAW is unchanged.

Failures retain their status, distinguish missing/mismatched inputs, corruption
and cancellation, and publish no successful return. These are not multi-artifact
transactions: later geometry failure can leave a finalized observation, and
commit/readback failure can leave an OPEN/RECOVERABLE/FINALIZED reservation. The
existing Store receipt identifies retained artifacts and observed state; later
failures include the earlier observation receipt without discarding a metric
receipt. Recovery/revalidation is explicit, never an implicit retry or deletion.

`alg03-recorded-integration` uses the existing integration executable with a
recorded control prefix and padded RAW8 quadratic image, an independent
long-double forward-projection oracle, exact lineage/version and repeat checks,
source/calibration refusals, real segment corruption/restoration and cancellation
after observation journaling with same-ID recovery. It retains the 60-second
integration deadline separately from the pre-existing cases. This is one offline
software increment, not whole-issue #64 completion: no service/SDK/UI surface,
worker automation, live acquisition, physical accuracy, native ARM throughput,
full-job backpressure or production qualification is claimed.

Tests include actual standalone dependency refusals, low-cap/large-shallow decode
admission, entered-state post-reservation failure/cancellation and FINALIZED
readback failure/revalidation. The Decimal enclosure oracle uses independent
100-digit fixed-point Brown inversion of stored input pixels rather than the
production Newton/Jacobian. Real optics/HIL, Windows/macOS native qualification,
long soak and physical calibration remain separate evidence gates.

## Export an explicitly selected result to PLY (Refs #64)

Use `mantis-cli export ARTIFACT OUTPUT.ply` (or `Client::export_artifact`)
with the exact finalized `org.mantis.Alg03Result` schema 1 artifact ID in the
current project. Choose a new destination outside the project store. The existing
daemon export job runs the typed reader once, including Store integrity checks,
exact processing dependencies and arithmetic replay. No recorded images are
reprocessed. With no source rows it fails with `empty_input`; with rows but no
accepted points it fails with `all_rejected`. Neither produces an empty PLY.

For a nonempty accepted set the job passes a temporary PointCloud to the existing
first-party PLY exporter. It retains the processing rig frame and calibration in
the packet, uses the accepted mask in source order (including duplicate points),
and emits the same millimeter axes. Every axis is narrowed to IEEE 754 binary32
with round-to-nearest, ties-to-even, without changing the floating-point environment.
Each axis loses at most half the larger adjacent binary32 spacing, at most
0.03125 mm under the existing 1e6 mm position cap. This is conversion error,
separate from the algorithm's f64 geometry error and not scanner accuracy.

The completed export job's `result_artifact` is the **original** ALG03 ID. In the
same project's artifact snapshot that ID identifies the exact finalized type,
hash and direct inputs, including the referenced observation. Use those project
artifacts for the recorded RAW ancestry, calibration, row identities, rejection
reasons and full precision. The compact PLY retains the usual millimeters header
and is **not** a self-contained provenance or rejection record. There is no
sidecar, new project artifact or change to which artifacts Studio advertises as
PointCloud. Legacy PointCloud exports and the exporter format are unchanged.
