# Python Client SDK publication gate — definition only

**Status: future gate, UNCONFIGURED / BLOCKED for publication.** This defines the
separate TestPyPI and production PyPI decision required by
[Issue #75](https://github.com/martinkoenig/mantis-studio/issues/75). It installs
no publisher, workflow, environment, permission, account, credential or runtime
enforcement. Neither accepting this definition nor green CI authorizes an upload.
Actual TestPyPI compatibility, final naming/rights/notices and release approval
remain open. Production PyPI upload is not an Issue #75 implementation goal.

## Existing inputs and authority boundary

- [Package metadata](../../sdk/python/pyproject.toml) currently declares provisional
  distribution `mantis-studio-client`, import `mantis`, SDK `0.1.0`, Python >=3.10
  and `protobuf>=3.20,<7`. These are not cleared public names or an approved release.
  SDK version, wire protocol v1 and application version are independent; neither
  a wire-version constant nor an application tag determines the SDK version.
- [The backend](../../sdk/python/_build_backend.py) stages its explicit
  `SOURCE_FILES` plus canonical [mantis.proto](../../protocol/protobuf/mantis.proto).
  [MANIFEST.in](../../sdk/python/MANIFEST.in) includes the bundled schema/generated
  module and provenance. Portable provenance format 2 binds schema, generated
  module, descriptor, compiler distribution `protoc-wheel-0==21.11`, executed
  `libprotoc 3.21.11` and build protobuf `3.20.0`. These integrity hashes do **not**
  authenticate source, a builder or release authority.
- [BUILDING](../../BUILDING.md#python-packaging),
  [the artifact qualifier](../../tests/sdk/artifacts_test.py) and
  [read-only artifact CI](../../.github/workflows/python-sdk.yml) describe actual
  installed-artifact qualification, not registry publication. Source/build-tree
  imports cannot qualify a downloaded SDK. Linux evidence is not non-Linux,
  remote-security, physical-hardware or metrology acceptance.
- [Issue #81](https://github.com/martinkoenig/mantis-studio/issues/81) is the separate,
  future GitHub **product** release stage: exact accepted protected main, accepted
  #43/#44 main/cutover, #33 rights, #39 Qt and #73/#74 installed-product prerequisites
  remain intact. This SDK definition neither implements #81 nor grants tags,
  GitHub Releases, product distribution or promotion from develop to main.
  Owner-approved SDK release-tag/version alignment with that accepted source and
  cutover policy must be resolved before production; an accepted develop commit
  alone is not a release request.

[ADR-033](../adr/033-reliability-performance-release-gates.md),
[the reliability standard](../architecture/reliability-performance-and-validation.md)
and [test quality](test-quality.md) remain binding. Independent review of **source
and tests/evidence**, mandatory CI and explicit owner release go/no-go are separate
requirements. No actor may infer one from another.

## Required owner inputs — none supplied by this definition

Record these in one reviewed release request before any privileged setup/use.
An absent, ambiguous or changed input means BLOCKED, not a convenient default.

| Input | Required binding / unresolved decision |
| --- | --- |
| Naming and rights | Owner clearance of final distribution and import names (#40–#42), name availability on **each** index, current-artifact rights and complete license/notices (#33). Apache-2.0 choice alone is not rights clearance. Do not claim the already separately owned `mantis-sdk` name. Renaming requires new reviewed metadata/artifacts and applicable qualification. |
| Accounts and project control | Authorized owner separately establishes TestPyPI and PyPI accounts, secure account/recovery ownership and project roles under index requirements. Confirm project control or approve a new-project pending publisher. A pending publisher does **not** reserve a name; first upload creates the project and is itself privileged. No API-token fallback is authorized here. |
| Immutable release identity | Exact accepted source commit and tree, accepted protected-main/cutover alignment, tag name and tag object/peeled commit, SDK metadata version, prerelease/stable and uniqueness/version-order policy. Final tag syntax, version and triggering ref are **UNCONFIGURED**; do not tag `0.1.0` by inference. Reject rewritten/off-policy tags. |
| Publisher identity | Exact GitHub owner/repository, immutable repository/owner identity evidence, top-level workflow filename under `.github/workflows/`, reviewed workflow commit, job and destination-specific environment name. Current source repository is `martinkoenig/mantis-studio`, **not** an installed publisher registration. Final publisher workflow/job/environments are **UNCONFIGURED**. |
| Environment and ref authorization | Owner-approved approvers, separate TestPyPI/production approval boundaries, allowed events and exact release ref/SHA restrictions; verify effective protection and plan support. Bind the request, environment approval and execution to the same source, workflow, artifact manifest and destination. Missing protection/approval is BLOCKED; no bypass. |
| Cutover and lifecycle | After repository rename/transfer (#44), workflow rename/replacement, ownership or environment changes, revalidate source URLs, publisher tuple, immutable IDs, permitted refs and both index registrations. Old trust must not silently authorize the new context. Owner-controlled publisher retirement/offboarding must be reviewed; removing a project member does not remove a Trusted Publisher. |

PyPI's GitHub publisher registration matches owner, repository, workflow filename
and, when configured, environment. **This gate requires the environment binding**
for each destination, even though PyPI makes it optional. PyPI also checks the
repository owner ID. That registration is not a commit allowlist or proof that a
tag is accepted: release ref/SHA, event, workflow revision and artifact restrictions
need separately reviewed enforcement and effective environment/ref controls.
No such enforcement is introduced here.

## Qualification first; OIDC authority only after destination approval

1. **Readonly qualification.** Build and test from the recorded immutable source
   with reviewed pinned tooling and bounded outputs. No publishing secrets or
   `id-token: write` in build/test/PR jobs. Preserve actual command exits, complete
   relevant logs, dependency/tool identities and frozen input hashes. Reject
   untrusted PR/fork outputs, including artifacts relayed through a trusted run;
   neither an artifact name nor a successful upstream run establishes trust.
2. **Independent qualification decision.** Review source **and** tests, real
   negative/restoration evidence, and all applicable completed mandatory checks
   at their actual tested identities. Bind candidate, PR integration checkout and
   accepted resulting source explicitly; head-only green is insufficient. Current
   PR applicability is five [Architecture](../../.github/workflows/build.yml)
   checks (x64/ARM Studio ON/OFF and sanitizers), six Python artifact checks
   (x64 CPython 3.10–3.14 and native ARM64 3.12), two
   [Python control](../../.github/workflows/python-control.yml), two
   [installed Plugin SDK](../../.github/workflows/standalone-plugin-sdk.yml) and two
   [configured-toolchain](../../.github/workflows/toolchain-provenance.yml) checks.
   All 17 are required for that PR scope, not just the five protected checks;
   configured-toolchain is PR/manual-only, not a push job. Refresh actual workflow
   applicability, effective rules and app identities for the final release source;
   include any additional accepted release gates. Pending, absent, cancelled,
   skipped or failed mandatory proof is not PASS.
3. **Freeze the publishable set.** Record exact wheel/sdist filenames, sizes and
   SHA-256, metadata Name/Version/dependencies/classifiers/URLs/license/notices,
   source/tag/tree and build-run/checkout identities. Require independent-root
   determinism, clean installed wheel, unpacked-sdist rebuild and isolated frontend
   builds, canonical descriptor/golden-wire and both qualified protobuf edges.
   Retain installed control/Job and matching real local-daemon evidence with
   authentication, protocol/error/deadline and shutdown failures at their actual
   platform scope. Require `python -m twine check --strict` on **these** final
   distributions plus semantic metadata/provenance checks: twine rendering success
   is not a semantic, rights or source-authenticity verdict. Coherently rehashed
   wrong-source/metadata/provenance artifacts must still be rejected. Existing
   qualifiers and independently accepted evidence may be reused only when their
   inputs/context and claim are demonstrably unchanged; otherwise requalify.
4. **Separate destination go/no-go.** The authorized owner approves the exact
   request/manifest for TestPyPI, and only later separately for production after
   the compatibility step below. Qualification is readonly and cannot grant this
   approval. Approvals expire on source, workflow, artifact, identity, version,
   destination or relevant protection changes. Serialize the destination/project/
   version request before token acquisition; no parallel publishers for one set.
5. **Future minimal publication job.** Only the separately approved destination
   job/environment may receive job-level `id-token: write`; otherwise use minimum
   readonly permissions, no repository write authority. It retrieves only the
   reviewed immutable artifact set and rechecks identities/hashes, without running
   package builds, tests, setup scripts or untrusted checkout code under OIDC
   authority. Publisher tools/actions and provenance/attestation handling need
   their own reviewed immutable identities before activation; this document
   selects no workflow or security implementation. Protect both OIDC and minted
   tokens from logs, artifacts and other jobs; never persist long-lived upload
   credentials. An OIDC token authenticates the job, not package safety.

TestPyPI and production require **separate index-side publisher configurations**,
projects/control checks and environment approvals. Current documented OIDC
semantics use GitHub's issuer `https://token.actions.githubusercontent.com`,
audience `testpypi` for TestPyPI and `pypi` for production; revalidate against the
destination's published audience and supported claims before activation. Match
signature/issuer, validity and destination audience as well as the approved
repository/workflow/environment and execution context. Environment-bound subjects
do not themselves prove the release tag/SHA. PyPI exchanges matching OIDC for a
short-lived API token (currently 15 minutes); a tuple shared with other projects
can authorize them too, so review/restrict its full trust scope. Do not broaden
trust or substitute credentials when matching fails.

## Actual TestPyPI compatibility before production

After **separate explicit authorization**, a future implementation must upload
only the frozen SDK wheel/sdist to TestPyPI, record the real attempt/run and index
project/version/file identities, then download both from that index. Verify their
exact hashes against the approved manifest, metadata and provenance. In clean
external environments, install the downloaded wheel and independently rebuild and
install the downloaded sdist using the qualified pinned build tools. Verify actual
installed paths/hashes, imports and the applicable installed protocol/control/Job
and daemon compatibility tests on the supported matrix/runtime edges. Record
commands, exits and exact tool/runtime/source identities, not just HTTP success.

TestPyPI has a separate, potentially pruned database. Resolve SDK files only from
the approved TestPyPI project/version; obtain dependencies separately from reviewed
pinned sources. Do not combine TestPyPI and PyPI with `--extra-index-url` or resolve
SDK dependencies opportunistically from TestPyPI. An unavailable download or
missing compatibility leg is BLOCKED, not an assumed success.

The later production request must identify the **same qualified bytes** and their
TestPyPI evidence; upload from the retained verified set, not an index-to-index
promotion by name. Different test names/versions or regenerated production bytes
are not exact-byte proof: stop for owner disposition and applicable fresh
qualification/TestPyPI evidence. Production still needs its own owner go/no-go and
accepted tag/source/version alignment. After any authorized production upload,
verify the complete index file/hash set and clean download/install evidence before
recording completion. No public production run is authorized by this definition.

## Refusal and authorized recovery scenarios

These are decision requirements for future enforcement and independent negative
validation, **not executed publisher tests**. Refusal must occur before token/write
authority whenever the defect is knowable then; retain sanitized evidence.

| Scenario | Refusal evidence/decision | Authorized recovery only |
| --- | --- | --- |
| Wrong/off-accepted SHA, rewritten/off-policy tag, mismatched SDK version or application/wire version substituted | Immutable source/tag/tree/metadata comparison fails; owner request invalid | Correct request or reviewed source/tag/version decision, then affected qualification and fresh approval; never retag silently |
| Mandatory check missing, pending, skipped or failed, or wrong tested checkout/app | Independent source/test/CI gate refuses go/no-go; no token | Diagnose exact failed/missing proof; obtain applicable successful evidence at correct identities; no timeout/test weakening or blanket retry |
| Changed, mismatched or unreviewed artifact; coherently rehashed wrong metadata/provenance | Frozen manifest/hash, semantic/source checks or independent review fails | Quarantine changed set, retain original proof; correct and independently requalify affected artifacts, repeat compatibility and approval |
| Missing naming/rights/notices, account control, publisher tuple, environment restriction or owner approval | Required owner-input/destination gate remains UNCONFIGURED/BLOCKED | Authorized owner resolves exact missing input/setup separately; no guessed name, token fallback or protection change by qualification jobs |
| Wrong OIDC issuer/audience/identity/context, renamed workflow/repository, untrusted PR/fork or relayed artifact | Context/artifact trust gate refuses; index rejects nonmatching token claims (not a substitute for local gating) | Investigate sanitized claim/run identities without exposing tokens; owner revalidates legitimate configuration; never broaden trust to make the run pass |
| No actual TestPyPI upload/download/clean-installed compatibility, or different production bytes | Production go/no-go refuses; offline checklist/local artifacts cannot substitute | Separately authorize/execute the missing TestPyPI compatibility against final bytes and obtain independent evidence before new production decision |
| Duplicate or concurrent request | Destination/project/version serialization and ledger compare exact request/file hashes | Only one authorized attempt proceeds; identical completed set is recorded as already published after readback, not reuploaded; conflicting set is BLOCKED |
| Timeout, partial upload, duplicate filename or uncertain index result | Stop writes; outcome UNKNOWN/partial, not success; retain per-file results and index readback | Owner reconciles actual filenames/hashes and missing set. No blind retry or skip-existing masking. Only explicitly approved missing-file completion with identical bytes and revalidated gates, or separately approved new version with fresh qualification; never overwrite/delete/reuse filenames to hide failure |

Uploads are not a multi-file transaction. Stop on the first failure, preserve the
initiating error and inspect actual index state. No automatic retry, deletion,
yank, version change, tag rewrite or second concurrent attempt is authorized.
The final ledger must bind owner decisions, independent verdict/check identities,
source/tag/tree, exact file hashes, publisher/workflow/environment/destination,
run/attempt, compatibility evidence and actual final/partial index state. Keep
credentials and private logs out of public records. Hashes and attestations have
different claims; record what was actually verified, never infer acceptance from
an attestation's presence.

## Official semantics to recheck before activation

- PyPI: [Trusted Publishing](https://docs.pypi.org/trusted-publishers/),
  [publisher registration](https://docs.pypi.org/trusted-publishers/adding-a-publisher/),
  [pending projects](https://docs.pypi.org/trusted-publishers/creating-a-project-through-oidc/).
- PyPI: [using a publisher / destination audiences](https://docs.pypi.org/trusted-publishers/using-a-publisher/),
  [security boundaries](https://docs.pypi.org/trusted-publishers/security-model/),
  [identity/token internals](https://docs.pypi.org/trusted-publishers/internals/),
  [forbidden triggers and claim failures](https://docs.pypi.org/trusted-publishers/troubleshooting/).
- PyPA: [TestPyPI account/index/download isolation](https://packaging.python.org/en/latest/guides/using-testpypi/).

Those documents describe registry capabilities, not evidence of Mantis account
setup, configured enforcement, successful upload or permission to publish.
