# Virtual-rig serial CTest baseline

[Issue #164][issue] supplies a serial baseline and fixture attribution for
[#108][parent]. It does not qualify overlap. In this retained run the five tests
accounted for **1.77%–5.49%** of first-pass CTest wall time, depending on the
profile. There is one serial observation per profile, not a before/after pair.

## Evidence and measurement

Source: protected `develop` merge `2f307ad1b0554df73b1d28a99853765e16c3024d`,
[Architecture push run 46095962416, attempt 1][run]. In each of the five completed
jobs the checkout step's `git log -1 --format=%H` output matched this SHA. The
job names, runner labels and logged Studio configure arguments or sanitizer
preset values identify the profiles below. The ARM64 entries are the hosted
`ubuntu-24.04-arm` jobs; that identifies the runner, not the architecture of
every configured binary or the absence of emulation.

The four Linux matrix `Test` steps (#10) logged
`ctest --test-dir build/ci --output-on-failure`. The sanitizer step (#9) logged
`ctest --test-dir build/sanitizers --output-on-failure`, with
`ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1` in its step
header. These match the [pinned workflow][workflow-linux] and
[sanitizer step][workflow-san]. The commands have no selection, repeat, job-count
or timeout override; the step headers show no `CTEST_PARALLEL_LEVEL` or
`CTEST_TEST_TIMEOUT` override, and the actual log records alternate a Start with
its matching completion before the next Start. This establishes serial CTest
execution in each observed step, not single-threaded tests or cross-job isolation.

For each first pass, the bounded extraction matched the logged command and
`Test project` through the pass summary and `Total Test time (real)` to the
corresponding Actions step. It checked the ordered Start/completion pairs by
test number and name, the result ordinals and denominator, and exactly one
passing pair for each of the five source registrations. The table uses CTest's
reported real time, cross-checked against log timestamps, not Actions step/job
wall or a sum of all displayed durations. The family numerator is the sum of
its five displayed elapsed times; the denominator is that profile's first-pass
CTest real wall. The share is a ratio of rounded log readings, not a speedup
estimate. Test counts are observed passes, not configured membership.

| Profile | Job | Family tests / full first-pass tests | Family sum (s) | Full serial CTest wall (s) | Family share of wall |
| --- | --- | ---: | ---: | ---: | ---: |
| x86_64 Studio ON | [140827226279][x86-on] | 5 / 224 | 10.41 | 587.64 | 1.77% |
| x86_64 Studio OFF | [140827226296][x86-off] | 5 / 181 | 10.56 | 246.56 | 4.28% |
| ARM64 Studio ON | [140827226469][arm-on] | 5 / 224 | 14.79 | 752.77 | 1.96% |
| ARM64 Studio OFF | [140827226351][arm-off] | 5 / 181 | 14.45 | 313.58 | 4.61% |
| x86_64 sanitizers (Studio OFF) | [140827226067][san] | 5 / 181 | 37.29 | 679.22 | 5.49% |

The separate ON-only [UI step][workflow-ui] (#17) selects ten UI tests and uses
`--repeat until-fail:3`. The x86_64 and ARM64 logs each contain 30 ordered passing
pairs (three per selected test), zero virtual-rig outcomes, and CTest real walls
of **320.74 s** and **397.35 s**, respectively. Neither their time nor their
counts enter the baseline table. The matrix OFF repeat steps were skipped;
the sanitizer job has no such step.

### First-pass family observations (25)

CTest numbers are local to each job, not stable registration identities. The
limits in the last column are `TIMEOUT` declarations in the pinned source
([root registrations][root-reg], [storage registration][storage-reg]); they are
not recovered effective CTest properties or measurements of timeout behavior.

| Profile | CTest # | Test | Outcome | Elapsed (s) | Source timeout (s) |
| --- | ---: | --- | --- | ---: | ---: |
| x86_64 Studio ON | 66 | `virtual-rig-scene` | Passed | 4.35 | 40 |
| x86_64 Studio ON | 67 | `virtual-rig-contract` | Passed | 0.79 | 30 |
| x86_64 Studio ON | 68 | `virtual-rig-runtime` | Passed | 1.48 | 30 |
| x86_64 Studio ON | 69 | `virtual-rig-daemon` | Passed | 0.63 | 40 |
| x86_64 Studio ON | 215 | `virtual-rig-storage` | Passed | 3.16 | 40 |
| x86_64 Studio OFF | 23 | `virtual-rig-scene` | Passed | 4.39 | 40 |
| x86_64 Studio OFF | 24 | `virtual-rig-contract` | Passed | 0.79 | 30 |
| x86_64 Studio OFF | 25 | `virtual-rig-runtime` | Passed | 1.50 | 30 |
| x86_64 Studio OFF | 26 | `virtual-rig-daemon` | Passed | 0.64 | 40 |
| x86_64 Studio OFF | 172 | `virtual-rig-storage` | Passed | 3.24 | 40 |
| ARM64 Studio ON | 66 | `virtual-rig-scene` | Passed | 6.23 | 40 |
| ARM64 Studio ON | 67 | `virtual-rig-contract` | Passed | 1.15 | 30 |
| ARM64 Studio ON | 68 | `virtual-rig-runtime` | Passed | 2.08 | 30 |
| ARM64 Studio ON | 69 | `virtual-rig-daemon` | Passed | 0.74 | 40 |
| ARM64 Studio ON | 215 | `virtual-rig-storage` | Passed | 4.59 | 40 |
| ARM64 Studio OFF | 23 | `virtual-rig-scene` | Passed | 6.17 | 40 |
| ARM64 Studio OFF | 24 | `virtual-rig-contract` | Passed | 1.10 | 30 |
| ARM64 Studio OFF | 25 | `virtual-rig-runtime` | Passed | 2.01 | 30 |
| ARM64 Studio OFF | 26 | `virtual-rig-daemon` | Passed | 0.72 | 40 |
| ARM64 Studio OFF | 172 | `virtual-rig-storage` | Passed | 4.45 | 40 |
| x86_64 sanitizers | 23 | `virtual-rig-scene` | Passed | 18.69 | 40 |
| x86_64 sanitizers | 24 | `virtual-rig-contract` | Passed | 3.49 | 30 |
| x86_64 sanitizers | 25 | `virtual-rig-runtime` | Passed | 3.64 | 30 |
| x86_64 sanitizers | 26 | `virtual-rig-daemon` | Passed | 1.52 | 40 |
| x86_64 sanitizers | 172 | `virtual-rig-storage` | Passed | 9.95 | 40 |

## Fixture and resource ownership

These are **source findings**, not runtime telemetry from the archived passes.
The root `BUILD_TESTING` block registers the first four tests, and the storage
subdirectory registers the fifth on Linux. The [plugin target][plugin-target]
builds `virtual-rig` and generates its sibling manifest. The
[manifest][plugin-manifest] declares `in_process`; the [Registry][registry]
loads the approved rig in process. That does not describe every plugin that a
real daemon may discover. The virtual rig's own [producer][producer] is a
thread when not using virtual time. A [Session][session] additionally owns and
joins its producer/writer threads. Neither should be counted as a daemon child.

### `virtual-rig-scene`

`mantis-virtual-scene-tests` receives the module path. The
[scene fixture][scene-fixture] copies the library and manifest into a random
temp root and writes its own profile, with `hosts` scratch under that root.
Test-only manual-clock hooks drive optical/pixel checks.
[Recording and replay][scene-recording] add a gated Session and a real
`Scene.mantis` Store with two byte-exact replays after a profile revision.
[Renderer/ABI wall and CPU measurements, packet lifetimes and Linux self
FD/thread checks][scene-lifetime] are process-scoped. Keep serial: measurements
and Session/Store work still share CPU, memory and I/O.

### `virtual-rig-contract`

`mantis-virtual-rig-tests contract` [loads the module directly][contract], writes
`rig.json` in a [random temp workspace][rig-workspace] and sets
`MANTIS_VIRTUAL_RIG_PROFILE` in its own process. It uses no daemon, loopback or
Store. It checks ABI, opt-in enumeration, allocation/refcount bounds, simulated
timing/source loss and generation changes. The [async callback gate][contract-gate]
and [observed wait entry/cancellation][contract-cancel] use finite real waits;
the [entry point][rig-entry] checks retirement of host buffer references. This
has fewer shared resources in source, but keep it serial until those waits,
teardown and independence from any proposed peer are checked directly.

### `virtual-rig-runtime`

`mantis-virtual-rig-tests runtime` uses the same temp/profile helper, but
[copies the module and manifest, uses Registry `hosts` scratch and opens
`Recording.mantis`][rig-runtime]. Its source-paced Session cases exercise
GREY/Y10P capture, a five-second wait and stop, queue/drop assertions and an
injected writer exception. [Abandon/recover and two byte-exact
replays][rig-recovery] check the Store in process, not process-kill durability.
Keep serial: capture progress, writer contention, queue behavior and recovery
are sensitive to scheduling and shared storage.

### `virtual-rig-daemon`

[The Python harness][daemon-start] imports the build-tree SDK, owns a
`TemporaryDirectory` for `Project.mantis`, `rig.json` and `daemon.log`, and
starts the built `mantisd` twice. It asserts default-off discovery of the rig,
then opts in via a modified copy of [`virtual-rig-community.json`][community].
It [checks profile disappearance/reappearance, committed capture, replay,
high-rate profile discovery only and restart][daemon-capture]. The daemon
discovers build-tree plugins and [opens its own Store and diagnostics][service].
Keep serial: listener ownership and retirement are unresolved, and live
readiness, capture, replay and Store work share resource budgets.

The Python probe binds IPv4 loopback with port zero, reads the assigned number,
and **closes on leaving the `with` block**, before creating the child. Only the
number is passed via `MANTIS_PORT`, not an open socket.
[Daemon startup][daemon-app] constructs the runtime before
[`Socket::listen` binds and listens][socket]. There is no continuous reservation
from the probe to the first bind, or between the first child's shutdown and the
second bind on that port. The per-run token authenticates client requests; it
does not reserve the port. This is a source-level reservation gap, not evidence
of an actual collision. Each normal shutdown asserts a zero direct-child exit
within five seconds. [`OwnedProcesses`][owned] registers the direct `Popen`
child before waiting for readiness and polls, kills and waits on live tracked
children on unwind. It does not enumerate descendants or prove process-group,
socket or scratch retirement after cancellation. The success logs do not show
an independent no-survivor observation. No port experiment was performed.

### `virtual-rig-storage`

`mantis-rig-storage-qualification regression` reads
[`virtual-rig-trigger.json`][trigger] and uses [private plugin copies, a rewritten
profile, `hosts` scratch and `Recording.mantis`][storage-fixture]. The linked
fault-enabled Store and test-only Session sampler exercise [pipe gates and
process-local fault controls][storage-gates] as well as [saturation and
committed-prefix recovery][storage-prefix]. The [registered regression
mode][storage-entry] runs transient/prolonged delay and writer-error/short-window
verdict controls; it is not the standalone full-rate benchmark or collapse
mode. The fixture checks a 512 MiB free-space floor (not a reservation), and
[samples its own resources and checks teardown][storage-cleanup]. Keep serial:
actual Store persistence and timing/queue/resource assertions share the
underlying filesystem and host capacity.

The fixtures' project names do not refer to a shared project. The
[Store constructor][store] places `runtime.lock`, manifest, SQLite database
(WAL, `synchronous=FULL`) and object/journal/cache directories under the
supplied project root. Separate roots do not isolate filesystem capacity or
latency, page cache, CPU or memory. Process-local environment changes, clock
hooks, fault slots, pipes and self FD/thread checks likewise do not establish
host or descendant isolation.

## What remains unqualified

- Reuse [#107's scoped classification][inventory]: timing, recorder/Store,
  process-owning and unresolved tests stay serial by default. Its historical
  inventory counts are not the denominators above. As in [#37's retained
  execution review][executed], a checkout, command and ordered outcomes do not
  recover configured JSON-v1 membership/argv, fixtures, properties, effective
  environment, compiler/linker/sanitizer flags or binary-architecture proof.
- [#108's source/fixture acceptance][source-accept] did not qualify native
  execution or publication. The [later qualification capsule][capsule]
  records an incomplete, expired campaign. This baseline neither reopens it
  nor qualifies native overlap, retirement, collector publication or the whole
  parent. The standing
  [#109][host] host limits (one exclusive HEAVY slot, at most four compiler
  jobs and serial execution for exclusive/timing-sensitive work) remain.
- Before any separately authorized comparison, require a supported context,
  fresh finite admission and direct evidence for the specific proposed peers:
  an exact configured baseline and reconciled serial outcomes, owned scratch
  and continuous listener ownership or enforced exclusion, and cancellation and
  no-overlap negatives that observe direct and descendant retirement, released
  handles and cleanup. Any comparison must preserve configured and executed
  coverage, environment, assertions, timeouts and UI repetitions. Measure
  repeated like-for-like serial and proposed runs with pass/failure outcomes,
  dispersion, CPU/memory/I/O pressure and wall to the last required job; retain
  and classify invalid/failed observations, and keep a verified serial fallback.
  [ADR-033][adr] requires measured benefit without weakening validity. This
  sample proves neither a speedup nor a cause for any recorder failure.

The private recomputation retains the raw inputs, exact commands, digests and
canonical extraction for independent review. On disposable log copies the same
extractor refused a wrong checkout and a missing and duplicated first-pass
completion, each nonzero with no canonical output. Restoring the original
bytes reproduced the canonical result byte for byte. No configure, test rerun,
port experiment or scheduling change was used to produce this report.

[issue]: https://github.com/martinkoenig/mantis-studio/issues/164
[parent]: https://github.com/martinkoenig/mantis-studio/issues/108
[run]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/attempts/1
[x86-on]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/job/140827226279
[x86-off]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/job/140827226296
[arm-on]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/job/140827226469
[arm-off]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/job/140827226351
[san]: https://github.com/martinkoenig/mantis-studio/actions/runs/46095962416/job/140827226067
[workflow-linux]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/.github/workflows/build.yml#L20-L68
[workflow-san]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/.github/workflows/build.yml#L204-L231
[workflow-ui]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/.github/workflows/build.yml#L116-L120
[root-reg]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/CMakeLists.txt#L507-L523
[storage-reg]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/CMakeLists.txt#L31-L45
[plugin-target]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/CMakeLists.txt#L167-L176
[plugin-manifest]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/plugins/first-party/devices/virtual-rig.json.in
[registry]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/src/plugin-runtime/plugin-runtime.cpp#L469-L505
[producer]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/plugins/first-party/devices/virtual-rig.cpp#L254-L275
[session]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/src/device-runtime/device-runtime.cpp#L351-L398
[scene-fixture]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_scene.cpp#L284-L327
[scene-recording]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_scene.cpp#L609-L728
[scene-lifetime]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_scene.cpp#L759-L825
[contract]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L339-L357
[rig-workspace]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L28-L44
[contract-gate]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L657-L699
[contract-cancel]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L985-L1018
[rig-entry]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L1169-L1186
[rig-runtime]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L1026-L1079
[rig-recovery]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.cpp#L1124-L1165
[daemon-start]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.py#L11-L67
[daemon-capture]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/virtual_rig.py#L67-L112
[community]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/profiles/virtual-rig-community.json
[trigger]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/profiles/virtual-rig-trigger.json
[service]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/src/services/services.cpp#L113-L126
[storage-fixture]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/rig.cpp#L530-L566
[storage-gates]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/faults.cpp#L17-L86
[storage-prefix]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/rig.cpp#L1292-L1358
[storage-entry]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/rig.cpp#L1406-L1515
[storage-cleanup]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/storage/rig.cpp#L1174-L1232
[store]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/src/artifact-store/artifact-store.cpp#L645-L680
[daemon-app]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/apps/mantisd/main.cpp#L10-L70
[socket]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/src/platform/platform.cpp#L112-L148
[owned]: https://github.com/martinkoenig/mantis-studio/blob/2f307ad1b0554df73b1d28a99853765e16c3024d/tests/integration/capture_lifecycle.py#L211-L234
[inventory]: https://github.com/martinkoenig/mantis-studio/issues/107#issuecomment-6722097900
[executed]: https://github.com/martinkoenig/mantis-studio/issues/37#issuecomment-7182372600
[source-accept]: https://github.com/martinkoenig/mantis-studio/issues/108#issuecomment-6727412860
[capsule]: https://github.com/martinkoenig/mantis-studio/issues/108#issuecomment-6731774802
[host]: https://github.com/martinkoenig/mantis-studio/issues/109
[adr]: ../adr/033-reliability-performance-release-gates.md
