# Read-only Git shadow collection

Issue110's collector records what a change-impact policy would select. It never
runs tests, grants selected-only admission or changes any gate. Every decision
retains the five native Architecture profiles and all applicable local final
suites, sanitizer/detector controls and original UI repetitions. Current source
closure, runtime safety and historical failure recall remain unqualified.

## Local interface

```sh
python3 -B tools/ci_shadow.py --repository . --event push \
  --ref refs/heads/develop --base BEFORE_SHA1 --head PUSH_SHA1

python3 -B tools/ci_shadow.py --repository . --event pull_request \
  --ref refs/pull/NUMBER/merge --base BASE_PARENT_SHA1 \
  --head PR_HEAD_SHA1 --merge MERGE_SHA1
```

Use exact immutable 40-character commit IDs. The collector verifies that every
object exists, reads its tree and raw parent identities, checks the event's local
ref and actual checkout, then obtains the complete bounded raw NUL-delimited
base-to-tested-commit diff. PR merge parents must exactly match base and PR head;
the tested revision is the merge, not the PR head. Push comparisons use the supplied
pre-push base and checked push head. It rechecks ref/checkout after acquisition.
Both `refs/pull/N/merge` and checkout@v4's `refs/remotes/pull/N/merge` mapping are
supported; conflicting mappings are errors. Missing refs never fall back to HEAD.
Diff paths remain repository-root-relative even when `--repository` names a
subdirectory and repository configuration enables `diff.relative`; neither may
omit committed changes outside the invocation directory.

Endpoint assertions must come from the invocation's event context. Local Git
checks do not authenticate a provider event or prove which commit a server called
`before`; the collector does not claim that authority. It does not attest dirty
working-tree contents. It reads commit trees, never worktree diffs or issue labels.
Every PR is treated as untrusted; fork origin is reported UNKNOWN. No credentials,
network operation, privileged path, test executor or arbitrary command option is
provided. Replacement objects, ambient Git overrides, external diff/textconv,
hooks, fsmonitor, global/system configuration and transport protocols are disabled
for the collector's child commands. This changes no host or repository settings.

There is no caller `complete` flag. Completeness is set only after successful Git
exit, bounded parsing and the final identity checks. Git diagnostics, including
warnings with zero exit, fail acquisition. Commands have a five-second bound;
stdout is bounded (raw diff at 1 MiB), diagnostics at 4096 bytes, changes at 4096,
and commit metadata at 64 KiB. Overflow, timeout, corruption, shallow/missing
history, unsupported encoding, ref changes or malformed records retain full
selection and explicit error codes. The Linux pidfd/`waitid(WNOWAIT)` mechanism observes leader exit without reaping
it. On every outcome, including exit0/exit1 with closed-stream descendants, the
owned group is signalled before the leader identity can be reused, then reaped and
checked absent. Cleanup is bounded separately and reported as a secondary cause
without replacing an initiating command failure. Unconfirmed retirement fails
closed and prevents subsequent acquisition. Orphans that another owner has not
reaped remain unconfirmed; no escaped-session or arbitrary kernel-I/O preemption
claim follows. Unsupported containment platforms fail closed. Raw stderr, commit
messages and environment are never emitted.

Rename pairs, deletions, mode changes and gitlinks are retained in the acquisition
manifest. Exact-content renames use Git's 100% similarity detection; other moves
may appear as addition/deletion. Both forms retain full selection. Submodule
changes are unqualified. Raw UTF-8 filenames, including tabs/newlines, survive
JSON escaping; unsupported policy paths still retain full selection. Empty diffs
are complete acquisitions but do not authorize omission.

The optional pair `--inventory PATH --inventory-sha256 SHA256` permits diagnostic
source/target/test impact reporting from a pinned static inventory. The isolated
`ci_shadow_inventory.py` worker rejects nonregular paths before open, uses
nonblocking/no-follow open flags, validates the actual descriptor, bounds bytes
and compares descriptor/path identity and metadata before/after reading. FIFO,
symlink, replacement, size and read failures retain full fallback. The parent
bounds worker lifetime/output independently of synchronous filesystem calls and
records `inventory_acquisition`; timeout requires owned worker cleanup. If the OS
cannot complete retirement within that bound it is explicitly unconfirmed, never
reported as successful arbitrary I/O preemption. Byte integrity
is not authenticity or dependency completeness. The accepted inventory schema is
`ci110-inventory-v1` with scope `reused107`; synthetic/unknown inventories are
rejected. Even a caller marking closure complete cannot reduce the matrix.
Omitted, corrupt, oversized or stale inventories retain full profile identities
without fabricating unavailable current test registrations. The policy is adapted
from the accepted inert prototype; current generated/header/runtime closure and
representative labelled failure outcomes are still missing.

