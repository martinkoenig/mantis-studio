"""Owned tiny CMake producer used by the existing shadow collection entrypoint.

Only these repository-owned setup commands run. Imported CTest commands never do.
No product configuration, target, test binary or borrowed output is accessed.
"""
import hashlib
import json
import os
from pathlib import Path
import re
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
PROTO = 'protocol/protobuf/mantis.proto'
RULE_RECEIPT = 'mantis-protobuf-ninja-query.json'
PYTHON_OUTPUT = 'python/mantis/mantis_pb2.py'
PYTHON_RECEIPT = 'mantis-protobuf-python-ninja-query.json'
ENCLOSURE_SCRIPT = 'tests/unit/alg03_enclosure.py'
CALIBRATION_FILES = ('sdk/python/mantis/calibration.py', 'sdk/python/mantis/__init__.py',
                     'tests/unit/python_calibration.py')
CALIBRATION_TRACE = 'mantis-python-calibration-copy-trace.json'


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
        cancellation_noted = False

        def retain_cancellation(signum):
            nonlocal primary, cancellation_noted
            if signum is not None and not cancellation_noted:
                message = 'tiny setup cancelled by signal ' + str(signum)
                if primary is None:
                    primary = KeyboardInterrupt(message)
                else:
                    primary.add_note(message)
                cancellation_noted = True
            if isinstance(primary, (subprocess.TimeoutExpired, subprocess.CalledProcessError)):
                primary.output, primary.stderr = bytes(output), bytes(diagnostic)

        # Caller dispositions belong outside the ENTIRE outcome/receipt
        # transaction, not merely outside process retirement. Never raise a
        # secondary asynchronous exception during publication/error accounting.
        try:
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
            receipt_error = None
            snapshot_signal = None
            try:
                with (root / 'producer-commands.jsonl').open('a') as stream:
                    snapshot_signal = cancellation_signal
                    retain_cancellation(snapshot_signal)
                    row = {'argv': list(map(str, command)), 'exit': code,
                           'reaped_exit': process.returncode if process is not None else None,
                           'error': type(primary).__name__ if primary else None,
                           'stdout': output.decode(errors='replace'), 'stderr': diagnostic.decode(errors='replace'),
                           'cleanup_errors': cleanup, 'elapsed_seconds': time.monotonic() - started,
                           'deadline_seconds': deadline - started, 'cancellation_signal': snapshot_signal}
                    stream.write(json.dumps(row, sort_keys=True) + '\n')
            except OSError as exc:
                receipt_error = exc
            retain_cancellation(cancellation_signal)
            if receipt_error is not None:
                if primary is None:
                    primary = receipt_error
                else:
                    primary.add_note('tiny setup receipt failed: ' + type(receipt_error).__name__)
            if cleanup:
                if primary is None:
                    primary = AssertionError('tiny setup retirement unconfirmed: ' + ','.join(cleanup))
                else:
                    primary.add_note('tiny setup cleanup: ' + ','.join(cleanup))
            retain_cancellation(cancellation_signal)
            if receipt_error is None and cancellation_signal != snapshot_signal:
                # A first signal during serialization/write/close/accounting
                # cannot change already-written bytes. Append exactly ONE bounded
                # amendment to the preceding command receipt, not a retry or
                # duplicate command. The first signal is immutable even if more
                # signals arrive during amendment publication/error accounting.
                amendment = {'event': 'publication-cancellation',
                             'cancellation_signal': cancellation_signal,
                             'error': type(primary).__name__}
                try:
                    with (root / 'producer-commands.jsonl').open('a') as stream:
                        stream.write(json.dumps(amendment, sort_keys=True) + '\n')
                except OSError as exc:
                    assert primary is not None
                    primary.add_note('tiny setup receipt failed: ' + type(exc).__name__)
            if primary is not None:
                raise primary
        finally:
            for signum, handler in handlers.items():
                signal.signal(signum, handler)
    return bytes(output)


