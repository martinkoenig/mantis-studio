# Projects: isolated backend follow-up proposal

This document proposes future **public service contracts**. UI-M2a changes no
protocol, client ABI, daemon, store format, acquisition algorithm or lock policy.
The [current action matrix](projects.md) remains the implementation contract.

## P1: project switching isolation (blocks GUI New/Open)

The existing authenticated `Request.project_open(path, create)` is real. Active
capture and busy-job guards and the next-store lock work, but completed replay
queues and preview leases survive `Runtime::open_project`. A real public-client
audit confirms that A's replay can write a preview into B's cache after both
clients confirm B. Jobs/events also remain daemon-wide, contrary to the assignment's
initial suggestion of project-scoped history. No public field associates them
with a project. UI-M2a labels that context explicitly rather than guessing.

A separate runtime correction must give project-scoped acquisition/replay/data
references an immutable project identity/generation. A switch needs to invalidate
old replay queues, preview leases and delayed results across **all** clients,
prevent writes/read requests being reinterpreted against another store, and define
which completed jobs/events persist in a daemon-wide history. Work which captures
an old Store must never silently use the replacement Store. A GUI generation only
prevents delivery of cached images; it cannot fix daemon isolation or another client.

Require explicit precondition/current-project generation and an operation identity
for safe deduplication and unknown-outcome lookup. Preserve the old project until
the next store is acquired, validated and locked; atomically publish new identity
and scoped metadata. Test busy/refusal, old lease/replay use, late completion,
coincident artifact IDs, two clients, disconnect, restart and loss of the write
response. Never automatically retry a possibly completed mutation.

## Public catalog and operation surface

| Future contract | Required authority / semantics |
| --- | --- |
| Capabilities | Advertised, versioned per-daemon permissions; absence is unavailable, never optimistic UI enablement |
| Catalog list/search | Authorized stable project IDs, bounded pagination, deterministic sort, declared fields/presence, scoped result counts, snapshot/revision tokens; no GUI directory scanning |
| Current-project info/select | Stable daemon/project identity and generation; explicit all-clients mutation, expected-current precondition and post-operation authoritative confirmation |
| Exclusive create / explicit open | Distinguish create-new from open-existing. The current `create=true` can open existing data; do not label it an exclusive New guarantee |
| Recent/favorites/tags | Defined persistence/owner scope, optional-field presence, bounded updates, optimistic concurrency; distinguish device/laptop/user/daemon ownership |
| Versions/contents | Immutable artifact references with provenance, explicit type registry, true revision/chronology identity; never infer a version from an artifact ID |
| Import/clone/archive/package export | Explicit job contract, permission/capability checks, resource quotas, atomic publish, collision rules and immutable source |
| Share/cloud/cross-daemon | Authenticated authority and project identity include daemon; remote paths are runtime-host paths, never laptop file dialogs or implicit network catalogs |
| Delete/restore/trash | Explicit destructive permission, scoped identity + generation confirmation, dependency/active-job checks, recoverable trash policy, audited result |

## Security, transactions and recovery

- Token authentication currently grants daemon-wide access. Future catalog and
  operation capabilities need access control per subject/project/action, audited
  denials and no disclosure of unauthorized paths or metadata.
- Validate runtime-host paths before any side effect: reject NUL/control input,
  bounded Unicode handling, explicit normalization policy, path traversal,
  symlink/hard-link races, allowed roots, ownership and permissions. Shell quoting
  is not a path security boundary. UI-M2a has no shell or project-path mutation UI.
- Preserve cross-process project locks and define lock transfer/release ordering.
  A failed switch/create must retain current identity and avoid partial directory
  creation, cache contamination or leaked leases. `create=false` existence checks
  must not accidentally create after a manifest race. No GUI access to SQLite,
  manifests, object directories or runtime locks is acceptable.
- Define schema migration compatibility and validation before publication, with
  rollback/recovery for interrupted migrations, partial import and crash startup.
  Preserve immutable artifacts and existing calibration/acquisition provenance.
- Jobs require bounded admission, cancellation semantics before/after commit,
  explicit busy refusal and operation deduplication. Once committed, cancellation
  cannot ambiguously promise rollback. Unknown outcome needs query/confirmation,
  retaining phase/code/component and both operation and snapshot root causes.
- Keep data-plane/preview identifiers project-scoped, expiry/release explicit and
  all catalog/control payloads bounded. Do not fetch raw scans for catalog thumbnails.
  Optional generated thumbnails need their own immutable, authorized, size-bounded
  public artifact contract, produced by the runtime rather than a GUI scanner.

Acceptance and backend changes require independent review under the existing ADRs
and reliability standard; this proposal is not permission to implement them here.
The unreadable PointCloud retry (~500ms), unchanged in UI-M2a, is corrected by the
separate frontend [UI-M3a reliability package](pointcloud-retry.md). Project/replay
isolation remains a backend follow-up; retry suppression does not repair it.
