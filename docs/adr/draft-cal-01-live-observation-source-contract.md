# Draft ADR: CAL-01 live calibration observation sources

Status: **DRAFT / PROPOSED — NOT ACCEPTED.** No accepted ADR number is allocated.
Tracks [CAL-01 #65](https://github.com/martinkoenig/mantis-studio/issues/65).
Inspection baseline: `1f89ad04a341731da26f4ca4b0bb05690c02acb9`.

## Applicability and authority

This is an owner-authorized draft ADR transcription of the independently reviewed
[R2 decision material](https://github.com/martinkoenig/mantis-studio/issues/65#issuecomment-6701796619),
not a normative amendment, implementation, migration or CAL-02 dispatch. Every
SHALL below is proposed text, applicable only if separately adopted through an
explicit owner decision, independent architecture/source review and separately
authorized merge with exact-SHA integration review. Creating, reviewing or merging
a document that still has DRAFT status does not silently change that status.

Accepted [ADR-028](028-target-observations-and-deterministic-calibration-datasets.md),
[ADR-030](030-calibration-artifacts-revisioning-and-active-binding.md),
[ADR-035](035-calibration-control-api-and-job-orchestration.md),
[ADR-036](036-studio-calibration-workspace-uses-public-calibration-api.md) and
[ADR-037](037-real-hardware-calibration-acceptance-is-evidence-driven.md) remain
binding and byte-for-byte unchanged. In particular, the physical RawCapture M8
route is retained; live-only no-pixel equivalence remains BLOCKED. #66 is not
started or authorized by this draft.

The integrated [PR #98 proposal](../architecture/cal-01-live-observation-proposal.md)
is unchanged and remains a proposal. R2 supersedes earlier draft recommendations
for pre-start persistent source reservation and a public selection1 extension:
this candidate uses ephemeral collection, postseal publication and explicit
selection2 with unchanged M3 numerical/descriptor1. It changes no historical
schema1 interpretation or accepted numerical rule.

## Context and source-grounded extension seams

The current recording-first workflow cannot supply a typed observation-only live
source without extending explicit identity, persistence and control boundaries.
Existing extension points are reused rather than introducing another acquisition
architecture. CONFIRMED means inspected source at the pinned baseline; proposed
behavior below is not delivered merely by this document.

| Confirmed current boundary | Source / minimum proposed extension |
| --- | --- |
| Session starts a recorder and defines readiness through its first callback | [device runtime](../../src/device-runtime/device-runtime.cpp): explicit observation consumer/readiness, same acquisition owner |
| Source keys and M3 ingestion require real finalized RawCapture2 | [dataset builder](../../src/calibration-dataset/calibration-dataset.cpp), [pure model](../../src/calibration/include/mantis/calibration_dataset.hpp): additive typed source/key values, no fake RawCapture IDs |
| M4 stereo uses the opposite camera's unselected successful detection | [M4 partitions](../../src/calibration/include/mantis/calibration_solve.hpp): preserve full population, physical-ID union and existing numerical/split rules |
| Ordinary artifact allocation differs from logical calibration revision reservation | [Store](../../src/artifact-store/artifact-store.cpp): source uses ordinary begin only after seal; new result series use schema-aware begin_calibration |
| Cancelled queued Jobs can discard their task without invoking it | [Job manager](../../src/jobs/jobs.cpp): shared operation latch and nonblocking queued-task RAII retirement |
| DTOs/codecs/public clients have explicit legacy schema and RawCapture fields | [service](../../src/services/include/mantis/calibration_service.hpp), [codecs](../../src/calibration-artifacts/calibration-artifacts.cpp), [client](../../src/client/include/mantis/client.hpp): additive versioned surfaces, old-peer rejection |
| Current protocol has Request through47, Response through18, CaptureStart.devices1 | [actual protocol source](../../protocol/protobuf/mantis.proto): only candidate unused additions below; this draft allocates no fields |

[ADR-021](021-enumerated-acquisition-and-frameset-contract.md),
[023](023-acquisition-preview-leases-and-storage-jobs.md),
[025](025-packed-monochrome-image-byte-layout.md),
[026](026-bounded-software-observation-pairing.md),
[027](027-calibration-target-geometry-and-physical-scale.md),
[029](029-calibration-solve-validation-and-stereo-policy.md),
[033](033-reliability-performance-release-gates.md),
[frozen architecture](../../MANTIS_STUDIO_ARCHITECTURE.md) and the
[binding test-quality contract](../engineering/test-quality.md) remain authoritative.
The current #102 Store/Session candidate is not assumed accepted. Source defects
and historical green runs do not establish the reported microSD freeze cause.

## Proposed owner decisions — all remain unadopted

| Choice | Recommended candidate / rationale | Alternatives and compatibility gate |
| --- | --- | --- |
| A — durability | Bounded RAM and ephemeral acquisition identity; retire/drain/seal before ordinary source reservation and one publication | Pre-start durable attempt inventory and compact observation journals add recovery/startup surface without recovering RAM in the first case; defer both. Crash before publication honestly loses volatile evidence. Sparse forensic pixels require separate explicit consent/quota and never normal-mode fallback. |
| B — graph/version/migration | Source1/Document2, Dataset2/Camera2/Rig2, Target1; schema-stable new result series, typed graph and explicit legacy-client guards | No old bytes/rows rewritten. Initial no-DDL is conditional on actual Store/import/list/cursor/activation proof; extra persistent indexes/history require a reviewed transactional migration. |
| C — admission/selection | Admission2 is scheduling-dependent latest-ready sampling; selection2 defines key2 ordering with M3 numerical/descriptor1 unchanged | Preserve old selection1; exact legacy embedding, tie/permutation/overflow and x86/ARM proof before implementation acceptance. Retain opposite-camera unselected detections. |
| D — ownership/containment | Existing daemon physical-camera lease plus bounded supervised helper, explicit IPC/lifetime/cancel/retirement and provisional resource ceilings | An in-process alternative requires explicitly weaker hung-call retirement claims. No zero-copy or finite arbitrary OS-I/O completion guarantee; budgets are future measured gates. |
| E — physical M8 | Retain binding RawCapture evidence and three independent real acquisitions/restarts, six fixed-model cross evaluations, measured target and activation/history/replay | No-pixel source IDs/corners/hashes are not equivalent physical evidence. Any alternative needs separate owner/independent proof; M8b remains separate. |

The following L/P/C/U/M clauses are transcribed verbatim from the reviewed
[R2 draft text](https://github.com/martinkoenig/mantis-studio/issues/65#issuecomment-6701603330).
The [R2 report](https://github.com/martinkoenig/mantis-studio/issues/65#issuecomment-6701602554)
and [evidence index](https://github.com/martinkoenig/mantis-studio/issues/65#issuecomment-6701631713)
retain their scoped research acceptance and original evidence limitations.

## ADR-028: add a version-scoped live-source subsection

**L01 — Identity and ownership.** Live collection SHALL be an observation consumer of the existing daemon acquisition/Session path. It SHALL use the selected acquisition-capable composite parent and exclusive physical component resources, with unchanged accepted pairing. It SHALL NOT open children independently, use a second camera/pairing architecture, create a RawCapture, or supply dummy RawCapture identities to old M2/M3 validators. Recording mode retains its existing readiness, LOSSLESS queue and failure semantics.

**L02 — Ephemeral identity.** Before collection the daemon SHALL allocate only an operational identity, not a Store source artifact or calibration logical revision. `LiveAcquisitionIdentityV1` is `(identity_schema=1, daemon_nonce[16], acquisition_nonce[16], generation:uint64)`. Nonces are fresh CSPRNG bytes; generation is positive and monotonic within the daemon incarnation; checked exhaustion/entropy failure is fatal. PID, wall time, project path and native sequence alone are insufficient identity. Operation fences additionally bind the project-instance epoch and discovery/profile generations; these are not relabelable source identities. A source's identity is preserved on final publication/import and does not prove physical acquisition authenticity.

**L03 — Source and key values.** New pure values SHALL have these meanings; legacy values SHALL remain unchanged:

```text
SourceIdentityV2 = RawCaptureIdentity(exact_raw_artifact_id)
                 | LiveIdentity(LiveAcquisitionIdentityV1)
FrameSetKeyV2   = (source_kind, SourceIdentityV2, parent_sequence:uint64)
ObservationKeyV2= (FrameSetKeyV2, exact_role:utf8, physical_camera_id:utf8)
TargetObservationV2 = {
  key, target_identity_and_revision, target_type, image_width, image_height,
  sorted_point_ids, associated_image_points_px, associated_object_points_mm,
  detection_evidence, pair_provenance_key, image_evidence_key
}
SourceRecordV2 = {
  key, outcome:detected|no_target, observation:TargetObservationV2|null,
  pair_provenance_key, image_evidence_key, record_sha256
}
```

Source kind ranks are `raw_capture=0`, `live_observation=1`. Raw identity compares exactly as the existing raw artifact-ID string. Live identity compares unsigned daemon nonce bytes, unsigned acquisition nonce bytes, then numeric generation. Frame sequence compares numerically unsigned; role and physical ID compare unsigned UTF-8 bytes with the shorter prefix first. No locale, case folding, role alias or array order changes the key. Identity version must be known; unknown versions fail incompatible. A live observation can exist in RAM before an artifact reference exists; it SHALL NOT encode a fictional finalized reference.

**L04 — Legacy embedding and selection versions.** The order-preserving legacy embedding is

```text
E(((raw_id, sequence), role, camera)) =
  ((raw_capture, RawCaptureIdentity(raw_id), sequence), role, camera)
```

For all valid legacy keys a,b, `a<b iff E(a)<E(b)` and equality is preserved. Old datasets/builders remain schema1/selection1. New Dataset2 uses `key_schema=2`, `admission_policy_version=2` in source evidence, `descriptor_policy_version=1`, `selection_policy_version=2`, `split_policy_version=1`, `solver_policy_version=1`. Selection2 is the existing M3 v1 numerical algorithm with the explicitly defined extended key comparator: identical eight image-space components, checked half-away `llround(component*1000000)` int64 quantization, checked uint64 squared distances/sum, per-camera seed/maximin/tie behavior, ranks and budget. If count fits budget, select all in canonical key order. Otherwise seed is smallest fixed descriptor then key; at each step choose largest minimum distance, ties smallest key. No reservoir/first-N/quality or chronological selector is introduced.

**L05 — Exact compatibility obligations.** Before accepting an implementation, require discrete key/rank/partition equality for E over retained legacy fixtures, equal descriptors, reordered source/candidate arguments, duplicate keys, quantization boundaries and overflow; execute independent x86/ARM checks. Golden equal-descriptor keys raw-A10/raw-A20/raw-B1, same role/camera, budget2 select raw-A10 then raw-A20. With centroid-X fixed values0/500000/1000000 and other components equal/valid, budget2 selects raw-A10 then raw-B1. These are required oracles, not execution results in this draft. Duplicate keys and arithmetic overflow fail before partial selection mutation.

**L06 — Admission2.** `paired_stride_latest_v2` anchors eligibility to first valid parent sequence s0: `(s-s0)%stride==0`, positive explicit stride and checked continuity/arithmetic. It retains one pending latest pair and at most one in-flight pair. Replacement evicts only pending, recording its key/reason; no completed/in-flight record is evicted. Dispatch is admission to analysis and reserves exact pair/role record and ledger capacity first. Producer SHALL NOT wait for detection, hashing, conversion, helper IPC, UI, filesystem or database. Essential source identity/native/parent continuity is validated even for policy-skipped pairs. Scheduling can change the admitted population; determinism is claimed only from a finalized population and config.

**L07 — Population and accounting.** A successful source SHALL retain every completed admitted detection and successful no_target record for each required camera, including all detections unselected by later M3 selection. Empty successful observations are invalid. Required-frame absence, source inconsistency, layout/backend errors and unknown essential pairing provenance fail the session; they SHALL NOT become no_target. A zero-target source can be structurally valid, while solving it can fail eligibility. Failed/cancelled diagnostic evidence is not a usable source. The sealed equalities are:

```text
published = policy_skipped + eligible
eligible = dispatched + pending_replaced + predispatch_rejected + cancelled_pending
dispatched = completed_pairs + failed_pairs + cancelled_inflight
completed_camera_records = detected + no_target
detected = selected + detected_unselected       [per Dataset/per camera]
```

Live snapshots include pending/in-flight gauges. Successful publication has zero failed/cancelled admitted records and closed gauges. Policy-limit Finish drains the already reserved tail. Source/native loss, ADR026 pairing exclusions and deliberate sampling retain distinct ledgers and counter meanings. Overflow/incomplete accounting cannot be sealed success. Do not manufacture unobserved physical exposure counts.

**L08 — Provenance payload.** Each source SHALL retain exact target dependency/content; parent/plugin/build/profile descriptors; physical component/role identities; dimensions, optical frame ID/name and supplied mode/crop identity; original source calibration and captured active binding as distinct references; parent/child sequences, timestamps, clock domain ID/name and supplied flags/source; incarnation-scoped host receipt; pairing policy/mode/tolerance/delta/exclusions; actual detector implementation/options/OpenCV/Mantis build; preprocessing and admission configs; complete retained-evidence inventory and terminal boundary. Unknown optical exposure/skew/uncertainty SHALL remain absent with reason, never numeric zero or invented hardware sync. Parent/children must agree and remain stable. A logical source calibration reference without a resolved artifact SHALL not acquire a fictional artifact hash.

**L09 — Image and record evidence.** While dispatched bytes are owned/mapped, compute SHA-256 over a domain-separated canonical layout plus all delivered bytes including padding; separately hash tight detector-u8 rows. Exact proposed digest preimages are:

```text
raw_image_sha256 = SHA256("mantis.cal.live.raw.v1\0" ||
  u64be(len(canonical_layout_json)) || canonical_layout_json ||
  u64be(delivered_byte_count) || delivered_bytes)
detector_input_sha256 = SHA256("mantis.cal.live.u8.v1\0" ||
  u32be(width) || u32be(height) || tight_u8_rows)
record_sha256 = SHA256("mantis.cal.live.record.v2\0" ||
  canonical_record_without_record_sha256)
```

`canonical_layout_json` has exact known fields packing/layout-version/width/height/row-stride/delivered-extent; encoding follows canonical document rules. Neither image digest includes source nonce/role/clock, so relabeling does not itself change pixel-content evidence. Lower Y10P bits are absent from detector-u8 hashes; packing/padding affects raw hashes. Hashes are limited reuse/integrity witnesses, not originals or physical independence proof. Non-dispatched/cancelled images have absent digest with reason, never an invented hash. Store's existing hash algorithm/descriptor remains unchanged and explicitly identified; a document SHA-256 can accompany new external references without inserting a self-hash into its own document.

**L10 — M4 unchanged.** The new keys do not change ADR029: independent pinhole-brown5 mono, ≥4 noncollinear planar correspondences, held-out `0<H<N` and positions `floor(k*(N+1)/(H+1))-1` for k1..H, overflow-safe arithmetic; preliminary training excludes held-out data, fixed-model mono held-out pose estimation, separate final refit. Stereo uses final fixed mono intrinsics and the union of frames selected by either role ordered by min/max rank then FrameSetKeyV2, UINT32_MAX for unselected. Both camera records must exist; join physical ChArUco IDs, including an unselected opposite-camera detection. Preserve typed missing/insufficient/degenerate dispositions and all three stages. Checkerboard remains legacy mono-only; no optical PASS/metrology thresholds.

## ADR-030: add explicit schema and durability extensions

**P01 — Artifact/document matrix.** Only new objects use the following contracts. Existing schema1 bytes/rows/references/reader behavior are unchanged.

| Artifact kind | Artifact schema | Document packet/envelope | Own logical calibration revision | Dependencies |
| --- | --- | --- | --- | --- |
| CalibrationTarget | 1 unchanged | 1 unchanged | existing Target1 | none |
| CalibrationObservationSource, new type | 1 | 2 | NONE; real ordinary artifact ID after seal | Target1; captured active Rig1/2 if resolved/present |
| CalibrationDataset | 2 | 2 | new Dataset2 series | Target1 and each exact finalized live source1 |
| CameraCalibration | 2 | 2 | new Camera2 series | exact Dataset2, Target1 |
| RigCalibration | 2 | 2 | new Rig2 series | exact Dataset2, Target1 and distinct ordered LEFT/RIGHT Camera2 |

All names use the existing `org.mantis.` prefix. Document2 root is exactly `schema_version:2`, `artifact_schema_version`, `kind`, `conventions`, `payload`; packet type `org.mantis.CalibrationDocument` version2 with one existing-named u8 UTF-8 attribute. Neutral packet headers retain deterministic zero/empty/unknown defaults, including the neutral empty calibration reference; source identity and actual clocks live in payload. No self-artifact-ID/self-hash, pixels, hidden current-state lookup or SQLite document blob. Source's artifact schema1 is not the historical CalibrationDataset schema1.

**P02 — Exact payload relationships.** New source payload fields are `source_identity`, `target_reference`, `target`, `device_snapshot`, `calibration_snapshots`, `detector_manifest`, `preprocessing_config`, `admission_config`, `pair_provenance`, `image_evidence`, `records`, `ledger`, `terminal_seal`. Records are sorted ObservationKeyV2, points sorted physical ID, pair/image entries keyed canonically and referenced by digest. Source contains no M3 selected-only truncation. `terminal_seal` contains final observed parent boundary, source/helper-retired evidence, reason, closed counts/gauges and `complete=true`; false/missing/inconsistent seal is not usable.

Dataset2 payload is `revision`, `target_reference`, `source_references`, `dataset`; dataset includes exact target, config/key/descriptor/selection versions, ordered cameras and full source-record population with digests/descriptors/optional ranks. All requested roles must be represented for every retained pair; initial datasets are live-only. Total population equals the selected sources' complete successful admitted population, not arbitrary selected corners. Camera2/Rig2 use the existing numerical payload structures and evidence with ObservationKeyV2/FrameSetKeyV2 and explicit schema2 configs; solver/split policies stay1.

`TypedArtifactReferenceV2` contains `kind`, `artifact_schema_version`, exact `artifact_id`, `store_hash{algorithm,hex}`, and `document_sha256`. Document SHA-256 is computed externally over the exact canonical JSON bytes, never stored as the document's self-hash. Target1/new source/result documents can be checked using it without changing Store/native legacy hashes. Existing Reference id/schema/revision remains the exact logical calibration reference for Targets and results. A finalized source reference additionally binds its LiveAcquisitionIdentityV1; it has no logical revision.

Source provenance inputs are Target ID then captured active Rig ID if present/resolved. Dataset provenance inputs are Target ID then canonically ordered source artifact IDs. Camera inputs are Dataset,Target; Rig inputs Dataset,Target,LEFT,RIGHT, exactly as payload references. Unresolved plugin logical source calibration remains provenance with absent artifact, never an invented input. Every graph load checks finalized state, exact type/schema/native hash/document digest and full upstream content/roles/geometry; malformed versions/fields/counters fail before solve/activate.

**P03 — Canonicality.** Keep strict canonical UTF-8 JSON, sorted unique object keys, exact required/known fields, finite round-trip numbers, exact integer range/signedness, string enums, explicit null for absent optionals, canonical arrays and coordinate ID/name representations. Duplicate keys, unknown enums, bad point IDs/lengths, mismatched effective mm geometry, wrong source-record digests or partial populations are corrupt. Document1 codecs remain byte-exact. Hash equality does not prove physical authenticity.

**P04 — Seal then reservation.** No live source artifact or source logical revision SHALL be reserved while collecting. After producer retirement, admitted-tail drain and complete RAM validation/encoding, the publication Job SHALL linearize cancel versus persistence entry. If entry wins, call ordinary Store::begin once for the new source type, append exactly one prevalidated sealed packet, finalize and typed readback once. Return a finalized artifact reference only after readback. There is no observation journal/normal image persistence. Initial Target already exists; downstream calibration revisions are independently reserved by their own result publications. Distinguish operational identity, artifact allocation, logical revision and immutable finalization everywhere.

**P05 — Crash/retry.** Crash before persistence loses RAM and cannot be represented as a completed source or exact durable attempt inventory. Restart generates a new incarnation, invalidates handles and reports evidence unavailable; no automatic resume/recollection. After Store reservation, incomplete evidence stays nonusable. A lost postcommit reply is discovered as the same immutable source by bounded source enumeration/identity verification, not a new acquisition. No automatic reappend/re-reserve on a failed attempt. Explicit recovery addresses that exact artifact and may finalize only a full sealed one-packet source under exclusive ownership. Generic recovery SHALL reject the new type unless it invokes that typed gate. Zero chunks, incomplete packets, unclosed counters and unknown terminal boundary SHALL not become success.

If Store finalization commits but typed readback fails, report failed validation/integrity with that artifact ID retained for diagnosis; a generic FINALIZED row alone is not a usable source or successful Job result. Never rewrite the immutable packet to make readback pass. Normal publication reserves/appends/finalizes once; later explicit typed recovery is a separate operation on the same object, not a second acquisition.

**P06 — Revisions and metadata.** Targets retain existing allocation semantics. Schema2 Dataset/Camera/Rig use exact approved kind/schema whitelist and new logical series initially. Same-series revision allocation must enforce descriptor schema as well as kind and never reuse a number, including after failed publication. Existing metadata schema2 tables appear sufficient because registry rows reference descriptors containing schema and no new persistent operation index is required by this contract. No DDL is prescribed. Implementation must prove old-project opening/rollback and type/schema invariants; any additional index/persistent request history requires separate additive migration review, never an unannounced version bump or old-row rewrite.

**P07 — Identity reuse/import.** Duplicate input source identity, identical immutable source/hash under aliases, or repeated source refs is duplicate population and rejected. Same identity with conflicting bytes is corrupt. Identical imported objects may be reused once; imports preserve exact dependency identities/hashes and source identity. Relocating a project or allocating a container ID does not create independence. No cross-project global uniqueness/authenticity claim is made. Rewritten references produce different documents and cannot be smuggled under the original source identity. Import implementation is not required merely by this draft, but all loaders/builders enforce these referenced-population rules.

**P08 — Activation/snapshots.** Rig2 activation is explicit, metadata-only, exact-graph/schema-dispatched and validates discovered physical parent/components/roles/dimensions plus supplied mode/crop identity. Preserve original source calibration and captured active binding separately; parent/children agree and remain stable. Unknown original artifact/hash remains unknown. Snapshot before acquisition ownership/start, no producer DB lookup. Later activation/clear never changes an in-progress source or historical capture; future capture snapshots the new exact ID/schema/revision. FPS/packing alone are not geometric compatibility; equal dimensions alone do not prove equal crop/binning. No physical validation follows from these software checks.

**P09 — Legacy clients.** Existing schema1 builders/readers remain strict and unchanged. New daemon legacy calibration_info/activate/active paths reject a requested or active schema2 object as incompatible; legacy list must not encode new entries as schema1 and may reject the mixed-version listing with an explicit versioned-API requirement. Clear remains metadata removal with no graph reinterpretation. New v2 endpoints supply exact typed info/list/active/activate. CaptureStart gets an additive accepted-calibration-schema list: absent/empty means legacy1; active2 requires explicit2 before RawCapture creation. Old daemons/readers fail unsupported commands/schemas explicitly; clients never fall back to Recording or invent a successful empty result.

## ADR-035: add explicit live lifecycle, Job and public surface

**C01 — Operations.** Public pure DTOs SHALL add capability, live Start/Status/Finish/Cancel, DatasetBuildV2, CameraSolveV2, RigSolveV2, Info/List/Active/ActivateV2. Old DatasetBuild raw_capture_artifact_ids and old DTO/wire meanings remain unchanged. Full observations/points/images stay outside the 4MiB control plane. Version/presence absence is preserved through C++/Python/Protobuf; unknown versions fail. Full source validation and data export are bounded daemon work/data-plane concerns, not unbounded control-thread graph walks.

**C02 — Handles/idempotency.** Handle is `(daemon_incarnation, project_epoch, generation, acquisition_identity, operation_id)`. Start request has exact selected device, expected discovery/profile generations, exact Target reference, roles, admission/resource configs and client nonce. Idempotency is scoped to authenticated principal+project+incarnation+nonce and a complete request digest; equal retry returns the same operation, changed params conflict. Retain at most one active live operation and at most256 compact terminal receipts; never evict an active owner. Expired/old-incarnation handles fail stale/not-found. Cross-restart exactly-once Start/Finish is not promised; discovery of finalized sources is separate and cannot implicitly recollect.

**C03 — Ownership phases.** Start validates/reserves bounded resources before acquiring the existing camera lease and rechecks generations atomically. Readiness is first structurally valid pair. Finish closes admission at a producer boundary, stops only the owned source, drains finite admitted tail, seals RAM, and creates at most one queued publication Job. No queued Job holds cameras. Queue rejection is explicit and leaves sealed RAM under its owner until explicit retry/cancel. Project/device/role changes return bounded BUSY when ownership would be invalidated. A client disconnect/wait timeout does not cancel/transfer ownership.

**C04 — Cancellation/error rule.** One serialized cause/entry latch decides cancel versus persistence. Before entry cancellation prevents Store allocation; already observed first failure wins over later cancel. Once entry wins, actual nonpreemptive Store success/failure wins; no postwrite cancellation check hides finalized success. Late faults remain bounded secondary diagnostics. Cancelled queued tasks need RAII cleanup even when Job manager never invokes the task. Cancelling a known Finish Job uses this same state/authority, not a competing terminal path. Cleanup/logger exceptions cannot replace primary cause. Failed retirement retains ownership visibility; never acknowledge returned leases/mappings/processes before actual retirement.

For a live Finish Job, generic Runtime cancel_job SHALL resolve Job ID to the owning operation and participate in the same locked entry/cancel decision before token delivery. A token-only path is insufficient. Shutdown must request operation cancellation before retiring its Job owner, while queued-task destruction still releases the RAII permit. Entered Store I/O is never falsely declared preempted. Deterministic tests shall force both latch orderings and queued-task destruction.

**C05 — Helper/IPC.** Proposed helper is one daemon-supervised analysis process with no device/Store/project/activation authority. Request/response validate IPC version, bounded lengths, source identity/project/incarnation/generation/request ordinal, target/config digests, layout/extents and point limits. At most one request/in-flight result; at most one pending pair in daemon. Use retained immutable BufferViews or a bounded copied export; no existing export/zero-copy functionality is assumed. Helper performs effective configured M2 detection and hashing/conversion outside producer; helper-local thread settings and actual defaults are recorded. Deadline/crash/memory/malformed replies fail explicitly; no hidden helper restart/RAW fallback.

**C06 — Resource gates.** Initial proposals: stride60; duration≤300s/eligible≤600; camera-records≤1200/points≤256/encoded-record≤64KiB; source-heap≤32MiB/document≤32MiB/publication-workspace≤128MiB/added-daemon target≤192MiB; helper RSS target≤256MiB/address-space candidate512MiB; ledger≤4096 compressed entries plus≤600 candidate decisions and reserved seal entries; helper request deadline2s; one source/analysis/persistence slot until actual retirement. Limits include capacities and codec copies and are jointly checked against actual target/config before acquisition. Oversized config is rejected, not silently reduced. At a runtime cap close admission with an explicit bound/failure and preserve every already admitted record. These are qualification proposals, not measured Q6A guarantees.

Status≤16KiB/≤2Hz/one request; proposed controlled-software ACK p99≤100ms and interruptible source/helper retirement≤3s are future measured gates. Status/cancel SHALL not acquire blocked Store metadata locks. UI heartbeat does not depend on a control request finishing. Arbitrary entered OS I/O has no claimed finite bound; retain that owner/slot and report persisting/incomplete. Camera FrameSet rate, candidate admission, detector completion, hash/conversion, persistence bursts and UI rates are separately measured. No average or passing software CI establishes physical timing/throughput or microSD causality.

**C07 — Candidate wire map, not allocation authority.** At inspected HEAD Request commands stop at47, Response payload fields at18, CaptureStart has devices1. Candidate additive Request fields48–59 below and Response19–23 are unused in this source; recheck live descriptors before a separately authorized implementation. No existing tag is renumbered/reused.

| Request field | Candidate tag | Result |
| --- | --- | --- |
| calibration_capabilities_v2 | 48 | Response19 capabilities/type+document+policy support/limits |
| live_calibration_start | 49 | Response20 generation-scoped operation receipt |
| live_calibration_status | 50 | Response20 compact states/counters/errors/retirement |
| live_calibration_finish | 51 | existing result_id Job ID plus Response20 receipt |
| live_calibration_cancel | 52 | Response20 acknowledged intent/current actual outcome |
| calibration_dataset_build_v2 | 53 | result_id Job; exact finalized live source refs/Target1/config |
| calibration_camera_solve_v2 | 54 | result_id Job; exact Dataset2/role/heldout/new-series request |
| calibration_rig_solve_v2 | 55 | result_id Job; exact Dataset2/ordered Camera2 refs/rig frame |
| calibration_info_v2 | 56 | Response21 typed compact InfoV2, no populations/points |
| calibration_list_v2 | 57 | Response22 bounded entries/cursor |
| calibration_active_v2 | 58 | Response23 optional exact binding, absent is explicit |
| calibration_activate_v2 | 59 | explicit exact Rig2/device/expected-generation validation |

CaptureStart candidate field2 is `repeated uint32 accepted_calibration_schemas`; empty means1, new clients explicitly supply1,2. Pure service/C++/Python/CLI equivalents must preserve this boundary. List request has limit≤128/cursor and bounded metadata scan≤1024 rows per response, with continuation cursor even when no matching entry occurs; use a bounded Store query rather than materializing Store::list. No new index is assumed. Existing generic job_cancel/clear semantics remain available subject to the same lifecycle/version guards. No Plugin C ABI/packet catalog change is proposed.

## ADR-036: extend live presentation, preserve public-client-only boundary

**U01.** Retain seven stages and public M6 client ownership. Add capability-gated Live observations/no images saved beside Recorded captures; display actual physical roles, target measurement presence, source-ready, candidate/admitted/detected/no_target/rejection counts and draining/persisting/errors. No QML detector, Store, camera ownership, hidden lineage database or quality PASS.

**U02.** Only finalized source artifacts are selectable inputs. Resume from exact immutable refs; process restart may make unfinished ephemeral operations unavailable and SHALL NOT display durable interrupted-source rows that this design never wrote. Reconnect queries valid daemon handles; source discovery is separate. Stage navigation/disconnect does not cancel daemon work. Stale operation/project results never replace current selections. Activation is explicit/separate; physical-role writing belongs to separately authorized #67.

**U03.** Optional preview reuses ADR023's bounded local lease branch and decimation, separate from admission/retained records. No full-resolution remote Protobuf frame stream or shared-filesystem transport promise. Async requests and independent UI heartbeat remain required during blocked helper/Store; source2 compatibility messages are explicit. #68 implementation is not authorized by accepting this draft artifact.

## ADR-037: retain physical route; add an explicit limitation

**M01.** Existing decision4 remains unchanged: at least three genuine real ChArUco acquisitions/datasets on an unchanged rig, user-executed daemon restarts and disjoint authoritative RawCapture content-hash sets. No-pixel live sources are not an accepted substitute. Efficient live software calibration can be qualified separately while live-only M8 equivalence remains BLOCKED.

**M02.** Preserve physical parent/LEFT-RIGHT identities/mode/optical frames, printed/mounted active target extents and vector/layout provenance, truthful unknown clock/exposure uncertainty and stationary-pose guidance. Source IDs/nonces, corners or content digests do not establish physical origin/independence or detector reproduction. Repeated static content can be genuine; changed metadata can conceal clones; no automatic PASS rule resolves either ambiguity.

**M03.** Preserve all SIX ordered independent fixed-model cross-dataset evaluations, estimating only mono target pose while intrinsics/stereo remain fixed, plus separate real activation/historical RawCapture binding/replay before/after active changes. Within-dataset M4 held-out evidence is not physical independence. No image replay is available for a normal no-pixel source; report unavailable, not PASS.

**M04.** A future alternative needs separate owner/independent evidence and explicit adoption before replacing any RawCapture obligation; rejected/unproved equivalence retains the binding route and BLOCKED live gap. No automatic forensic retention, hardware action or quality threshold. M8a `final_m8_acceptance` remains NOT ESTABLISHED; M8b is a separate real-evidence decision. MicroSD freeze cause remains OPEN.

## Amendment scope and approval gate

Exact additions: L01–L10 extend ADR028 source/ingestion/keys/selection without changing legacy paragraphs; P01–P07 extend ADR030 decisions1–5/12 and P08–P09 decisions6–9/12; C01–C07 extend ADR035 decisions1–6/8/11–12/consequences; U01–U03 extend ADR036 decisions5–6/consequences; M01–M04 clarify ADR037 decisions4/6–8/11/13 while retaining its physical gate. No ADR number/status is allocated. ADR021/023/025/026/027/029/033 and frozen architecture remain binding.

Coordinate calibration detection/dataset/solve/artifacts/API/workspace and v0.3/M8 explanations if the owner later authorizes normative repository work. An unresolved incompatibility requires a decision, not code around the contract. Require decision report N01–N16 compatibility/resource/concurrency/storage/public-client oracles and representative R1 sensitivity in future implementation, plus all applicable source/CI/independent gates. #102 fixes remain unaccepted until their own review. This draft neither executes those checks nor adopts itself nor starts #66.

## Proposed lifecycle and Store/Job rationale

The following explanation is transcribed from the reviewed R2 report. It describes
candidate semantics, not runtime evidence. Earlier pending integration statements
in historical artifacts are superseded by independent integration acceptance of
the pinned baseline; #102 implementation acceptance remains a separate gate.

State sequence: `ACCEPTED → STARTING → COLLECTING → STOPPING/DRAINING → SEALED_RAM → QUEUED_PUBLICATION → PERSISTING → FINALIZED`, plus explicit cancelled/failed retirement states. The operational handle contains incarnation/project epoch/generation/acquisition identity, not a finalized artifact reference. Source-ready means first valid pair, not target detection or stored data.

Finish is idempotent within retained daemon operation history. It closes admission at a producer barrier, retires camera ownership, drains admitted work, validates the complete ledger and builds bounded canonical packet bytes. It records one publication Job ID; concurrent/repeated Finish returns the same ID. If Job submission fails before a Job exists, keep sealed RAM in an explicit queue-rejected state; a repeated Finish may submit once after capacity returns, without a Store write or new acquisition. No queue retry loop.

Publication entry is one serialized cancel/entry decision immediately before the first Store reservation. Cancellation winning before entry means no source artifact is reserved/appended; source RAM is discarded only after owned callbacks/helper retire. Once entry wins, reserve at most one ordinary source artifact, append exactly one sealed document, finalize then typed readback. Cancellation no longer interrupts this attempt. Generic cancel_job for a live Finish Job must route through this same entry latch, not merely race an independent token. Do not perform a late token check that hides a finalized result. Actual Job manager checks cancellation before a task, then records task return as completed without a postreturn cancellation check. It drops a cancelled queued task without invoking it: queued publication therefore needs a RAII permit whose destructor retires the operation/slot/RAM correctly. This is a concrete future implementation obligation, not a claim that existing Jobs already manage live state. If Store finalization commits but typed readback fails, retain the artifact identity and explicit integrity/validation failure; it is not a usable successful source and immutable bytes are not rewritten.

| Crash/fault point | Honest outcome and allowed recovery |
| --- | --- |
| During collection/before seal | No durable source; corners/counters are lost. Restart invalidates handle; cannot enumerate/count all interrupted attempts or invent a terminal boundary. |
| Sealed RAM but no Store reservation | Still no durable source. Old operation is unavailable after restart; never fabricate completed evidence. |
| After entry/Store reservation, before full packet commit | Incomplete/recoverable ordinary artifact may remain. No logical source revision exists. No success ACK. |
| Complete sealed packet, journal/DB/finalize interrupted | Only typed verification of the whole one-packet sealed source and exact dependencies may recover it. Never promote an unsealed prefix or omit a valid unindexed tail. |
| Finalize committed, reply lost | The source remains finalized; discover by bounded source listing and immutable acquisition identity, verify hash/graph, reuse the same object. Restart does not create an independent acquisition or cancel it. |
| EIO/ENOSPC/SQLite temporary failure | Preserve first error and genuine retryable bytes; explicit failed/incomplete state. No silent quarantine, duplicate append or automatic new artifact. |

Generic source recovery must reject this type unless routed through its typed sealed-source gate. A typed recovery path needs exclusive artifact ownership, complete bytes and valid ledger before finalization. Zero chunks cannot be finalized at this baseline. Existing Store append holds its metadata mutex across file/journal I/O; new live status/cancel must therefore use independent in-memory snapshots/latches, not Store reads or an unbounded global control join. Do not promise all unrelated legacy metadata calls remain bounded during blocked Store I/O.

#102 F2 concerns valid-journal operational failures misclassified as invalid at this HEAD; F1 concerns Session first-error/readiness exception consistency. Those defects are source-confirmed, not shown to cause the reported Q6A freeze. Its active candidate is not accepted. Future live implementation must reconcile accepted F1/F2 corrections or explicitly approved containment, without editing worker33's files or assuming its unreviewed candidate is fixed.


## Additional source-derived proof obligations

The existing Job manager clears a cancelled queued task while holding its own
mutex. A future queued-publication RAII guard must therefore make only bounded
state/latch transitions there: no join, Store call or reentry into the Job manager.
Do not hold the live entry latch while entering the Job manager. The shared-cancel
semantics in C04 require a demonstrated lock order and both race orderings, not a
claim that current Jobs already implement the proposed operation state machine.

Typed dependency traversal and source listing must remain bounded and reject
cycles/conflicting identities. In particular, captured active-rig provenance can
link earlier calibration graphs; its history cannot justify unbounded recursion.
The initial no-DDL inference remains conditional on bounded graph/import/listing,
cursor and activation proof. No new depth limit, migration or persistence index is
silently chosen here; unresolved contract conflicts require CHANGES REQUIRED
before adoption or conflicting implementation.

## 9. Future positive/negative acceptance matrix

**Specifications, NOT executed tests.** Use actual daemon/Store/public-client seams, independent expected traces, bounded gates and release-before-join cleanup. Existing regression families should be extended only where they lack the decisive oracle.

| ID | Positive control / negative or adversary | Required oracle |
| --- | --- | --- |
| N01 leases | Single live owner; both ordinary/projected/live conflict orders and simultaneous clients | One physical start, BUSY conflicts, atomic generation recheck, no queued lease or double stop. |
| N02 generations/lifetimes | Normal finish/restart; stale result, changed project/profile/role, late callback | Old result cannot mutate/publish/activate/access freed state; exactly-once owner/mapping retirement. |
| N03 sampler | Scripted continuous input; blocked helper, replacements, memory/ledger exhaustion | Producer advances without analysis wait; exact key/reason accounting, fixed capacities, no source gaps disguised as policy skips. |
| N04 source/provenance | Valid software pair; missing/duplicate child, physical swap, backward/incomparable clock, layout overflow | Correct source/error distinction, no detector on invalid bytes, unknown exposure retained, not zero/hardware sync. |
| N05 M2 | Blank/partial boards; unsupported layout/exception/malformed IPC | no_target versus error, exact IDs/mm geometry and digests, no empty successful observation. |
| N06 selection | Equal-descriptor and non-equal goldens above; reordered sources/records, duplicate keys, half-round/overflow | Exact keys/ranks and failure without partial mutation; legacy embedding relation proved, x86/ARM exact discrete outputs. |
| N07 stereo population | One side selected with opposite successful but unselected; remove opposite record versus no_target | Correct union/physical-ID intersection; missing record corrupt, successful absence typed ineligible. |
| N08 held-out | Same training with held-out-only perturbation; contaminate training split | Preliminary mono unchanged; fixed intrinsics, exact partitions and three stages; mutant leakage caught. |
| N09 numerics | Legacy and v2 adapted identical population | Model policies/tolerances unchanged; no Checkerboard stereo or optical PASS thresholds. |
| N10 cancel/Jobs | Cancel before queued task, before entry, after entry; real write success/failure | No Store reservation preentry; dropped queued task releases RAII slot; postentry actual result wins; primary error consistent. |
| N11 persistence/crash | Full source positive; EIO/ENOSPC/SQLite failure and every seal/journal/rename/DB/finalize/ACK boundary | One full sealed packet or explicit incomplete, no unsealed recovery/duplicate append/quarantine of valid retryable bytes; exact readback. |
| N12 schema/activation | Typed source→Dataset2→Camera2→Rig2 plus exact activation; wrong target/hash/schema/device/mode | Exact graph/hash/series schema validation and preacquisition rejection; immutable historical binding. |
| N13 legacy/import | Retained schema1 documents and RawCapture1/2/3; alias/clone/identity conflict, old peer | Byte/hash/replay identity, explicit unsupported handling, duplicate population rejection, no old object rewrite. |
| N14 snapshot | Collect under rev1, later activate/clear rev2 | Source uses captured rev1/original provenance; future capture uses new binding; historical replay unchanged. |
| N15 helper/UI | Valid real public SDK path; helper kill/hang/malformed reply, blocked Store, disconnect/UI destruction | No hidden camera/CV/Store authority, bounded control/UI heartbeat, stale replies fenced, no zero-copy/cleanup fiction. |
| N16 M8 false claims | Three datasets from one source, nonce-relabelled corners, static repeated content | Software reports evidence limits and live M8 BLOCKED; never declares physical equivalence or enables pixels to pass. |

Future R1 sensitivity must execute representative wrong-generation, source-gap relabel, early-ACK, duplicate-population or vector-index-join mutants through the production seam, fail the intended independent assertion, release gates safely and prove restoration. Document-consistency checks here are not those experiments.


## Validation and adoption limits

The matrix above specifies future implementation acceptance. It is not observed
PASS, code-mutant evidence, measured Q6A timing/throughput, physical calibration or
M8 equivalence. This PR changes only Markdown and the draft index. Applicable
local checks are links/applicability/faithful-transcription and architecture-policy
checks, descriptor collision checks and negative document controls. No production
build is claimed or substituted for required future implementation validation.

Before any normative adoption, owner decisions A–E must be explicit, all affected
contracts must be reconciled, and independent review must assess the exact PR
head and all five required CI jobs. Merge requires separate owner authorization
and exact-integration-SHA review. No automatic CAL-02, physical experiment,
threshold freeze, successor dispatch or cleanup follows.