def calibration_file_may_compete(args, caller, source, build):
    """Discard only known non-writers or writes visibly outside build/python."""
    if not args:
        return True
    op = args[0]
    if op in {'READ', 'STRINGS', 'SHA256', 'GLOB', 'GLOB_RECURSE', 'RELATIVE_PATH', 'MAKE_DIRECTORY'}:
        return False
    # The reviewed top-level recipe copy populates build/recipes, not build/python.
    if caller == str(source / 'CMakeLists.txt') and args == ['COPY', 'recipes', 'DESTINATION', str(build)]:
        return False
    if op == 'GENERATE' and args[1:2] == ['OUTPUT']:
        outputs = args[2:3]
    elif op == 'WRITE':
        outputs = args[1:2]
    elif op == 'REMOVE_RECURSE':
        outputs = args[1:]
    else:
        # No attempt to interpret arbitrary file operations, in particular
        # COPY/INSTALL directory mappings or their relative destinations.
        return True
    return not outputs or any(
        not output.startswith(str(build) + '/') or not output.isprintable() or
        any(c in output for c in '$;\\') or
        any(part in ('', '.', '..') for part in output[1:].split('/')) or
        Path(output).is_relative_to(build / 'python') for output in outputs)


def calibration_trace(manifest, trace):
    """Project the real expanded trace, without resolving or opening argv paths."""
    source, build = (Path(manifest['roots'][r]) for r in ('source', 'build'))
    assert trace.stat().st_size <= 16_000_000, 'fixture CMake trace exceeds capture limit'
    named_file = re.compile(r'(?<![A-Za-z0-9_.-])(?:calibration|__init__)\.py(?![A-Za-z0-9_.-])')
    with trace.open('rb') as stream:
        assert json.loads(stream.readline()) == {'version': {'major': 1, 'minor': 2}}
        commands = []
        for line in stream:
            row = json.loads(line)
            cmd, args = row['cmd'], row['args']
            # A directory-level writer need not mention either filename or the
            # absolute package path. Do not discard uncertain native file ops.
            if (any(named_file.search(arg) for arg in args) or
                    (cmd == 'file' and calibration_file_may_compete(args, row['file'], source, build)) or
                    (cmd in ('function', 'macro') and args and args[0].lower() in ('configure_file', 'file'))):
                commands.append({k: row[k] for k in ('cmd', 'file', 'line', 'frame', 'global_frame', 'args')})
    index = build / manifest['index']
    cache, = (row['jsonFile'] for row in json.loads(index.read_bytes())['objects'] if row['kind'] == 'cache')
    pins = {'index': index, 'cache': index.parent / cache, 'ctest': build / manifest['ctest'],
            **{p: source / p for p in ('CMakeLists.txt', *CALIBRATION_FILES)},
            **{p: build / p for p in ('python/mantis/calibration.py', 'python/mantis/__init__.py')}}
    receipt = {k: manifest[k] for k in ('source', 'roots', 'configuration', 'generator')}
    receipt.update(schema='ci110-mantis-python-calibration-copy-trace-v2', commands=commands,
                   sha256={k: digest(p.read_bytes()) for k, p in pins.items()})
    raw = encode(receipt)
    if len(raw) > 32_768:
        return None  # never trim away a possible writer to fit a success-only receipt
    (build / CALIBRATION_TRACE).write_bytes(raw)
    return {'root': 'build', 'path': CALIBRATION_TRACE, 'sha256': digest(raw)}


