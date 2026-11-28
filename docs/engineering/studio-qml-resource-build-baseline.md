# Studio QML resource build baseline

[Issue #161](https://github.com/martinkoenig/mantis-studio/issues/161) measures the
existing build, not a deduplication candidate. On one launcher-free Linux x86_64
Debug sample, the shared-input rebuild ran eight RCC generators and eight C++
compiles in **14.185 s wall**. C++ compilation accounted for **25.514 summed edge
seconds**, versus **2.793** for RCC. That is enough to justify investigating
why the same payload is compiled repeatedly. It is not a speedup estimate or a
recommendation to share registered resources.

## Source, graph and hosted evidence

The measured source was develop `5b5bdbdae33c4211f37b64c6fb8f5b15a9ec30ef`, tree
`a40b0c05e5ba9d32560e61f5bdf1a8069c6646d0`, root `CMakeLists.txt` blob
`df2f2b90e25b17985a0c33b536e55ae93645bf70`. The disposable source was extracted
from a Git archive; all 1,123 file identities, bytes and modes matched that tree
before and after the experiment, and file modification times were restored.

The retained x86_64 Ninja graph and the two hosted jobs are pinned to develop
`870f331a03615af0a8a29aeaefc560e43a861d1a` (tree
`7ed988712c55b92c43ad1dcc41f26d2aa4499d29`). The exact Git delta from that pin to
the measured source only added the APT phase report from PR #157. The root
CMake blob is identical. The retained graph and the fresh graph each have the
following eight `CUSTOM_COMMAND` outputs and eight target-specific CXX objects.
For each row, the local generated source is `.rcc/qrc_<resource>.cpp` and the
local object is `CMakeFiles/<target>.dir/.rcc/qrc_<resource>.cpp.o`; the actual
compile edge consumes that generated source.

| Resource | Target | Cold RCC / CXX (ms) | Timestamp RCC / CXX (ms) |
| --- | --- | ---: | ---: |
| `studio-ui` | `mantis-studio` | 347 / 3,068 | 322 / 3,241 |
| `studio-devices-ui` | `mantis-studio-devices-tests` | 401 / 3,055 | 368 / 3,086 |
| `studio-home-ui` | `mantis-studio-home-tests` | 407 / 3,404 | 347 / 3,209 |
| `studio-projects-ui` | `mantis-studio-projects-tests` | 333 / 3,069 | 358 / 3,102 |
| `studio-scan-ui` | `mantis-studio-scan-tests` | 338 / 3,074 | 361 / 3,246 |
| `studio-m0-ui` | `mantis-studio-ui-m0-tests` | 405 / 3,440 | 359 / 3,150 |
| `studio-role-ui` | `mantis-studio-x1-role-qml` | 340 / 3,144 | 332 / 3,249 |
| `studio-test-ui` | `mantis-studio-calibration-qml` | 316 / 2,942 | 346 / 3,231 |

[Architecture run 46086070525, attempt 1](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/attempts/1)
was a develop push. The [x86_64 job 140795808307](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/job/140795808307)
and [ARM64 job 140795808226](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/job/140795808226)
were both Ubuntu 24.04 Studio ON jobs. In each original job log, checkout step
2 printed the pinned SHA from `git log -1`, Configure was step 8, and Build was
step 9 before Test step 10. The step-labelled Build payload matched the
corresponding raw log slice. Each Build contained exactly eight named RCC
progress events and eight resource-object progress events, with no missing,
extra or repeated identities. Comparing the hosted path
`CMakeFiles/<target>.dir/build/ci/.rcc/qrc_<resource>.cpp.o` used the target and
resource identity, removing only the actual `build/ci/` segment. Hosted builds
used `ccache` and four-way Ninja parallelism. Those progress records corroborate
identity, not compilation misses or edge duration on either runner.

## Resource and compile inputs

The eight generated `.qrc` documents were byte-identical in the fresh build.
Their XML and the results of `rcc --list-mapping` independently agreed on all
**78** `:/ui/...` paths and corresponding source paths: prefix `/`, explicit
relative aliases, and no language override. All 78 resolved files were also
explicit inputs to each of the eight Ninja generation rules. The map binds
**1,810,951** source bytes per registration (including images, JS and `qmldir`,
not only QML). The sorted map of aliases, source paths, lengths and content
hashes matched across all targets and all phases. The timestamp experiment
changed no map or source content.

All eight graph commands invoke the same Qt `rcc` directly, differing in
`--name`, input `.qrc` path and `--output` path. None overrides the default C++
generator, compression algorithm/level/threshold, resource format version or
root in argv. The emitted C++ uses resource format 3 and references Qt's Zstd
resource feature. Each object contains a distinct `qInitResources_<name>` and
`qCleanupResources_<name>` (`-` becomes `_`) and a static initializer. The cold
C++ outputs were byte-distinct, but replacing only those init/cleanup names in
an analytical copy made all eight cold C++ files byte-identical. The eight
actual cold object digests were distinct.

Ninja and the compilation database agreed for each object after removing
only destination and depfile arguments. The compiler, ordered definitions,
include kinds and order, and flags were compared; the actual compiler
inputs reported by Ninja were the respective generated C++ and GCC's implicit
`stdc-predef.h` only. All eight use the same ordered common flags (`-g`,
`-std=c++23`, `-fPIE`, the five `mantis-options` warnings, then `-fPIC`) and each
has its own generated AUTOMOC include directory. After abstracting just the
resource source identity and that target-specific include path, the remaining
ordered flags form three groups:

- Studio and calibration QML use the same remaining flags and do not link Qt
  Test in the configured link plans.
- Scan, Devices, Projects, Home and UI M0 add the Qt Test system include,
  `QT_TESTLIB_LIB`, `QT_TESTCASE_BUILDDIR` and `QT_TESTCASE_SOURCEDIR`; their
  configured link plans include Qt Test.
- X1 role QML has those Qt Test settings but a different ordered project/Qt
  include list (e.g. no `src/render/include`, and Core precedes Quick). Its
  target links the calibration library rather than the full Studio model/render
  stack; other Qt and client usage is inherited. The file API confirms eight
  separate executable consumers, not a single linked registration owner.

Equal resource inputs and even name-normalized generated C++ do not establish
object, link or runtime equivalence. In particular, an alternative would have
to preserve the presence and lifetime of resource registration in each
executable, prevent dropped or duplicate initializers, and verify resource
lookup and QML behavior through the actual consumers. The object-only build did
not link or run any of these executables.

## Local timing and controls

Ubuntu 24.04.5 LTS x86_64; CMake 3.28.3, Ninja 1.11.1, Qt and `rcc` 6.4.2,
GCC/G++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1), `protoc` 3.21.12. The configured
profile was single-config Ninja Debug with `MANTIS_BUILD_STUDIO=ON`,
`BUILD_TESTING=ON`, `MANTIS_SANITIZE=OFF`, system C/C++ compilers and Python, and
explicitly empty C and C++ compiler launchers. The fresh build tree had no prior
Ninja log or objects; this is not a cold OS page-cache experiment. The single
`mantis-heavy-build` lease ran the configure, four explicit eight-object Ninja
invocations at `-j2`, and local controls, then exited successfully. Configure
was 2.241 s and is excluded from the phase walls below.