Diagnostic target reachability uses a reverse-dependency index and a worklist:
each target is discovered at most once and each dependency edge is indexed and
visited a bounded number of times, rather than repeatedly rescanning the graph.
Memory is proportional to the supplied targets/edges; deterministic reason/output
sorting is additional work proportional to the reported data. For a target with
no direct changed source, reasons list **all reached immediate dependencies**;
direct source reasons take precedence. Cycles terminate without recursion and
unreachable cycles are not impacted. Renaming/reordering targets cannot alter
logical reachability. This is bounded-input diagnostic computation, not an
end-to-end wall-clock guarantee, selection qualification or CI speedup claim.

Stdout is canonical JSON (`mantis-git-shadow-v1`). Zero exit means a manifest was
emitted, including fail-closed manifests; it never means qualification. Unsupported events produce a full fallback; argument
usage errors return nonzero. The `acquisition` object holds checked identities,
raw diff digest and records/completeness/errors. The `decision` object records
known target/test impact, suite decisions, provenance/errors and unconditional
full-gate obligations. No timestamp, absolute repository path or raw diagnostics
are embedded. Source evidence must be reviewed before public artifact upload.

## Disposable regression fixtures

```sh
python3 -B tests/ci/shadow_collection_test.py
python3 -B tests/ci/shadow_resource_test.py
```

The Git/resource controls use Python's standard library and small independent
local Git repositories. They cover actual commit/ref/merge relationships, complete source
changes and transitive consumer oracles, PR checkout mappings, shallow/missing and
corrupt objects/refs, ambient overrides/replacement objects, rename/deletion/type
changes/gitlinks, raw paths, empty diffs, resource/error controls and CLI behavior.
Graph controls exercise equivalent favorable/unfavorable chain renamings through
the actual CLI, count dependency visits independently of elapsed time, and verify
reachable/unreachable cycles, complete reasons and invalid/stale input recovery.
Resource controls use gated owned Python descendants and real FIFO/symlink/
descriptor races. Probe owners adopt/reap only their own descendants, including
on assertion failure. The tiny setup helper also scopes Linux subreaper ownership
and TERM/INT cancellation handlers to each owned command and restores both. It
observes leader exit without reaping, retires the owned process group before
releasing its identity, and reaps adopted descendants even when a successful or
failed leader leaves inherited streams open or closed. Bounded cleanup reuses
the collector's group-retirement primitive; this is not escaped-session or
hostile-process containment. Setup receipts retain the initiating exception,
partial bounded stdout/stderr, exit status, elapsed/budget and separate cleanup
errors on success, nonzero, timeout and cancellation. Receipt failure cannot
replace an initiating command error.

Configured diagnostic controls additionally configure and serially compile an owned tiny
multi-target fixture with Ninja and Ninja Multi-Config, and obtain real CTest JSON
with `--show-only=json-v1`. They never execute fixture test commands or product
targets. CMake, CTest, Ninja and a C++ compiler are required for this extension.
No installation or external network is used. Fixture-local commits/config/refs are disposable and never affect the project
repository. The same modules are registered as `ci-shadow-collection` and
`ci-shadow-resources` in Linux CTest configurations with `BUILD_TESTING=ON`,
including Studio ON/OFF and sanitizers. They use the configured Python interpreter
and retain the unchanged outer CTest30. Both configured fixture producers share
one 12-second campaign deadline (including their intervening diagnostic checks),
with at most two additional seconds for owned-group cleanup on failure, rather
than resetting a 30-second timer for every setup command. The collector and reader
modules acquire no processes, network connections or inventory data on import;
regressions audit import and ensure uncertain inventory cleanup blocks further
Git acquisition. These regressions do not replace any applicable native final check.

## Configured dependency diagnostics (opt-in, record-only)

The existing invocation accepts an additional **pair**:

```sh
python3 -B tools/ci_shadow.py --repository . --event push \
  --ref refs/heads/develop --base BEFORE_SHA1 --head PUSH_SHA1 \
  --configured-inputs PRIVATE_INPUT_MANIFEST.json \
  --configured-inputs-sha256 MANIFEST_SHA256
```

Without this option the `mantis-git-shadow-v1` envelope and static-inventory
behavior are unchanged. With it the envelope is explicitly
`mantis-git-shadow-v2`, containing a `configured_diagnostic` with schema
`ci110-configured-diagnostic-v1`. Consumers accepting only v1 must reject v2,
not reinterpret it as permission to select tests. The legacy `decision` remains
unchanged and FULL remains unconditional, even on a successful ingestion. The
configured path uses the existing policy's reachability routine, not another
selection engine. The static `--inventory` remains independent and optional.

### Pinned producer inputs

The importer **does not configure, compile, run CTest or execute any command**
from metadata. A separate caller-owned producer must create a CMake File API
query for `codemodel-v2`, `cache-v2` and `cmakeFiles-v1`, configure its own build
root, and save `ctest --test-dir BUILD -C CONFIG --show-only=json-v1` as a regular
JSON file. Native test binaries need not exist: missing CTest command expansion
is an explicit unknown, not a reason to build a product or fabricate a command.
Pin the exact chosen index rather than discovering a supposedly latest reply.
A private manifest contains these exact fields:

- `schema`: `ci110-configured-inputs-v1`.
- `source`: `{ "commit": SHA1, "tree": SHA1 }`, matching the collector's checked
  **tested** source, including the merge identity on PRs, not the PR head alone.
- `roots`: `source` and `build`, canonical absolute local directory paths. These
  are private inputs; output uses root digests and root-relative paths only.
- `configuration`: exact nonempty codemodel configuration name, e.g. `Debug`.
- `generator`: exact index generator name, e.g. `Ninja` or `Ninja Multi-Config`.
- `index`: build-relative pinned `.cmake/api/v1/reply/index-….json` path.
- `ctest`: build-relative saved CTest JSON path.
- `files`: unique records `{ "root": "source" | "build", "path": RELATIVE_PATH,
  "sha256": SHA256 }`. Include the index, every referenced codemodel/cache/
  cmakeFiles/target reply, CTest JSON, relevant configure inputs and source/header/
  Protobuf/QML metadata. Pinning `CMakeCache.txt` also binds its raw bytes. All
  listed files are read and digest-checked; unlisted paths are **never opened**.

All supplied files must be stable regular files. Symlink leaves/ancestry,
absolute or traversing relative record paths, duplicate JSON keys/records,
truncation, invalid/nonfinite JSON, unsupported document major versions,
dangling reply references, duplicate target/test identities, wrong metadata
roots/configuration/generator/source and changed byte pins fail closed. Missing
source metadata remains explicitly unresolved. Limits are 2,000,000 bytes per
file/manifest, 16,000,000 aggregate input-file bytes, 8192 files and 16384 records
per collection and edges/unknowns per emitted category. The isolated reader has
the existing five-second command bound and 2,000,000-byte output bound, with owned
group retirement. No synchronous Python filesystem call claims kernel-I/O
preemption. Imports remain inert.

The manifest digest binds the exact raw bytes, including private local root
identities; it is not a signature or authentication. File hashes bind the bytes
actually consumed. Caller-reported source/configuration context is not proof
that a particular CMake/CTest invocation generated those bytes, or that dirty
source matches a Git tree. In particular CTest JSON has no authenticated source
or configuration binding. These limits remain explicit unknowns even after
successful ingestion. Freeze raw manifests, producer commands and full outputs
privately; inspect normalized output before public upload. Raw command strings,
external absolute paths, environment, cache contents and parser exceptions are
not emitted. Command values are represented only by a byte digest.

### Edge and uncertainty semantics

- `observed`: codemodel-listed file-to-target and dependency-to-consumer edges,
  with exact pinned document/target identity. These are **listed observations**,
  not evidence that CMake listed every semantic dependency.
- `conservative-inference`: literal quoted include-to-target edges, with the
  including source in the reason, and target-to-test edges inferred by matching
  CTest's first command argument to a configured artifact. Include traversal is
  iterative and uses only pinned source bytes and listed include directories.
  It is not a preprocessor: conditionals/comments may overapproximate; macros,
  system/generated/unresolved includes stay unknown. Shared headers flow to
  downstream target consumers using the existing policy's worklist.
- `unresolved`: generated sources/configure inputs, object-library artifacts,
  Protobuf/QML/resource generation, fixtures/runtime dependencies, external/unpinned sources/includes,
  unsupported languages, missing/unmapped/ambiguous commands and configuration
  coverage remain explicit. Cycles terminate and are reported; absent or omitted
  dependency edges never establish no consumers. Unknown changed paths also
  remain unresolved. The selected configuration is imported; other configurations
  are not unioned or silently claimed qualified.

`status=ingested` means the bounded records were parsed, **not** complete product
closure, historical recall, provider-event authentication, signatures, OS
isolation, runtime coverage or performance qualification. `closure_complete` and
`execution_authorized` are always false; `full_matrix_would_select` is always true.
Every existing required local and remote gate remains binding. No workflow,
ruleset, scheduler, selection activation or skipped-test policy is changed.

## Integration boundary

The workflow owner may add a reviewed record-only invocation and artifact capture;
this change does not edit or activate a workflow. Supply event values through
quoted environment variables, never interpolation of untrusted text into shell
code. Preserve checkout's PR merge semantics, all existing tests, all five jobs,
required names and permissions. Do not use collector results as `if`, paths filters,
job matrices, test expressions, admission criteria or aggregation gates. Shallow
checkout currently yields an explicit full fallback; this tool never fetches or
changes checkout depth. Any later acquisition/fetch policy needs its own review.

Run the disposable regression command as an additional source check when integrating
this interface. Applicable actual full local qualification and exact PR/push CI
remain required before publication/acceptance. Historical precision/recall require
labelled actual outcomes; all-green jobs and seeded oracles cannot supply them.