def produce(root, generator='Ninja', *, deadline=None):
    deadline = time.monotonic() + SETUP_SECONDS if deadline is None else deadline
    source, build = root / 'source', root / 'build'
    shutil.copytree(FIXTURE, source)
    # Exact import-bearing source bytes, not lookalike Python programs. The
    # fixture configures their copies and CTest argv but never executes them.
    for path in CALIBRATION_FILES:
        dest = source / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(__file__).resolve().parents[2] / path, dest)
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
    # The last commit isolates the generator input for the reachability oracle.
    with (source / PROTO).open('a') as stream:
        stream.write('\n// changed generator input\n')
    for args in (['add', PROTO], ['commit', '-m', 'generator input change']):
        run(['git', '-C', str(source), *args], root, env=env, deadline=deadline)
    commit = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], root, deadline=deadline).decode().strip()
    tree = run(['git', '-C', str(source), 'rev-parse', 'HEAD^{tree}'], root, deadline=deadline).decode().strip()
    query = build / '.cmake/api/v1/query'
    query.mkdir(parents=True)
    for kind in ('codemodel-v2', 'cmakeFiles-v1', 'cache-v2'):
        (query / kind).touch()
    trace = root / 'calibration-trace.jsonl'
    trace_args = (['--trace-expand', '--trace-format=json-v1', '--trace-redirect=' + str(trace)]
                  if generator == 'Ninja' else [])
    run(['cmake', '-S', str(source), '-B', str(build), '-G', generator,
         '-DCMAKE_BUILD_TYPE=Debug', '-DPython3_EXECUTABLE=' + sys.executable, *trace_args], root, deadline=deadline)
    queries = {}
    python_query = ''
    ninja_paths = ('build.ninja', 'CMakeFiles/rules.ninja')
    if generator == 'Ninja':
        # Capture graph metadata before compilation can populate Ninja's deps log.
        ninja_pins = {p: digest((build / p).read_bytes()) for p in ninja_paths}
        for output in ('mantis.pb.cc', 'mantis.pb.h'):
            queries[output] = run(['ninja', '-C', str(build), '-t', 'query', output],
                                  root, deadline=deadline).decode()
        python_query = run(['ninja', '-C', str(build), '-t', 'query', PYTHON_OUTPUT],
                           root, deadline=deadline).decode()
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
    for path in sorted(source.iterdir()) + [source / p for p in (PROTO, ENCLOSURE_SCRIPT, *CALIBRATION_FILES)]:
        if path.is_file():
            manifest['files'].append({'root': 'source', 'path': path.relative_to(source).as_posix(),
                                      'sha256': digest(path.read_bytes())})
    reply = indexes[0].parent
    index = json.loads(indexes[0].read_bytes())
    system = build / 'CMakeFiles' / index['cmake']['version']['string'] / 'CMakeSystem.cmake'
    extra = [system, *(build / 'python/mantis' / Path(p).name for p in CALIBRATION_FILES[:2])]
    if generator == 'Ninja':
        assert ninja_pins == {p: digest((build / p).read_bytes()) for p in ninja_paths}, 'Ninja graph changed after query'
        bindings = {**ninja_pins, PROTO: digest((source / PROTO).read_bytes()),
                    'index': digest(indexes[0].read_bytes()),
                    'cache': digest(next(reply.glob('cache-*.json')).read_bytes()),
                    'owner': digest(next(reply.glob('target-mantis-wire-*.json')).read_bytes())}
        receipt = {k: manifest[k] for k in ('source', 'roots', 'configuration', 'generator')}
        receipt.update(schema='ci110-mantis-protobuf-ninja-query-v1', sha256=bindings, queries=queries)
        (build / RULE_RECEIPT).write_bytes(encode(receipt))
        python_receipt = {k: manifest[k] for k in ('source', 'roots', 'configuration', 'generator')}
        python_receipt.update(schema='ci110-mantis-protobuf-python-ninja-query-v1', query=python_query,
                              sha256={**bindings, 'owner': digest(next(reply.glob('target-mantis-python-*.json')).read_bytes())})
        (build / PYTHON_RECEIPT).write_bytes(encode(python_receipt))
        extra.extend(build / p for p in (*ninja_paths, RULE_RECEIPT, PYTHON_RECEIPT))
    for path in sorted(reply.glob('*.json')) + [build / 'ctest.json', build / 'CMakeCache.txt', *extra]:
        manifest['files'].append({'root': 'build', 'path': path.relative_to(build).as_posix(),
                                  'sha256': digest(path.read_bytes())})
    if generator == 'Ninja':
        pin = calibration_trace(manifest, trace)
        if pin is not None:
            manifest['files'].append(pin)
    path = root / 'inputs.json'
    path.write_bytes(encode(manifest))
    return manifest, path, base