Phase wall is elapsed monotonic time around each Ninja invocation. Edge times
come from phase-local append deltas of Ninja's build log, mapped to producers
in the actual graph. Output aliases and multi-output rows were counted once
per command, with matching row metadata and counts reconciled to completed
verbose progress. Edge sums are overlapping elapsed durations, not CPU time,
wall-time components to add together, or savings. There were no missing
expected edges in either active phase, unattributed command rows, log-prefix
losses or primary build failures. Local compiles ran directly; launcher-cache
hit/miss counts do not apply.

| Phase | Wall (s) | RCC count / sum (s) | Resource CXX count / sum (s) | Prerequisite count / sum (s) | Log rows / commands |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fresh build tree | 96.566 | 8 / 2.887 | 8 / 25.196 | 51 / 159.418 | 120 / 67 |
| Unchanged | 0.030 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| Timestamp-only rebuild | 14.185 | 8 / 2.793 | 8 / 25.514 | 0 / 0 | 24 / 16 |
| Input restored | 0.030 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

Cold prerequisites came from the graph, including order-only dependencies:
24 other C++ compiles (143.817 edge s), 12 static archive steps (0.848 s), 14 Qt
AUTOMOC/autogen steps (14.608 s), and one Protobuf generation (0.145 s). The
cold wall is therefore not the cost of RCC. Each no-op returned `ninja: no work
to do`, with unchanged Ninja build and dependency logs.

- **Rejection and recovery.** With the cold 16-output manifest validated and
  the unchanged no-op recorded, temporarily moving the actual main Studio
  generated source aside made the output validator exit 3 with
  `missing resource output: .rcc/qrc_studio-ui.cpp`. Renaming that same file
  back returned exit 0 and reproduced all 16 original sizes, digests and
  modification times without a rebuild. This checks evidence rejection, not a
  claim about Ninja's recovery policy.
- **Invalidation and restoration.** Advancing only the owned `ui/shell/Main.qml`
  mtime (an explicit dependency in every RCC rule) dirtied all eight RCC
  outputs and reran all eight consuming compiles. All eight generated C++ and
  all eight object digests changed: each C++ diff was exactly the line encoding
  that file's 64-bit big-endian millisecond mtime in `qt_resource_struct`, and
  each object diff was three changed bytes within that field in `.rodata`,
  with other bytes unchanged. The input's original timestamps and bytes were
  restored. The final empty no-op retained the rebuilt output digests, not the
  cold ones; Ninja did not rewrite the embedded metadata back. No additional
  output deletion, rewrite or rebuild was used to hide that distinction.

Raw job logs, configure/build commands, tool and input identities, phase logs,
output manifests and generated artifacts are retained privately for independent
recomputation. This is one local observation of an unchanged graph, not
hosted ARM64 timing, full-build throughput, APT/cache measurement, or a claim
about rendering or Qt runtime behavior. The [bridge-object baseline issue
#159](https://github.com/martinkoenig/mantis-studio/issues/159) is separate;
fixture correctness and observational APT evidence are not substitutes for
this measurement. Controlled APT and like-for-like full build comparisons,
authenticated dependency closure, and application/output equivalence remain
under [#111](https://github.com/martinkoenig/mantis-studio/issues/111). The
PR #130/#39 and #103 holds are unchanged.
