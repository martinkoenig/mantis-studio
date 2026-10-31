# CI-STAB-2 — Comprehensive test reliability and effectiveness qualification

**Status:** Planned, not implemented or accepted.
**Prerequisite:** Independently review/close the ongoing CI-PERF-1A recorder correction; do not interfere with its worktree or assume its root-cause hypothesis is proven.
**Policy:** [REVIEW-QA-1](review-and-test-qualification.md), [ADR-033](../adr/033-reliability-performance-release-gates.md), [existing v0.4 reliability audit](../architecture/v0.4-reliability-gap-analysis.md).

## Objective and non-goals

Restore evidence-based confidence in the **entire** registered test suite: correct assertions, deterministic error reporting and runtime behavior, realistic failure cases and reliable CI. Inventory **all** tests, even those that have never failed. Apply R1 deep review to correctness/data-critical families; proportionate checks to R2/R3 tests. Inspect related `src/` and plugin/ABI implementations whenever tests could signal a brittle or incorrect production design.

Not authorized: unrelated feature work, loosening contract/timeouts/queue bounds to hide failures, wholesale refactoring, inventing hardware/CI success, auto-advancing milestones or changing architectural contracts without approval. This is a temporary quality gate before resuming FS-1 and other feature work.

## Package sequence

**S2-01 — Inventory and risk register.** Enumerate current configured CTests with `ctest --test-dir <build> --show-only=json-v1`; include CMake registrations, Python/CI-only, optional hardware/HIL/GPU, Qt/UI and disabled tests. Reconcile actual selection with `.github/workflows/`. Map each test to source, actual asserted invariant, integration layer, configuration/runner, resource dependencies, isolation, R1–R3 risk and observed history. Historic test counts are reference data, never assumed current. Produce a ranked list of small, independent corrective packages with acceptance IDs.

**S2-02 — R1 test and production code audit.** Independently examine acquisition/recorder/pacing/queues, RawCapture/SQLite/durability/recovery/replay, projected light/F2 and safe-state ownership, plugin ABI/lifecycle, cancellation/threading/error containment, calibration identity/provenance, distributed authority, service composition and genuinely critical Qt/UI/GPU paths. Identify what each key assertion will catch, validate with representative controlled fault injection, and fix *verified* production defects as well as test defects. Preserve existing good coverage; do not duplicate well-qualified tests.

**S2-03 — Determinism and infrastructure.** Examine thread scheduling, future/destructor order, time assumptions, test resource contention, global state, temp paths/ports/filesystem/SQLite, process isolation, parallelism, environment/toolchain differences and sanitizer reporting. Replace brittle sleeps with controlled synchronization where appropriate; do not conflate a safety timeout with expected latency. Use carefully scoped stress/repetitions for diagnosis, not retry-until-green.

**S2-04 — Verification and CI coverage audit.** Confirm required Studio ON/OFF, Linux native ARM64, x86_64 ASan/UBSan, Python checks, CTest registrations, workflow filters/exit codes, diagnostics and failure-log preservation. Identify limitations: GPU tests in a VM, real Q6A/laser HIL, Windows/macOS and sustained media performance are not silently credited from software fixtures.

**S2-05 — Final qualification.** Complete relevant builds/full suites and focused concurrency/load/fault probes on final changes; collect exact SHA, environment, test selection/counts, commands/options, evidence and unresolved risks. Independently verify required GitHub Actions for the final candidate and publish the qualified invariant/incident matrix. No acceptance with failing or unverified required CI.

## Known leads; not established diagnoses

- [CI run 41188660291](https://github.com/martinkoenig/mantis-studio/actions/runs/41188660291) failed `x1-pairing-stream-GREY-phase-4000-recorder` on x86_64 Studio ON with explicit LOSSLESS queue saturation. It establishes a symptom; **storage latency, starvation, producer rate and cache causality remain unproven** from that run.
- Earlier fixture load, pacing and sanitizer incidents are recorded in [ENG-EFF-2A](../agents/repository-instructions.md) and the [v0.4 gap analysis](../architecture/v0.4-reliability-gap-analysis.md). Revisit the actual current source before reusing historical claims.
- The active worker has *reported*, but the reviewer has not yet independently verified, defects around throwing logger callbacks, future cleanup ordering and error precedence during abandonment. Do not mark them accepted or prove the queue incident from these findings.

## Completion gate

**SQ-01:** All current tests inventoried and reconciled to CI; each R1 invariant has mapped production code, assertion and real failure model.

**SQ-02:** Every R1 gap has a meaningful existing/new negative test or an explicit blocking investigation; source correctness and robustness reviewed independently.

**SQ-03:** Observed intermittent failures have evidenced root causes/corrections or precise externally bounded limitations; no suppression, re-run laundering or unexplained red check.

**SQ-04:** Fault, cancellation, saturation, storage, cleanup, replay and integrated scenarios are suitably deterministic and diagnostics preserve primary causes.

**SQ-05:** Mandatory local and remote configurations pass for exact reviewed commits; incomplete physical/GPU/platform evidence clearly marked.

**SQ-06:** Complete documented qualification and residual-risk register independently **ACCEPTED** before release of further feature packages. Multiple green repetitions support confidence but cannot prove absence of races.

Each implementation/correction package receives its own independent review and explicit ACCEPT before the next. Scope remains risk-based rather than promising mathematical certainty.
