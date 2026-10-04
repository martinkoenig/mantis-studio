# CI platform checks

The workflow builds and tests both desktop and headless profiles on `ubuntu-24.04` (x86_64) and `ubuntu-24.04-arm` (native ARM64). A separate x86_64 job runs ASan/UBSan.

The ARM64 job uses a native runner, not an x86 build with an ARM label. Runner availability and repository billing/access policies are controlled by GitHub. See [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

The v0.2 code checkpoint `2927889` passed all five jobs in
[run 37192144976](https://github.com/martinkoenig/mantis-studio/actions/runs/37192144976):
Linux x86_64 and native ARM64 with Studio ON/OFF, plus ASan/UBSan. An earlier
GCC 13 failure exposed a missing explicit `<utility>` include in replay; it was
fixed on the feature branch and the complete matrix passed again. No failing
tests were suppressed. The workflow and its ordinary-tag trigger policy remain
unchanged; deterministic acquisition fixtures are included by CTest.

ARM runner success verifies compilation and fixtures, not a QCS6490 kernel,
physical OV9281 capture or sustained Q6A storage. Those remain pending user
execution. See validation.md for local evidence and the hardware procedure.
