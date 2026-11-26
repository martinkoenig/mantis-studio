"""Read-only local Git shadow collection; never execute or admit selected tests."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import time

import ci_shadow_policy as policy

MAX_DIFF_BYTES = 1_048_576
MAX_CHANGES = 4096
COMMAND_SECONDS = 5
CLEANUP_SECONDS = 1
INVENTORY_SECONDS = 1
MAX_INVENTORY_BYTES = 2_000_000


class AcquisitionError(Exception):
    """Stable primary cause with separate owned-cleanup diagnostics."""
    def __init__(self, message, *, returncode=None, cleanup_errors=()):
        super().__init__(message)
        self.returncode = returncode
        self.cleanup_errors = tuple(cleanup_errors)


def require(condition, reason):
    if not condition:
        raise AcquisitionError(reason)


def _retire_owned_group(process):
    """Signal while the unreaped leader still reserves this group identity."""
    errors = []
    # No poll()/wait() is permitted before this point: a reaped leader would
    # permit PID/group reuse and make a subsequent group signal unsafe.
    if process.returncode is not None:
        return ['owned-leader-identity-lost']
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    except OSError:
        errors.append('owned-group-signal-failed')
    try:
        process.wait(timeout=CLEANUP_SECONDS)
    except (subprocess.TimeoutExpired, OSError):
        return errors + ['owned-leader-retirement-unconfirmed']
    deadline = time.monotonic() + CLEANUP_SECONDS
    while True:
        # Only reap adopted children in this invocation's group, never unrelated
        # children or host processes. Non-child orphans remain unconfirmed until
        # their actual owner reaps them; never report successful retirement early.
        try:
            while os.waitpid(-process.pid, os.WNOHANG)[0]:
                pass
        except ChildProcessError:
            pass
        except OSError:
            return errors + ['owned-group-reap-failed']
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            return errors
        except OSError:
            return errors + ['owned-group-retirement-unconfirmed']
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return errors + ['owned-group-retirement-unconfirmed']
        time.sleep(min(0.005, remaining))


def _run_owned(command, env, *, limit, seconds, kind):
    require(os.name == 'posix' and hasattr(os, 'pidfd_open') and hasattr(os, 'waitid')
            and hasattr(os, 'WNOWAIT'), 'unsupported-command-containment-platform')
    output = bytearray()
    diagnostic_bytes = 0
    process = None
    leader_fd = None
    try:
        try:
            process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True)
            leader_fd = os.pidfd_open(process.pid)
            with selectors.DefaultSelector() as ready:
                ready.register(process.stdout, selectors.EVENT_READ, 'output')
                ready.register(process.stderr, selectors.EVENT_READ, 'diagnostic')
                ready.register(leader_fd, selectors.EVENT_READ, 'leader')
                deadline = time.monotonic() + seconds
                leader_code = None
                while ready.get_map():
                    remaining = deadline - time.monotonic()
                    require(remaining > 0, kind + '-command-timeout')
                    events = ready.select(remaining)
                    require(events, kind + '-command-timeout')
                    for key, _ in events:
                        if key.data == 'leader':
                            status = os.waitid(os.P_PID, process.pid,
                                               os.WEXITED | os.WNOWAIT | os.WNOHANG)
                            require(status is not None, 'owned-leader-status-unavailable')
                            leader_code = (status.si_status if status.si_code == os.CLD_EXITED
                                           else 128 + status.si_status)
                            ready.unregister(leader_fd)
                            continue
                        is_output = key.data == 'output'
                        available = limit + 1 - len(output) if is_output else 4097 - diagnostic_bytes
                        block = os.read(key.fd, min(8192, available))
                        if not block:
                            ready.unregister(key.fileobj)
                        elif is_output:
                            output.extend(block)
                            require(len(output) <= limit, kind + '-output-limit')
                        else:
                            diagnostic_bytes += len(block)
                            require(diagnostic_bytes <= 4096, kind + '-diagnostic-limit')
                if leader_code != 0:
                    raise AcquisitionError(kind + '-command-failed', returncode=leader_code)
                require(not diagnostic_bytes, kind + '-reported-diagnostic')
        except OSError:
            raise AcquisitionError(kind + '-unavailable-or-inaccessible') from None
    finally:
        primary = sys.exc_info()[1]
        cleanup = _retire_owned_group(process) if process is not None else []
        if leader_fd is not None:
            try:
                os.close(leader_fd)
            except OSError:
                cleanup.append('owned-handle-close-failed')
        if process is not None:
            for stream in (process.stdout, process.stderr):
                try:
                    stream.close()
                except OSError:
                    cleanup.append('owned-handle-close-failed')
        if cleanup:
            if isinstance(primary, AcquisitionError):
                primary.cleanup_errors += tuple(cleanup)
            elif primary is None:
                raise AcquisitionError('owned-group-retirement-unconfirmed', cleanup_errors=cleanup)
            else:
                primary.add_note('owned cleanup: ' + ','.join(cleanup))
    return bytes(output)


class LocalGit:
    def __init__(self, repository):
        self.repository = repository
        # No ambient alternate object stores, injected config, lazy network fetch,
        # replacement objects, credential prompts or shell helpers. Git's own local
        # repository remains authoritative; no host/repository configuration writes.
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
        self.env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull,
                        GIT_TERMINAL_PROMPT='0', GIT_NO_REPLACE_OBJECTS='1',
                        GIT_NO_LAZY_FETCH='1', GIT_OPTIONAL_LOCKS='0', LC_ALL='C')

    def run(self, *args, limit=4096):
        command = ['git', '--no-optional-locks', '--no-replace-objects',
                   '-c', 'protocol.allow=never', '-c', 'core.hooksPath=' + os.devnull,
                   '-c', 'core.fsmonitor=false', '-C', str(self.repository), *args]
        return _run_owned(command, self.env, limit=limit, seconds=COMMAND_SECONDS, kind='git')

    def identity(self, value):
        require(policy.sha(value), 'immutable-sha1-required')
        require(self.run('cat-file', '-t', value).strip() == b'commit', 'endpoint-not-commit')
        tree = self.run('rev-parse', '--verify', value + '^{tree}').decode('ascii').strip()
        require(policy.sha(tree), 'invalid-tree-object')
        # Raw commit headers avoid history walkers' graft/simplification behavior.
        commit = self.run('cat-file', 'commit', value, limit=65_536)
        header = commit.split(b'\n\n', 1)[0].split(b'\n')
        require(header and header[0] == b'tree ' + tree.encode('ascii'), 'commit-tree-mismatch')
        parents = [line[7:].decode('ascii') for line in header if line.startswith(b'parent ')]
        require(all(policy.sha(p) for p in parents), 'invalid-parent-record')
        return {'commit': value, 'tree': tree, 'parents': parents}

    def resolve_ref(self, ref):
        # PR checkout creates refs/remotes/pull/N/merge with checkout@v4. Support
        # that exact mapping, but never substitute arbitrary HEAD for a missing ref.
        candidates = [ref]
        if re.fullmatch(r'refs/pull/[1-9][0-9]*/merge', ref):
            candidates.append(ref.replace('refs/pull/', 'refs/remotes/pull/', 1))
        found = []
        for candidate in candidates:
            # Missing refs produce an empty successful listing; corrupt-ref/other
            # Git failures remain errors rather than being mistaken for absence.
            listing = self.run('for-each-ref', '--format=%(refname) %(objectname)', candidate)
            for line in listing.decode('ascii').splitlines():
                parts = line.split(' ')
                require(len(parts) == 2 and policy.sha(parts[1]), 'invalid-ref-object')
                if parts[0] == candidate:
                    found.append(parts[1])
        require(found, 'event-ref-missing')
        require(len(set(found)) == 1, 'conflicting-event-refs')
        return found[0]


def parse_diff(raw):
    """Git --raw -z: header NUL path [NUL rename-target] NUL, no quoting loss."""
    require(not raw or raw.endswith(b'\0'), 'unterminated-nul-diff')
    tokens = raw.split(b'\0')[:-1] if raw else []
    records = []
    seen = set()
    index = 0
    while index < len(tokens):
        fields = tokens[index].split(b' ')
        index += 1
        require(len(fields) == 5 and re.fullmatch(rb':[0-7]{6}', fields[0]) and
                re.fullmatch(rb'[0-7]{6}', fields[1]) and
                all(re.fullmatch(rb'[0-9a-f]{40}', f) for f in fields[2:4]) and
                re.fullmatch(rb'[MADTU]|[RC][0-9]{1,3}', fields[4]), 'malformed-raw-diff')
        status = fields[4].decode('ascii')
        require(index < len(tokens), 'missing-diff-path')
        first = tokens[index]
        index += 1
        old = None
        if status[0] in 'RC':
            require(index < len(tokens), 'missing-rename-target')
            old, first = first, tokens[index]
            index += 1
        try:
            path = first.decode('utf-8')
            old_path = old.decode('utf-8') if old is not None else None
        except UnicodeError:
            raise AcquisitionError('unsupported-path-encoding') from None
        require(path and path not in seen, 'empty-or-duplicate-diff-path')
        require(not path.startswith('/') and all(p not in ('', '.', '..') for p in path.split('/')),
                'invalid-git-path')
        if old_path is not None:
            require(old_path and not old_path.startswith('/') and
                    all(p not in ('', '.', '..') for p in old_path.split('/')), 'invalid-old-git-path')
        seen.add(path)
        records.append({'status': status, 'path': path, 'old_path': old_path,
                        'old_mode': fields[0][1:].decode('ascii'), 'new_mode': fields[1].decode('ascii'),
                        'old_object': fields[2].decode('ascii'), 'new_object': fields[3].decode('ascii')})
        require(len(records) <= MAX_CHANGES, 'diff-record-limit')
    return records


def collect(repository, event, ref, base, head, merge=None, preflight_errors=()):
    receipt = {'event': event, 'ref': ref, 'git_version': None, 'identities': {}, 'tested_commit': None,
               'diff': {'complete': False, 'encoding': 'git-raw-nul-v1', 'records': [],
                        'sha256': None, 'bytes': None, 'record_count': None, 'base': None, 'head': None},
               'errors': [], 'privileged_access': False, 'fork_origin': 'UNKNOWN',
               'pr_trust_policy': 'all-PR-inputs-untrusted'}
    request = None
    if preflight_errors:
        receipt['errors'] = list(preflight_errors)
        return receipt, request
    git = LocalGit(repository)
    try:
        require(event in ('push', 'pull_request'), 'unsupported-event')
        require(isinstance(ref, str) and ((event == 'pull_request' and
                re.fullmatch(r'refs/pull/[1-9][0-9]*/merge', ref)) or
                (event == 'push' and re.fullmatch(r'refs/heads/[A-Za-z0-9._/-]+', ref))),
                'event-ref-mismatch')
        receipt['git_version'] = git.run('--version').decode('ascii').strip()
        git.run('check-ref-format', ref)
        require(git.run('rev-parse', '--is-shallow-repository').strip() == b'false', 'shallow-repository')
        for name, value in (('base', base), ('head', head)):
            receipt['identities'][name] = git.identity(value)
        if event == 'pull_request':
            receipt['identities']['merge'] = git.identity(merge)
            require(receipt['identities']['merge']['parents'] == [base, head], 'merge-parent-mismatch')
            tested = merge
        else:
            require(merge is None, 'unexpected-merge-identity')
            tested = head
        require(git.resolve_ref(ref) == tested, 'event-ref-sha-mismatch')
        require(git.run('rev-parse', '--verify', 'HEAD').decode('ascii').strip() == tested,
                'checkout-sha-mismatch')
        receipt['tested_commit'] = tested
        raw = git.run('diff', '--raw', '-z', '--no-abbrev', '--no-relative', '--no-ext-diff', '--no-textconv',
                      '--find-renames=100%', '--ignore-submodules=none', '-O' + os.devnull, base, tested, '--',
                      limit=MAX_DIFF_BYTES)
        records = parse_diff(raw)
        require(git.resolve_ref(ref) == tested, 'event-ref-changed-during-acquisition')
        require(git.run('rev-parse', '--verify', 'HEAD').decode('ascii').strip() == tested,
                'checkout-changed-during-acquisition')
        receipt['diff'].update(complete=True, records=records, sha256=hashlib.sha256(raw).hexdigest(),
                               bytes=len(raw), record_count=len(records), base=base, head=tested)
        if any('160000' in (r['old_mode'], r['new_mode']) for r in records):
            receipt['errors'].append('submodule-change-unqualified')
        endpoint = receipt['identities']['merge' if event == 'pull_request' else 'head']
        request = {'schema': 'ci110-request-v1', 'mode': 'shadow-feedback',
                   'base': {k: receipt['identities']['base'][k] for k in ('commit', 'tree')},
                   'head': {k: endpoint[k] for k in ('commit', 'tree')}, 'labels': [],
                   'diff': {'base_commit': base, 'head_commit': tested, 'complete': True,
                            'shallow': False, 'truncated': False, 'reported_count': len(records),
                            'changes': [{k: r[k] for k in ('status', 'path', 'old_path')} for r in records],
                            'event': 'pr-merge' if event == 'pull_request' else 'protected-push',
                            'ref': ref, 'expected_head': tested, 'fork': event == 'pull_request', 'privilege': 'none'}}
    except (AcquisitionError, UnicodeError) as exc:
        receipt['errors'].append(str(exc) if isinstance(exc, AcquisitionError) else 'invalid-git-encoding')
        if isinstance(exc, AcquisitionError):
            receipt['errors'].extend(exc.cleanup_errors)
    return receipt, request


def manifest(repository, event, ref, base, head, merge=None, inventory=None, inventory_sha256=None,
             preflight_errors=()):
    acquisition, request = collect(repository, event, ref, base, head, merge, preflight_errors)
    decision = policy.classify(policy.canonical(request) if request else b'', inventory, inventory_sha256)
    # Even a complete diff cannot attest current dependency closure or outcome recall.
    return {'schema': 'mantis-git-shadow-v1', 'shadow_only': True, 'execution_authorized': False,
            'acquisition': acquisition, 'decision': decision,
            'limits': ['all-required-native-local-and-CI-gates-remain',
                       'local-ref-validation-is-not-provider-event-authentication',
                       'historical-failure-recall-unknown', 'dependency-closure-unqualified']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=Path('.'))
    parser.add_argument('--event', required=True, help='push/pull_request; unsupported events emit full fallback')
    parser.add_argument('--ref', required=True)
    parser.add_argument('--base', required=True, help='Exact immutable pre-push/base-parent SHA1')
    parser.add_argument('--head', required=True, help='Exact immutable push/PR-head SHA1')
    parser.add_argument('--merge', help='Exact two-parent PR merge SHA1, required for pull_request')
    parser.add_argument('--inventory', type=Path, help='Optional pinned static diagnostic inventory')
    parser.add_argument('--inventory-sha256', help='Independent byte-integrity anchor, not authenticity proof')
    parser.add_argument('--configured-inputs', type=Path, help='Optional pinned configured diagnostic input manifest')
    parser.add_argument('--configured-inputs-sha256', help='Byte-integrity anchor for configured inputs, not authenticity')
    args = parser.parse_args(argv)
    data = None
    inventory_receipt = {'status': 'not-requested', 'error': None, 'cleanup_errors': []}
    if args.inventory is not None:
        command = [sys.executable, '-I', '-B', str(Path(__file__).with_name('ci_shadow_inventory.py')),
                   str(args.inventory)]
        try:
            data = _run_owned(command, dict(os.environ), limit=MAX_INVENTORY_BYTES,
                              seconds=INVENTORY_SECONDS, kind='inventory')
            inventory_receipt['status'] = 'regular-file'
        except AcquisitionError as exc:
            errors = {2: 'inventory-not-regular', 3: 'inventory-object-changed',
                      4: 'inventory-size-limit', 5: 'inventory-read-failed'}
            inventory_receipt.update(status='failed', error=errors.get(exc.returncode, str(exc)),
                                     cleanup_errors=list(exc.cleanup_errors))
    result = manifest(args.repository, args.event, args.ref, args.base, args.head,
                      args.merge, data, args.inventory_sha256, inventory_receipt['cleanup_errors'])
    result['inventory_acquisition'] = inventory_receipt
    if args.configured_inputs is not None:
        # Explicit opt-in v2 envelope; the legacy v1 shape/path is unchanged.
        # Source is the ACTUAL tested checkout (PR merge, not PR branch head).
        result['schema'] = 'mantis-git-shadow-v2'
        diagnostic = {'schema': 'ci110-configured-diagnostic-v1', 'status': 'rejected',
                      'errors': ['configured-source-context-unavailable'], 'closure_complete': False,
                      'execution_authorized': False, 'full_matrix_would_select': True}
        endpoint = result['acquisition']['identities'].get('merge' if args.event == 'pull_request' else 'head')
        source = {k: endpoint[k] for k in ('commit', 'tree')} if endpoint else None
        if source is not None and not result['acquisition']['errors']:
            worker = ('import sys; sys.path.insert(0, sys.argv.pop(1)); '
                      'import ci_shadow_configured; raise SystemExit(ci_shadow_configured.main())')
            command = [sys.executable, '-I', '-B', '-c', worker, str(Path(__file__).resolve().parent),
                       str(args.configured_inputs), args.configured_inputs_sha256 or '',
                       source['commit'], source['tree']]
            try:
                raw = _run_owned(command, dict(os.environ), limit=MAX_INVENTORY_BYTES,
                                 seconds=COMMAND_SECONDS, kind='configured')
                diagnostic = policy.strict_json(raw)
                import ci_shadow_configured as configured
                configured.project(diagnostic, result['acquisition']['diff']['records'])
            except AcquisitionError as exc:
                diagnostic.update(status='rejected', execution_authorized=False, full_matrix_would_select=True)
                diagnostic['errors'] = [str(exc), *exc.cleanup_errors]
            except (ValueError, KeyError, TypeError):
                diagnostic = {'schema': 'ci110-configured-diagnostic-v1', 'status': 'rejected',
                              'closure_complete': False, 'execution_authorized': False, 'full_matrix_would_select': True}
                diagnostic['errors'] = ['configured-worker-invalid-output']
        result['configured_diagnostic'] = diagnostic
    sys.stdout.buffer.write(policy.canonical(result))
    # Zero means a shadow manifest was emitted, never admission or qualification.
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
