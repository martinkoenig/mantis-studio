# Contributing

Follow [AGENTS.md](AGENTS.md), the
[reliability, performance and validation standard](docs/architecture/reliability-performance-and-validation.md)
and [test-quality contract](docs/engineering/test-quality.md). Keep changes
focused; match validation to actual risk.

## Validation and delivery

| Risk | Local feedback before handoff | Integration evidence |
| --- | --- | --- |
| R3 — isolated documentation or non-semantic change | Accuracy, links, formatting, directly affected checks | Required CI; review actual diff and policy impact |
| R2 — service, SDK, configuration, ordinary UI/feature | Affected build, focused regression and integration tests; relevant UI/negative checks | Required CI and independent source/test review |
| R1 — acquisition, storage, calibration, provenance, ABI, security, concurrency or other critical path | Relevant integration, negative/recovery and sanitizer checks; affected modes and relevant performance evidence | Required CI and independent source/test review, all explicit critical-feature gates |

Escalate risk when changing a critical contract, validation or CI behavior.
Choose the cheapest test layer that proves the property. Use
[BUILDING.md](BUILDING.md) for actual presets and procedures. Preserve required
native x86_64/ARM64, Qt-enabled/headless, sanitizer and other CI coverage.
Run locally the configurations necessary to diagnose and verify the change;
complete duplication of the CI matrix is not a universal pre-push gate.
Explicit technical requirements and genuine hardware/release gates remain
binding. Format changed C/C++ code using repository style. Do not weaken tests
or change thresholds just to pass.

A draft PR may be opened with clearly marked incomplete checks. Implementation
handoff identifies tested revision, scope, commands, outcomes, risks and pending
CI. Independent review inspects actual code/tests and verifies required exact-head
CI and explicit issue gates. Reuse qualified evidence when relevant inputs are
unchanged; justify scope of reuse. Repeat affected checks or checks whose validity
is uncertain. Authorized integration respects branch protections, verifies actual
merged source and required integration checks; closure requires the whole scope.

## Compatibility

Preserve Plugin SDK/Client SDK separation. Foundation headers must not expose
Qt, SQLite, OpenCV, Eigen, CUDA or Vulkan types. First-party plugins may include
only public SDK and their own dependencies. Studio and CLI must not link to
runtime implementation libraries.

ABI structures use size/version prefixes, fixed-width integers, UTF-8, opaque
handles and explicit ownership. Never throw across C callbacks. Append compatible
fields only with size-aware handling; incompatible changes require a new queried
interface version. Application, ABI, schema, recipe, project and algorithm
versions are independent. Reserve removed Protobuf field numbers/names; never
reassign them. Protocol messages describe services, not screens.

Architectural changes require an ADR describing the problem, inadequate extension
points, alternatives, compatibility and migration. Feature convenience alone is
insufficient. Software integration does not imply hardware or release qualification.

## Issues and pull requests

Use the [issue and handoff guide](docs/engineering/knowledge-and-handoff.md).
Explain the problem, behavior change and verification; link the issue, include a
reproducible fixture for relevant storage/plugin crashes, state actual platform
coverage. Keep dependency upgrades separate from semantic changes where practical.
Public records are concise, sanitized and in English.

The repository is licensed under Apache-2.0; see [LICENSE](LICENSE).
