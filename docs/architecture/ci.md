# CI platform checks

The workflow builds and tests both desktop and headless profiles on `ubuntu-24.04` (x86_64) and `ubuntu-24.04-arm` (native ARM64). A separate x86_64 job runs ASan/UBSan.

The ARM64 job uses a native runner, not an x86 build with an ARM label. Runner availability and repository billing/access policies are controlled by GitHub. See [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

The workflow is provided but has not been pushed or executed on GitHub during this delivery. Its existence must not be presented as evidence of a passing ARM64 build. Native Q6A capture support remains outside this milestone; the same headless architecture is intended to build on Linux ARM64.
