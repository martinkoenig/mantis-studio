# Internal staged product install contract v1

Status: **provisional internal candidate**, not Debian packaging, release or
supported-platform acceptance. Application version `0.2.0` is independent of the
plugin queried C ABI, plugin/algorithm versions, wire, project, artifact and recipe
schemas. Existing executable names and environment keys are unchanged. This is
not a branding, licensing, service or public naming decision.

## Scope and ownership

The top-level CMake project is the authoritative producer. With internal runtime
libraries statically linked (`BUILD_SHARED_LIBS=OFF`, the existing default), its
`install()` rules define these disjoint immutable file owners:

| CMake component | Owned files | Product role dependencies |
| --- | --- | --- |
| `Daemon` | `mantisd`, private `mantis-plugin-host`, first-party Virtual Scanner, Virtual Rig, example-points, PLY exporter and Linux X1 modules with matching generated manifests; the example recipe; its license copy | No CLI, Studio, SDK or Qt requirement |
| `CLI` | `mantis-cli` and its license copy | No locally installed or automatically started daemon |
| `Studio` (only when enabled) | `mantis-studio`, with its existing QML/assets embedded in qrc, and its license copy | No locally installed or automatically started daemon; external matching Qt/runtime modules are required |
| `PluginSDK` | Existing standalone `MantisPluginSDK` public headers, INTERFACE targets, exact-version CMake metadata and license | No product binaries or private headers/libraries |

There is no runtime SONAME, exported private C++ library, independently packaged
runtime ABI, worker role, service, desktop metapackage or proprietary module.
`mantis-worker`, crash-test/other deliberate-failure plugins and test executables
remain build-only; the crash recipe is not installed. Physical X1 operation is not
validated by staging its existing module. CLI/Studio communicate through the
existing Client API. They do not acquire runtime ownership.

`PluginSDK` uses `add_subdirectory(sdk)` and the **same** install/export declarations
as the accepted standalone SDK. There is no second public-header copy list. The
SDK C11/C++23 requirements and exact-version discovery remain intact; no C++ ABI or
prior-SDK compatibility promise is added.

Shared internal build-tree use remains possible, but **product installation fails
explicitly** for `BUILD_SHARED_LIBS=ON`: contract v1 has no private shared-library
file/dependency ownership contract. It must not silently stage incomplete ELF
executables or invent a stable runtime SONAME. Standalone `cmake -S sdk` installation
remains independent of that product guard.

## Relative destinations and lookup

GNUInstallDirs variables control the relative directories, including `lib64` or a
multiarch/custom libdir. Defaults beneath the install prefix are:

- executables: `${CMAKE_INSTALL_BINDIR}` (normally `bin`);
- plugins/manifests: `${CMAKE_INSTALL_LIBDIR}/mantis/0.2.0/plugins`;
- private host: `${CMAKE_INSTALL_LIBEXECDIR}/mantis/0.2.0`;
- recipe: `${CMAKE_INSTALL_DATADIR}/mantis/0.2.0/recipes`;
- product licenses: `${CMAKE_INSTALL_DATADIR}/licenses/MantisStudio-0.2.0/<component>`;
- SDK: the existing versioned include and CMake paths from `sdk/CMakeLists.txt`.

`MANTIS_INSTALL_PLUGINSDIR`, `MANTIS_INSTALL_HOSTDIR` and
`MANTIS_INSTALL_RECIPESDIR` can override the derived defaults at configure time.
All destinations must be nonempty prefix-relative paths without parent traversal.
Absolute destinations are rejected, not exempted from DESTDIR. Use an absolute
install prefix and nonprivileged staging. An install preflight rejects symlink
destination ancestry before file copying; no chmod, ownership or security-policy
change is attempted. A missing required output or a destination that cannot be
written is an error. CMake installation is **not transactional**: failed staging
may retain a partial tree and must not be called a completed package.

Daemon lookup precedence is:

1. Explicit `--plugins`, `--plugin-host` and `--recipes` values, as before.
2. For the actual configured build output directory only, existing sibling
   `plugins`/`recipes` and `bin/mantis-plugin-host` defaults.
3. Otherwise, the configured relative installed layout, rooted at the actual
   running image directory. Linux uses `/proc/self/exe`; Windows and macOS use
   their executable-image APIs. Symlinks resolve to the real executable.

There is no `argv[0]`, CWD or PATH-based executable guess, source-tree lookup, or
retry against build outputs after an installed file is missing. The only CWD-based
state default remains the existing `Example.mantis`; tests and unattended use
should always specify `--project` explicitly. Paths relative to a prefix survive
`cmake --install --prefix`, DESTDIR and relocation, including spaces.

