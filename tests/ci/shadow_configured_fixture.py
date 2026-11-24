"""Owned tiny CMake producer used by the existing shadow collection entrypoint.

Only these repository-owned setup commands run. Imported CTest commands never do.
No product configuration, target, test binary or borrowed output is accessed.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import selectors

# Both real configurations share this campaign budget, not one deadline per
# command. Two seconds of bounded retirement remain inside the outer CTest30.
SETUP_SECONDS = 12
MAX_OUTPUT_BYTES = 1_048_576


FIXTURE = Path(__file__).parent / 'fixtures' / 'shadow-configured'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def run(command, root, *, deadline=None, env=None):
    """Own only trusted tiny setup commands; never imported inventory commands.

    Linux-only like the containing CTests. Keep the leader unreaped until group
    signalling, adopt/reap its orphans, and restore process-local state. This is
    cooperative fixture containment, not a hostile-process/security supervisor.
    """
    import ctypes
    import ci_shadow as shadow
    started = time.monotonic()
    deadline = started + SETUP_SECONDS if deadline is None else deadline
    output, diagnostic = bytearray(), bytearray()
    process = None
    leader_fd = None
    code = None
    cleanup = []
    handlers = {}
    cancellation_signal = None
    previous_subreaper = ctypes.c_int()
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(37, ctypes.byref(previous_subreaper), 0, 0, 0) != 0:
        raise RuntimeError('tiny setup subreaper query failed')

    def cancelled(signum, frame):
        # Never raise across Popen's construction/handle publication or a
        # destructive read/buffer handoff. Both must finish before unwinding.
        nonlocal cancellation_signal
        if cancellation_signal is None:
            cancellation_signal = signum

    def check_cancelled():
        if cancellation_signal is not None:
            raise KeyboardInterrupt('tiny setup cancelled by signal ' + str(cancellation_signal))

    def read_stream(stream, destination):
        block = os.read(stream.fileno(), min(8192, MAX_OUTPUT_BYTES + 1 - len(destination)))
        destination.extend(block)
        if len(destination) > MAX_OUTPUT_BYTES:
            raise AssertionError('tiny setup output limit exceeded')
        return block

    try:
        for signum in (signal.SIGTERM, signal.SIGINT):
            handlers[signum] = signal.signal(signum, cancelled)
        if libc.prctl(36, 1, 0, 0, 0) != 0:
            raise RuntimeError('tiny setup subreaper unavailable')
        check_cancelled()
        if time.monotonic() >= deadline:
            raise subprocess.TimeoutExpired(command, 0, output=b'', stderr=b'')
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=True)
        check_cancelled()  # ownership is published before observing cancellation
        assert process.stdout is not None and process.stderr is not None
        leader_fd = os.pidfd_open(process.pid)
        with selectors.DefaultSelector() as ready:
            ready.register(leader_fd, selectors.EVENT_READ, None)
            for stream, data in ((process.stdout, output), (process.stderr, diagnostic)):
                os.set_blocking(stream.fileno(), False)
                ready.register(stream, selectors.EVENT_READ, data)
            while code is None:
                check_cancelled()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, deadline - started)
                # Non-raising Python handlers can restart select (PEP475).
                # Bound observation latency, not the campaign/cleanup deadline.
                events = ready.select(min(0.05, remaining))
                check_cancelled()
                for key, _ in events:
                    if key.data is None:
                        status = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT)
                        assert status is not None, 'tiny setup leader status unavailable'
                        code = status.si_status if status.si_code == os.CLD_EXITED else -status.si_status
                    elif not read_stream(key.fileobj, key.data):
                        ready.unregister(key.fileobj)
                    check_cancelled()  # any consumed bytes are already buffered
            # Leader exit is NOT pipe EOF: descendants may retain either stream.
            # Drain only currently available bounded output, then retire the tree.
            for stream, data in ((process.stdout, output), (process.stderr, diagnostic)):
                while time.monotonic() < deadline:
                    try:
                        if not read_stream(stream, data):
                            break
                        check_cancelled()
                    except BlockingIOError:
                        break
        check_cancelled()
        if code:
            raise subprocess.CalledProcessError(code, command, bytes(output), bytes(diagnostic))
    finally:
        primary = sys.exc_info()[1]
        # The non-raising handler remains active through retirement and draining.
        # Preserve the first error; a late cancellation is recorded, never lost.
        # No signals are sent outside this owned group.
        if process is not None:
            cleanup.extend(shadow._retire_owned_group(process))
        if leader_fd is not None:
            try:
                os.close(leader_fd)
            except OSError:
                cleanup.append('tiny-setup-handle-close-failed')
        if process is not None:
            for stream, data in ((process.stdout, output), (process.stderr, diagnostic)):
                if stream is None:
                    continue
                try:
                    os.set_blocking(stream.fileno(), False)
                    while read_stream(stream, data):
                        pass
                except BlockingIOError:
                    pass
                except (OSError, AssertionError) as exc:
                    cleanup.append('tiny-setup-output-drain-failed:' + type(exc).__name__)
                finally:
                    try:
                        stream.close()
                    except OSError:
                        cleanup.append('tiny-setup-handle-close-failed')
        if libc.prctl(36, previous_subreaper.value, 0, 0, 0) != 0:
            cleanup.append('tiny-setup-subreaper-restore-failed')
        for signum, handler in handlers.items():
            signal.signal(signum, handler)
        late_cancel = primary is None and cancellation_signal is not None
        if late_cancel:
            primary = KeyboardInterrupt('tiny setup cancelled by signal ' + str(cancellation_signal))
        if isinstance(primary, (subprocess.TimeoutExpired, subprocess.CalledProcessError)):
            primary.output, primary.stderr = bytes(output), bytes(diagnostic)
        row = {'argv': list(map(str, command)), 'exit': code,
               'reaped_exit': process.returncode if process is not None else None,
               'error': type(primary).__name__ if primary else None,
               'stdout': output.decode(errors='replace'), 'stderr': diagnostic.decode(errors='replace'),
               'cleanup_errors': cleanup, 'elapsed_seconds': time.monotonic() - started,
               'deadline_seconds': deadline - started, 'cancellation_signal': cancellation_signal}
        try:
            with (root / 'producer-commands.jsonl').open('a') as stream:
                stream.write(json.dumps(row, sort_keys=True) + '\n')
        except OSError as exc:
            if primary is None:
                raise
            primary.add_note('tiny setup receipt failed: ' + type(exc).__name__)
        if cleanup:
            if primary is None:
                raise AssertionError('tiny setup retirement unconfirmed: ' + ','.join(cleanup))
            primary.add_note('tiny setup cleanup: ' + ','.join(cleanup))
        if late_cancel:
            assert primary is not None
            raise primary
    return bytes(output)


def produce(root, generator='Ninja', *, deadline=None):
    deadline = time.monotonic() + SETUP_SECONDS if deadline is None else deadline
    source, build = root / 'source', root / 'build'
    shutil.copytree(FIXTURE, source)
    env = dict(os.environ, GIT_AUTHOR_NAME='Fixture', GIT_AUTHOR_EMAIL='fixture@example.invalid',
               GIT_COMMITTER_NAME='Fixture', GIT_COMMITTER_EMAIL='fixture@example.invalid',
               GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1')
    for args in (['init', '--template=', '-b', 'develop'], ['add', '.'], ['commit', '-m', 'tiny configured source']):
        run(['git', '-C', str(source), '-c', 'core.hooksPath=' + os.devnull, *args],
            root, env=env, deadline=deadline)
    base = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], root, deadline=deadline).decode().strip()
    # Independent real committed diff for the full CLI composition.
    with (source / 'public.hpp').open('a') as stream:
        stream.write('// changed shared interface\n')
    for args in (['add', '.'], ['commit', '-m', 'shared contract change']):
        run(['git', '-C', str(source), *args], root, env=env, deadline=deadline)
    commit = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], root, deadline=deadline).decode().strip()
    tree = run(['git', '-C', str(source), 'rev-parse', 'HEAD^{tree}'], root, deadline=deadline).decode().strip()
    query = build / '.cmake/api/v1/query'
    query.mkdir(parents=True)
    for kind in ('codemodel-v2', 'cmakeFiles-v1', 'cache-v2'):
        (query / kind).touch()
    run(['cmake', '-S', str(source), '-B', str(build), '-G', generator,
         '-DCMAKE_BUILD_TYPE=Debug'], root, deadline=deadline)
    # Tiny executable artifacts are needed for CTest's real command expansion.
    # One compiler only; never run these test executables.
    run(['cmake', '--build', str(build), '--config', 'Debug', '--parallel', '1'], root, deadline=deadline)
    ctest = run(['ctest', '--test-dir', str(build), '-C', 'Debug', '--show-only=json-v1'], root, deadline=deadline)
    (build / 'ctest.json').write_bytes(ctest)
    indexes = list((build / '.cmake/api/v1/reply').glob('index-*.json'))
    if len(indexes) != 1:
        raise AssertionError('tiny producer did not create exactly one File API index')
    manifest = {'schema': 'ci110-configured-inputs-v1', 'source': {'commit': commit, 'tree': tree},
                'roots': {'source': str(source), 'build': str(build)}, 'configuration': 'Debug',
                'generator': generator, 'index': indexes[0].relative_to(build).as_posix(),
                'ctest': 'ctest.json', 'files': []}
    for path in sorted(source.iterdir()):
        if path.is_file():
            manifest['files'].append({'root': 'source', 'path': path.name, 'sha256': digest(path.read_bytes())})
    for path in sorted((build / '.cmake/api/v1/reply').glob('*.json')) + [build / 'ctest.json', build / 'CMakeCache.txt']:
        manifest['files'].append({'root': 'build', 'path': path.relative_to(build).as_posix(),
                                  'sha256': digest(path.read_bytes())})
    path = root / 'inputs.json'
    path.write_bytes(encode(manifest))
    return manifest, path, base


def repin(manifest, root, path, value):
    """Mutate only an owned test input; explicitly refresh its independent pin."""
    raw = encode(value) if not isinstance(value, bytes) else value
    (Path(manifest['roots'][root]) / path).write_bytes(raw)
    row = next(r for r in manifest['files'] if r['root'] == root and r['path'] == path)
    row['sha256'] = digest(raw)


def target_document(manifest, name):
    reply = Path(manifest['index']).parent
    for row in manifest['files']:
        if row['root'] == 'build' and row['path'].startswith(str(reply / ('target-' + name + '-'))):
            path = row['path']
            return path, json.loads((Path(manifest['roots']['build']) / path).read_bytes())
    raise AssertionError('missing owned target ' + name)
