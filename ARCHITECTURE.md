# Architecture authority

[MANTIS_STUDIO_ARCHITECTURE.md](MANTIS_STUDIO_ARCHITECTURE.md) is the supplied, frozen v1 baseline and remains unchanged.

The [implementation map](docs/architecture/implementation.md), [milestone coverage](docs/architecture/milestone.md), and [ADRs](docs/adr/README.md) explain the original Skeleton v0.1 realization and subsequent additive milestones. A documented implementation limit is not permission to collapse or bypass an architectural boundary.

Release baselines: [v0.3 geometric calibration](docs/architecture/v0.3-geometric-calibration.md)
and [v0.4 laser acquisition L0](docs/architecture/v0.4-laser-acquisition.md).
See [README.md](README.md) for current implementation status.

CUT-0B consolidated the pinned Core and UI histories without changing architecture
authority or public contracts. Its exact-SHA software integration was independently
accepted at `e0a8652f2854a58aa611daa11c5c52051b303497` after five green CI jobs.
CUT-0C promotes that commit to the `develop` shared development line; `main` remains
unchanged. See [CUT-0B qualification](docs/architecture/cut-0b-integration-qualification.md)
and the [CUT-0C integration/review policy](docs/architecture/cut-0c-development-baseline.md).
Source-package and hardware acceptance gates remain separate.
