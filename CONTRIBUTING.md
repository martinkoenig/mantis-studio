# Contributing

Read the frozen architecture and relevant ADR before changing a public contract. Preserve the distinction between the Plugin SDK and the Client SDK.

For all new work, also follow the binding
[reliability, performance and validation standard](docs/architecture/reliability-performance-and-validation.md).
Measurement-critical and hot-path code is not accepted on happy-path functionality
alone: failure semantics, bounded resources, diagnostics and relevant performance
evidence are part of the feature contract.

Before implementation and PR handoff, follow the binding
[test-quality contract](docs/engineering/test-quality.md). It defines
risk-proportionate test design and adversarial implementer self-review;
the required checks below remain applicable.

## Validation and delivery

Use the risk-based [test-quality contract](docs/engineering/test-quality.md) to choose local checks and identify unique evidence requirements. Build and run affected Studio or headless targets as relevant; use sanitizers for affected ownership, threads, ABI and storage, focused regression tests, and repository formatting on changed C/C++ code. Critical streaming/hot-path changes require relevant performance evidence; recovery and persistent-state changes require realistic negative/interruption tests.

Do not duplicate the complete platform/configuration matrix locally by default. Required GitHub CI (including native x86_64/ARM64, Studio ON/OFF and applicable sanitizer and additional jobs) remains unchanged and must pass for the reviewed candidate. Run full local suites when the task's risk, a concrete concern or unique acceptance evidence justifies them. Equivalent CI evidence may satisfy the same property; unique required local/hardware/measurement evidence must still be obtained. Draft publication may precede final qualification if incomplete/failed checks are disclosed. Independent source/test review and applicable mandatory checks precede authorized integration.

Foundation headers must not expose Qt, SQLite, OpenCV, Eigen, CUDA or Vulkan types. First-party plugins may include only the public SDK plus their own dependencies. Studio and CLI must not link to runtime implementation libraries. The boundary contract test checks includes, while the Qt-disabled build proves dependency separation.

## Compatibility

ABI structures use size/version prefixes, fixed-width integers, UTF-8, opaque handles and explicit ownership. Never throw across C callbacks. Append compatible fields only with size-aware handling; incompatible changes require a new queried interface version. Application, ABI, schema, recipe, project and algorithm versions are independent.

Reserve removed Protobuf field numbers and names. Do not reassign them. Protocol messages describe services, not screens or widgets.

Architectural changes require a new ADR describing the problem, inadequate extension points, alternatives, compatibility impact and migration path. Feature convenience alone is insufficient.

## Issue memory and public engineering records

For each substantive authorized work package, maintain a work issue. Its description records current scope, verified status, blockers and acceptance gates; material findings, disproven hypotheses, exact-SHA CI and independent review decisions belong in **new chronological comments**. The coordinator synchronizes the issue body when current status changes. Update hub [#14](https://github.com/martinkoenig/mantis-studio/issues/14) and add one concise link-bearing comment only for material project-wide changes. Write all new GitHub issues, PR descriptions, comments, review decisions and repository documentation **in English**. Screen every public write for secrets, private infrastructure details and raw logs. Avoid closing keywords for PRs targeting `develop` when `main` is the default branch; close only after independent ACCEPT and authorized integration. See [ENG-KNOW-1](docs/engineering/knowledge-and-handoff.md).

## Pull requests

Explain the problem, behavior change and verification. Include a reproducible fixture for storage or plugin crashes. State which platforms were actually tested. Keep dependency updates separate from semantic changes where practical.

The repository is licensed under Apache-2.0; see [LICENSE](LICENSE).
