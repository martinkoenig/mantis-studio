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

These use only Python's standard library and small independent local Git
repositories. They cover actual commit/ref/merge relationships, complete source
changes and transitive consumer oracles, PR checkout mappings, shallow/missing and
corrupt objects/refs, ambient overrides/replacement objects, rename/deletion/type
changes/gitlinks, raw paths, empty diffs, resource/error controls and CLI behavior.
Graph controls exercise equivalent favorable/unfavorable chain renamings through
the actual CLI, count dependency visits independently of elapsed time, and verify
reachable/unreachable cycles, complete reasons and invalid/stale input recovery.
Resource controls use gated owned Python descendants and real FIFO/symlink/
descriptor races. Fixture-local subreaper ownership exists only in disposable
probe processes, with failure unwind reaping their own children. No configure,
compiler, project binary, CTest, installation or external network is used. Fixture-local commits/config/refs are disposable and never affect the project
repository. The same modules are registered as `ci-shadow-collection` and
`ci-shadow-resources` in Linux CTest configurations with `BUILD_TESTING=ON`,
including Studio ON/OFF and sanitizers. They use the configured Python interpreter
and retain their internal bounded fixture deadlines. The collector and reader
modules acquire no processes, network connections or inventory data on import;
regressions audit import and ensure uncertain inventory cleanup blocks further
Git acquisition. These regressions do not replace any applicable native final check.

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