Existing per-plugin trust allowlists, sibling-library restrictions, queried C ABI,
manifest/binary identity checks, per-plugin error reporting and host isolation
behavior are unchanged. An unavailable recipe/plugin does not become a fabricated
successful workflow. Explicit overrides retain the existing error semantics.

## Nonprivileged reproduction

Run only with the existing exclusive native resource custody. Compiler jobs must
remain bounded and CTest serial. Do not install into the host `/usr` or start a
system service.

```sh
cmake -S . -B build/headless -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMANTIS_BUILD_STUDIO=OFF -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_LIBDIR=lib64 -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/headless --parallel 2
ctest --test-dir build/headless --parallel 1 --output-on-failure

# Fresh owned paths, not existing system or user-data directories:
DESTDIR="$STAGE" cmake --install build/headless --prefix '/opt/Mantis internal candidate'
cmake --install build/headless --prefix "$CLI_ONLY" --component CLI
python3 -B tests/install/qualify.py --build build/headless --output "$EVIDENCE"
```

The registered Linux `installed-product` test executes the same qualifier. Set
`TMPDIR` to a task-owned temporary root **outside the original source/build/install
trees**, with trusted ancestry for the existing X1 role tests. Do not weaken their
ancestry checks or change shared directory permissions to accommodate a temporary
root. The qualifier rejects a relocation root inside the source/build trees.
Each invocation retains a new evidence directory rather than erasing failed attempts.
It stages each component separately, compares their union with a real full
DESTDIR stage, then relocates to a fresh directory outside source/build/original
install. It runs installed CLI/daemon/host/plugins/recipe and externally compiled
installed-header C/C++ consumers, including loading their DSOs through the real
installed host. It never substitutes a dummy executable for product success.

The producer also emits `generated/install-layout-v1.json` with its **effective**
configure-time destinations. GNUInstallDirs can leave an empty cache placeholder
while its normal variable inherits a nonempty value (for example DATADIR from
DATAROOTDIR). Qualification consumes the generated contract, not a guessed
interpretation of raw cache placeholders. This producer metadata is not a new
installed runtime API or a second file-copy manifest.

`role-file-dependency-manifest-v1.json` is generated from **actual component-only
staged files**, not a parallel guessed copy list. It records file hashes, modes,
role ownership, ELF `DT_NEEDED` and resolved native loader closure with library
hashes. The full stage must equal that disjoint union. This is evidence of the
observed Linux closure, not Debian dependency-solver metadata or redistribution
permission. It does not bundle system Qt/OpenCV/Protobuf or certify an otherwise
Qt-free OS image. Prefix-contained `$ORIGIN` paths emitted by Qt's standard setup
are relocatable; absolute, CWD-based and prefix-escaping loader search paths are
rejected. A disposable actual-ELF dynamic-string mutation checks absolute-RPATH
rejection and exact-byte restoration. Runtime-path and resolved-library origins
must not point back to source/build. SDK configuration/compiler origins are retained separately.

Negative oracles exercise missing tokens/unavailable service, project ownership,
manifest ABI/identity, missing DSO/host/recipe, recipe schema, wrong host executable,
real queried ABI/short table rejection, output obstruction, required-output absence
and symlink stage containment. Corrupted inputs are restored byte-identically.
A disposable real installer guard bypass must make the containment oracle RED,
followed by exact-byte restoration and GREEN. Actual daemon-ready failure unwind,
observed forked-child retirement and unrelated-child survival test harness custody
and preserve the initiating error. These controls do not replace existing full
product cancellation, shutdown, storage, isolation or UI regressions.

## State and intentionally deferred policy

The install tree owns no projects, captures, artifacts, configuration, user data,
keys or token material. No schema/project migration, token provisioning, user/group
creation, privileged script, service enable/start, new network bind or authentication
policy is introduced. Daemon execution is explicit, locally configured, authenticated
loopback behavior as before. CLI live commands without a token or service fail
explicitly; help and Studio offline modes do not start a daemon.

The qualifier's owned staging overwrite/file-removal simulation compares untouched
user-data/config/key sentinels. It is **not** an OS package-manager upgrade, rollback,
remove or purge test. Native Debian metadata/solver/install/upgrade/removal testing
belongs to Issue #74. Service ownership/default start/offline/permissions policy
requires a separate explicit decision; no service default is silently adopted.

Full applicable local Studio ON/OFF, sanitizer/strict LSan effectiveness and UI
repeat gates, all five exact-final-head Architecture CI jobs, applicable standalone
SDK jobs and installed-product execution remain binding. Installed execution is
registered in the existing complete architecture suites on native x86_64/ARM64;
there is no test selection replacing a required full job. Independent source/test
review and separate installed execution are still required. Available Qt execution
does not clear Issue #39; Qt supported-runtime closure, redistribution rights,
branding, release/signing/registry publication, non-Linux/native platform evidence,
hardware and protected integration remain separate gates.
