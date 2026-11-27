# Retained APT timings after the Studio dependency split

[#156](https://github.com/martinkoenig/mantis-studio/issues/156) measures what
remained after [#115](https://github.com/martinkoenig/mantis-studio/pull/115).
It reuses [#100's independently accepted 14-run baseline](https://github.com/martinkoenig/mantis-studio/issues/100#issuecomment-6701700730),
which identified archive fetching in three historical outliers. The eight jobs
below are new observations from two already-completed `develop` pushes, not a
recount of that baseline or a before/after experiment. Both pushes are **post-split**.

| Sample (UTC date) | Architecture push / attempt | Source SHA | `.github/workflows/build.yml` blob |
| --- | --- | --- | --- |
| A (2026-11-09, #115 merge) | [42543423621 / 1](https://github.com/martinkoenig/mantis-studio/actions/runs/42543423621/attempts/1), success | `fde69ac46fd62fd3663fa5513e8ab12663f6f22f` | `2b3b6a627069cac2d6cb47e6b9a41fb91a385502` |
| B (2026-11-27, #155 merge) | [46072024421 / 1](https://github.com/martinkoenig/mantis-studio/actions/runs/46072024421/attempts/1), failure | `2b4a66b31dcfb7ccc9eddcf11a9634cc8370e054` | `bb69825f6e28cc61d4bcb0570c1dd66d06637142` |

The Linux matrix, APT commands and Configure/Build/Test definitions match in
[A's workflow](https://github.com/martinkoenig/mantis-studio/blob/fde69ac46fd62fd3663fa5513e8ab12663f6f22f/.github/workflows/build.yml#L20-L68)
and [B's workflow](https://github.com/martinkoenig/mantis-studio/blob/2b4a66b31dcfb7ccc9eddcf11a9634cc8370e054/.github/workflows/build.yml#L20-L68).
The native runner labels are `ubuntu-24.04` (x86_64) and `ubuntu-24.04-arm`
(ARM64). Image versions changed between pushes: x86_64 `20261101.360.1` →
`20261122.23.1`, ARM64 `20261101.172.1` → `20261122.193.1`. Sample B uses
only original attempt-1 jobs, not the later successful attempt or its carried jobs.

## Dependencies (#3)

All eight steps succeeded. **Wall** is Actions `completed_at − started_at` (whole
seconds) and, after `/`, the log interval from the `Dependencies` command-group
start to `Prepare compiler cache` (#4). **Index** and **Archives** give the
rounded MB and seconds reported by each of APT's two `Fetched` summaries (not
exact byte counts). The four **log windows** partition the log interval; the
columns are defined below. Independently rounded windows need not sum exactly.

| Sample / native arch / Studio | Job | Wall API / log (s) | Index (MB / s) | Archives (MB / s) | Log windows: update / prefetch / archive / postfetch (s) | dpkg markers inside postfetch (s) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| A / x86_64 / ON | [128923183553](https://github.com/martinkoenig/mantis-studio/actions/runs/42543423621/job/128923183553) | 52 / 51.3 | 13.0 / 1 | 247 / 25 | 5.3 / 0.7 / 25.2 / 20.2 | 18.3 |
| A / x86_64 / OFF | [128923183757](https://github.com/martinkoenig/mantis-studio/actions/runs/42543423621/job/128923183757) | 34 / 34.9 | 13.0 / 1 | 216 / 10 | 3.9 / 0.4 / 10.5 / 20.1 | 18.8 |
| A / ARM64 / ON | [128923183829](https://github.com/martinkoenig/mantis-studio/actions/runs/42543423621/job/128923183829) | 50 / 49.9 | 15.0 / 4 | 206 / 12 | 8.3 / 0.7 / 12.0 / 28.9 | 24.4 |
| A / ARM64 / OFF | [128923183669](https://github.com/martinkoenig/mantis-studio/actions/runs/42543423621/job/128923183669) | 45 / 45.1 | 15.0 / 4 | 175 / 9 | 11.1 / 0.9 / 9.4 / 23.6 | 18.6 |
| B / x86_64 / ON | [140750983921](https://github.com/martinkoenig/mantis-studio/actions/runs/46072024421/job/140750983921) | 44 / 43.8 | 13.2 / 1 | 247 / 4 | 5.8 / 0.5 / 4.7 / 32.8 | 31.1 |
| B / x86_64 / OFF | [140750983944](https://github.com/martinkoenig/mantis-studio/actions/runs/46072024421/job/140750983944) | 35 / 35.4 | 13.2 / 1 | 217 / 3 | 5.6 / 0.6 / 3.1 / 26.1 | 24.2 |
| B / ARM64 / ON | [140750983841](https://github.com/martinkoenig/mantis-studio/actions/runs/46072024421/job/140750983841) | 95 / 95.3 | 15.4 / 4 | 206 / 57 | 9.8 / 1.1 / 57.3 / 27.0 | 22.8 |
| B / ARM64 / OFF | [140750983958](https://github.com/martinkoenig/mantis-studio/actions/runs/46072024421/job/140750983958) | 74 / 73.4 | 15.4 / 2 | 176 / 36 | 11.2 / 1.1 / 36.1 / 25.0 | 19.8 |

The sum of APT's two reported fetch durations accounts for about **11–64%** of
log wall across these eight jobs. The rest is not all installation: the logs
show work before the archive fetch and after it. For example, B ARM64 ON
reported 206 MB of archives in 57 s versus A's 206 MB in 12 s; B x86_64 ON
reported 247 MB in 4 s versus A's 247 MB in 25 s. The variation is observed,
not attributed to an image, mirror, cache condition or the split.

**Requested vs observed.** All eight jobs request the same 13 common roots,
including `libopencv-dev` and `libopencv-contrib-dev`; both OpenCV packages are
reported by `dpkg-query` at `4.6.0+dfsg-13.1ubuntu1` in all eight. ON adds eight
Qt6/QML roots and logs `Setting up` for all eight and `libqt6core6t64`. OFF
requests none of those roots and logs no setup for those packages, but **all
four OFF jobs** log `Setting up libqt5core5t64` (as do all four ON jobs). A
package setup line is evidence of package-manager activity, not a full inventory
of the runner, an explanation of the dependency edge, or evidence of runtime use.

## Separate Actions phases and outcomes

Whole seconds from Actions metadata: `Restore compiler cache` (#5), `Configure`
(#8) and `Build` (#9). Prepare/reset and the x86_64 OFF policy check are separate
steps, not included in these timings. Restore results are logged prefix fallback
(A) or exact key (B), not compiler hit rates or APT cache observations. `Build`
is wrapper-plus-build wall time, not directly timed compiler work.

| Job | Restore (s / lookup) | Configure (s) | Build (s) | CTest (#10) / job |
| --- | ---: | ---: | ---: | --- |
| 128923183553 | 2 / prefix | 3 | 384 | 205/205 / success |
| 128923183757 | 3 / prefix | 1 | 187 | 162/162 / success |
| 128923183829 | 3 / prefix | 5 | 239 | 205/205 / success |
| 128923183669 | 2 / prefix | 3 | 184 | 162/162 / success |
| 140750983921 | 2 / exact | 1 | 291 | 223/224 / failure |
| 140750983944 | 4 / exact | 1 | 216 | 180/181 / failure |
| 140750983841 | 4 / exact | 3 | 240 | 224/224 / success |
| 140750983958 | 2 / exact | 9 | 183 | 181/181 / success |

The B x86_64 failures were in CTest: `x1-pairing-stream-GREY-phase-drift-gap-recorder`
(ON) and `x1-pairing-stream-GREY-phase-jitter-right-recorder` (OFF). Neither
failed in Dependencies, Configure or Build. The later passing attempt does not
explain the failures or replace these samples.

## Reproduce from the retained logs

1. In a private scratch directory with `umask 077`, set
   `api=repos/martinkoenig/mantis-studio` and use each `run`, `job` and `sha`
   above. Retrieve only the exact attempt metadata and each job text log:

   ```sh
   gh api "$api/actions/runs/$run/attempts/1" > "run-$run-a1.json"
   gh api "$api/actions/runs/$run/attempts/1/jobs?per_page=100" > "jobs-$run-a1.json"
   gh api --allow-escape-sequences "$api/actions/jobs/$job/logs" > "job-$job.log"
   git rev-parse "$sha:.github/workflows/build.yml"
   ```

   Verify the push event, `develop`, source SHA, workflow path/blob, attempt,
   completed job ID/name/runner and step conclusions. Confirm the checkout SHA
   from `git log -1 --format=%H` **output** in the job log and the runner/mode
   in the Dependencies environment. Check that the step boundaries align with
   the log timestamps (about one second here), and that CTest and cleanup output
   are present. Do not substitute a missing log with another attempt's job.
2. Strip ANSI formatting locally. `Run case …` and its group show the **echoed
   shell script**, including both ON and OFF branches and both APT command
   lines. Inspect actual output **after** that group's `##[endgroup]` and
   **before** the next step. The index has `InRelease` records then `Fetched`
   and the first `Reading package lists...`. The install's second `Reading
   package lists...`, `Building dependency tree...`, and `Need to get … of
   archives` precede package `Get:` lines and a separate `Fetched` summary.
   Use the latter receipt for archives, not the request or disk-space estimate.
   Requested and fetched archive MB agree as printed in these eight inputs;
   that equality is not a general rule. B x86_64 OFF, for instance, shows an
   index receipt of 13.2 MB in 1 s and an archive receipt of 217 MB in 3 s.
3. Subtract consecutive log timestamps: `Dependencies` group start → first
   `Reading package lists...` after index `Fetched` (update proxy) → first
   archive-block `Get:1` after `Need to get` (prefetch gap) → archive `Fetched`
   (archive window) → cache preparation group start (postfetch). The first
   archive-block `Get:1` can be a mirror-list read. Within postfetch, subtract
   first package selection from the last selection/unpack/setup/trigger line
   for the **dpkg marker span**. This is a span of observed activity, not a
   process timer. The update proxy includes startup, and the prefetch gap can
   include update tail and install resolution. Postfetch includes remaining
   package work, hooks and the final OpenCV query. Exact process boundaries and
   finer attribution within those windows are unavailable.
4. On a disposable copy of B x86_64 OFF, replacing the archive receipt text with
   the index receipt (same archive timestamp) was rejected because it disagreed
   with the archive request. Truncation before cache preparation was rejected
   for a missing closing boundary. Pairing the attempt-1 record with actual
   same-SHA attempt-3 metadata was rejected for attempt mismatch; swapping in
   A's x86_64 OFF log was rejected for checkout SHA mismatch. All four controls
   exited 1 on the intended check; the untouched inputs then reproduced all
   eight rows (exit 0). No exact input was missing. Raw logs stay private.

## Limits

These hosted jobs were not matched for image, preinstalled state, APT cache
state or network conditions. There is no controlled cold/warm APT or build-cost
comparison, causal speedup, or authenticated package/dependency closure here.
[#127](https://github.com/martinkoenig/mantis-studio/pull/127) records configured
identity, not actual tool invocation or package authenticity;
[#145](https://github.com/martinkoenig/mantis-studio/pull/145) verifies isolated
incremental/cache **correctness**, not build cost. Those remaining measurements,
dependency closure and runtime work stay with
[#111](https://github.com/martinkoenig/mantis-studio/issues/111); the
[#130](https://github.com/martinkoenig/mantis-studio/pull/130)/[#39](https://github.com/martinkoenig/mantis-studio/issues/39)
hold and [#103](https://github.com/martinkoenig/mantis-studio/issues/103) are
unchanged. No installation, build, CI replay or workflow change was used to
produce this report.