def calibration_decoy(root, *, directory_copy=False):
    """Two real configure-only snapshots: the package copy stays unchanged."""
    source, build = root / 'source', root / 'build'
    cal, init, script = CALIBRATION_FILES
    copy = 'python/mantis/calibration.py'
    for path in CALIBRATION_FILES:
        dest = source / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(__file__).resolve().parents[2] / path, dest)
    exact = (source / cal).read_bytes()
    active_copy = f'configure_file({cal} ${{CMAKE_BINARY_DIR}}/{copy} COPYONLY)'
    if directory_copy:
        alternate = source / 'alternate/mantis'
        alternate.mkdir(parents=True)
        fixed = alternate / 'calibration.py'
        fixed.write_bytes(exact)
        # CMake file(COPY) can skip equal timestamps at whole-second resolution.
        os.utime(fixed, (946684800, 946684800))
        route = f'''{active_copy}
file(COPY "${{CMAKE_CURRENT_SOURCE_DIR}}/alternate/mantis" DESTINATION python)'''
    else:
        (source / 'fixed.py').write_bytes(exact)
        route = f'''if(FALSE)
    {active_copy}
endif()
configure_file({cal} ${{CMAKE_BINARY_DIR}}/unrelated.py COPYONLY)
configure_file(fixed.py ${{CMAKE_BINARY_DIR}}/{copy} COPYONLY)'''
    (source / cal).write_bytes(exact + b'\n# independent before bytes\n')
    (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(CalibrationCopyRoute LANGUAGES NONE)
set(_Python3_EXECUTABLE "${{Python3_EXECUTABLE}}" CACHE INTERNAL "Fixture's running interpreter")
option(MANTIS_BUILD_STUDIO "Studio" OFF)
option(BUILD_TESTING "Testing" ON)
enable_testing()
add_custom_target(configured-metadata)
configure_file({init} ${{CMAKE_BINARY_DIR}}/python/mantis/__init__.py COPYONLY)
{route}
add_test(NAME python-calibration COMMAND ${{Python3_EXECUTABLE}} ${{CMAKE_CURRENT_SOURCE_DIR}}/{script} ${{CMAKE_BINARY_DIR}})
''')
    deadline = time.monotonic() + SETUP_SECONDS

    def git(*args):
        return run(['git', '-C', str(source), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                    '-c', 'core.hooksPath=' + os.devnull, *args], root, deadline=deadline).decode().strip()

    def configure(destination):
        command = ['cmake', '-S', str(source), '-B', str(destination), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug',
                   '-DPython3_EXECUTABLE=' + sys.executable]
        if destination == build:
            query = destination / '.cmake/api/v1/query'
            query.mkdir(parents=True)
            for kind in ('codemodel-v2', 'cmakeFiles-v1', 'cache-v2'):
                (query / kind).touch()
            command += ['--trace-expand', '--trace-format=json-v1', '--trace-redirect=' + str(root / 'trace.jsonl')]
        run(command, root, deadline=deadline)

    git('init', '--template=', '-b', 'develop')
    git('add', '.')
    git('commit', '-m', 'before copy route')
    base = git('rev-parse', 'HEAD')
    before = root / 'before'
    configure(before)
    (source / cal).write_bytes(exact)
    git('add', cal)
    git('commit', '-m', 'calibration module only')
    head = git('rev-parse', 'HEAD')
    assert git('diff', '--name-status', base, head) == f'M\t{cal}'
    configure(build)
    assert (before / copy).read_bytes() == (build / copy).read_bytes() == exact
    if not directory_copy:
        assert (before / 'unrelated.py').read_bytes() != (build / 'unrelated.py').read_bytes() == exact
    assert not git('status', '--porcelain')
    ctest = run(['ctest', '--test-dir', str(build), '--show-only=json-v1'], root, deadline=deadline)
    (build / 'ctest.json').write_bytes(ctest)
    reply = build / '.cmake/api/v1/reply'
    index, = reply.glob('index-*.json')
    cmake_files, = reply.glob('cmakeFiles-*.json')
    assert [r for r in json.loads(cmake_files.read_bytes())['inputs'] if r['path'] == cal] == [{'path': cal}]
    version = json.loads(index.read_bytes())['cmake']['version']['string']
    files = [('source', p) for p in git('ls-files').splitlines()] + [
        ('build', p.relative_to(build).as_posix()) for p in sorted(reply.glob('*.json'))] + [
        ('build', p) for p in ('ctest.json', f'CMakeFiles/{version}/CMakeSystem.cmake', copy, 'python/mantis/__init__.py')]
    roots = {'source': str(source), 'build': str(build)}
    manifest = {'schema': 'ci110-configured-inputs-v5', 'source': {'commit': head, 'tree': git('rev-parse', 'HEAD^{tree}')},
                'roots': roots, 'configuration': 'Debug', 'generator': 'Ninja', 'index': index.relative_to(build).as_posix(),
                'ctest': 'ctest.json', 'files': [{'root': r, 'path': p, 'sha256': digest((Path(roots[r]) / p).read_bytes())}
                                                for r, p in files]}
    pin = calibration_trace(manifest, root / 'trace.jsonl')
    if pin is not None:
        manifest['files'].append(pin)
    return manifest, base


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


def producer_inputs():
    """Exact local fixture/source identity, not producer authentication."""
    paths = [Path(__file__), Path(__file__).with_name('shadow_collection_test.py'),
             *sorted(FIXTURE.rglob('*'))]
    return {**{str(p.relative_to(Path(__file__).parent)): digest(p.read_bytes()) for p in paths if p.is_file()},
            **{p: digest((Path(__file__).resolve().parents[2] / p).read_bytes()) for p in CALIBRATION_FILES}}


def prepare(root):
    """Produce exactly the two existing fixtures before local CTest starts.

    Keep a single setup12 budget, including pinning/sealing. Command receipts
    retain actual argv/output/elapsed/retirement. No validator result is cached.
    """
    started = time.monotonic()
    deadline = started + SETUP_SECONDS
    original, path, base = produce(root, deadline=deadline)
    # Preserve the original input-pinning work inside the same setup budget.
    for row in original['files']:
        data = (Path(original['roots'][row['root']]) / row['path']).read_bytes()
        assert digest(data) == row['sha256'], 'pristine producer input changed'
    multi_root = root / 'multi'
    multi_root.mkdir()
    multi, multi_path, multi_base = produce(multi_root, 'Ninja Multi-Config', deadline=deadline)
    receipt = {'schema': 'ci110-tiny-prepared-v1', 'producer_inputs': producer_inputs(),
               'python': {'executable': str(Path(sys.executable).resolve()), 'version': sys.version},
               'producers': [
                   {'directory': '.', 'manifest_sha256': digest(path.read_bytes()), 'base': base},
                   {'directory': 'multi', 'manifest_sha256': digest(multi_path.read_bytes()), 'base': multi_base}]}
    receipt['setup_seconds'] = time.monotonic() - started
    assert receipt['setup_seconds'] <= SETUP_SECONDS, 'aggregate tiny setup12 exceeded'
    raw = encode(receipt)
    with (root / 'prepared.json').open('xb') as stream:
        stream.write(raw)
    assert time.monotonic() <= deadline, 'aggregate tiny setup12 publication exceeded'
    return digest(raw)


def load_prepared(root, expected):
    """Read the exact original closure in place; reject, never rebuild/fallback.

    The independent digest must come from the caller's completed setup receipt.
    Each test still runs the real diagnostic/collector/replay against its current
    inputs. Existing changed graph/PR/policy-rejected cases acquire fresh evidence.
    """
    raw = (root / 'prepared.json').read_bytes()
    assert len(raw) <= MAX_OUTPUT_BYTES and digest(raw) == expected, 'prepared receipt byte mismatch'
    receipt = json.loads(raw)
    assert receipt['schema'] == 'ci110-tiny-prepared-v1'
    assert receipt['producer_inputs'] == producer_inputs(), 'prepared producer source changed'
    assert receipt['python'] == {'executable': str(Path(sys.executable).resolve()), 'version': sys.version}, \
        'prepared interpreter changed'
    assert type(receipt['setup_seconds']) in (int, float) and 0 <= receipt['setup_seconds'] <= SETUP_SECONDS
    assert [r['directory'] for r in receipt['producers']] == ['.', 'multi']
    results = []
    for row, generator in zip(receipt['producers'], ('Ninja', 'Ninja Multi-Config')):
        directory = root if row['directory'] == '.' else root / 'multi'
        manifest_path = directory / 'inputs.json'
        raw = manifest_path.read_bytes()
        assert len(raw) <= MAX_OUTPUT_BYTES and digest(raw) == row['manifest_sha256'], \
            'prepared manifest byte mismatch'
        manifest = json.loads(raw)
        assert manifest['generator'] == generator and manifest['configuration'] == 'Debug'
        assert manifest['roots'] == {'source': str(directory / 'source'), 'build': str(directory / 'build')}, \
            'prepared fixture relocated'
        assert isinstance(row['base'], str) and len(row['base']) == 40
        for item in manifest['files']:
            path_parts = Path(item['path']).parts
            assert item['root'] in ('source', 'build') and path_parts and not Path(item['path']).is_absolute()
            assert '..' not in path_parts, 'prepared unsafe path'
            file = Path(manifest['roots'][item['root']]) / item['path']
            assert file.is_file() and not file.is_symlink(), 'prepared nonregular input'
            data = file.read_bytes()
            assert len(data) <= MAX_OUTPUT_BYTES and digest(data) == item['sha256'], \
                'prepared input byte mismatch'
        results.append((manifest, manifest_path, row['base']))
    return results


if __name__ == '__main__':
    import argparse
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
    parser = argparse.ArgumentParser(description='Prepare the existing two tiny test fixtures before CTest')
    parser.add_argument('--prepare', type=Path, required=True)
    args = parser.parse_args()
    args.prepare.mkdir(parents=True, exist_ok=False)
    print(json.dumps({'prepared_sha256': prepare(args.prepare.resolve())}, sort_keys=True))
