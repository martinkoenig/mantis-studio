"""Linux Python-only Stage A qualification; logs retained in --work-dir.

Run in a venv containing build==1.2.2.post1 and pyproject's exact build tools.
No daemon, CMake, compiler campaign or source-tree PYTHONPATH is used.
"""
import argparse
import base64
import csv
import ctypes
from email.parser import BytesParser
import hashlib
import importlib.metadata
import importlib.util
import io
import json
import os
import platform
from pathlib import Path, PurePosixPath
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch
import venv
import zipfile

REPO = Path(__file__).resolve().parents[2]
PROBE = Path(__file__).with_name('installed_probe.py')
SCHEMA = REPO / 'protocol/protobuf/mantis.proto'
EPOCH = '1700000000'
WORK: Path
COMMAND_ID = 0
EXPECTED_MACHINE: str
EXPECTED_PYTHON: str


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_frozen_input_export(work, hashes):
    """Export only manifest-listed bytes; preserve hidden path components inside ZIP.

    upload-artifact's default hidden-file exclusion applies to the outer ZIP,
    not its members. Fixed order/metadata make the allowlisted export repeatable.
    No recursive directory upload or broad include-hidden-files option is needed.
    """
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_STORED) as archive:
        for relative, digest in sorted(hashes.items()):
            parts = PurePosixPath(relative)
            assert (parts.parts and not parts.is_absolute() and
                    relative == parts.as_posix() and '\\' not in relative and
                    '..' not in parts.parts), 'unsafe frozen input path'
            path = work / 'frozen-inputs' / relative
            assert not any(parent.is_symlink() for parent in
                           (path, *path.parents)), 'symlink frozen input'
            data = path.read_bytes()
            assert sha(data) == digest, 'frozen input hash mismatch: ' + relative
            member = zipfile.ZipInfo('frozen-inputs/' + relative, (1980, 1, 1, 0, 0, 0))
            member.create_system = 3
            member.external_attr = 0o100644 << 16
            archive.writestr(member, data)
    (work / 'frozen-inputs.zip').write_bytes(output.getvalue())


def preserve_secondary(failure, message):
    """Retain primary identity/errno/traceback and render secondary diagnostics.

    Chaining works on Python3.10 too; mutating args loses OSError diagnostics,
    while add_note is unavailable on that supported runtime.
    """
    diagnostic = RuntimeError(message)
    diagnostic.__cause__ = failure.__cause__
    diagnostic.__suppress_context__ = True
    failure.__cause__ = diagnostic
    failure.__suppress_context__ = True



def _retire_group(process, reaped):
    # The leader may already have exited on TERM. Its owned group still needs
    # escalation, and orphaned descendants adopted by this runner need reaping.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5)
    deadline = time.monotonic() + 5
    while True:
        try:
            pid, status = os.waitpid(-process.pid, os.WNOHANG)
        except ChildProcessError:
            break
        if pid:
            reaped.append({'pid': pid, 'status': status})
        elif time.monotonic() >= deadline:
            raise RuntimeError('owned descendant reaping timeout')
        else:
            time.sleep(0.01)
    try:
        os.killpg(process.pid, 0)
    except ProcessLookupError:
        return
    raise RuntimeError('owned process group remains after cleanup')


def command(args, *, cwd=None, expected=0, env=None, timeout=180, expect_timeout=False):
    """Linux qualification runner; timeout is never an ordinary negative exit.

    Commands must keep descendants in their new session/process group. Subreaper
    state is local to this serial runner process, saved/restored per invocation;
    waitpid/signals target only the newly created command group, never all children.
    """
    global COMMAND_ID
    COMMAND_ID += 1
    label = f'{COMMAND_ID:03d}'
    settings = dict(os.environ, SOURCE_DATE_EPOCH=EPOCH, PYTHONHASHSEED='0',
                    PIP_DISABLE_PIP_VERSION_CHECK='1', PIP_ONLY_BINARY=':all:',
                    TMPDIR=str(WORK / 'tmp'))
    settings.pop('PYTHONPATH', None)
    settings.pop('PYTHONHOME', None)
    if env:
        settings.update(env)
    timed_out, process, failure, previous = False, None, None, None
    libc = None
    cleanup_errors, reaped = [], []
    output = ''
    try:
        if sys.platform != 'linux':
            raise RuntimeError('UNQUALIFIED: command lifecycle checks require Linux')
        libc = ctypes.CDLL(None, use_errno=True)
        state = ctypes.c_int()
        if libc.prctl(37, ctypes.byref(state), 0, 0, 0) != 0:
            raise RuntimeError('cannot read runner subreaper state')
        previous = state.value
        if libc.prctl(36, 1, 0, 0, 0) != 0:
            raise RuntimeError('cannot enable runner subreaper')
        with (WORK / (label + '.log')).open('wb') as log:
            process = subprocess.Popen([str(arg) for arg in args], cwd=cwd or WORK,
                                       env=settings, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                # Establish the initiating failure before any cleanup can fail.
                if not expect_timeout:
                    failure = AssertionError(f'unexpected command timeout ({timeout}s); {label}.log')
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass  # Finally escalates the group, regardless of leader state.
        output = (WORK / (label + '.log')).read_text(errors='replace')
        if failure is None:
            if expect_timeout and not timed_out:
                failure = AssertionError(f'expected command timeout did not occur; {label}.log')
            elif not expect_timeout and ((expected == 0 and process.returncode != 0) or
                                         (expected != 0 and process.returncode == 0)):
                failure = AssertionError(f'command exit {process.returncode}, expected {expected}; {label}.log')
    except BaseException as error:
        failure = failure or error
    finally:
        if process is not None:
            try:
                _retire_group(process, reaped)
            except BaseException as error:
                cleanup_errors.append(f'{type(error).__name__}: {error}')
        if previous is not None and libc is not None and libc.prctl(36, previous, 0, 0, 0) != 0:
            cleanup_errors.append('runner subreaper restore failed')
        receipt = {'argv': [str(arg) for arg in args], 'cwd': str(cwd or WORK),
                   'pid': process.pid if process else None,
                   'exit': process.returncode if process else None,
                   'timed_out': timed_out, 'expected_timeout': expect_timeout,
                   'timeout_seconds': timeout, 'log': label + '.log',
                   'failure': str(failure) if failure else None,
                   'cleanup_errors': cleanup_errors, 'reaped_descendants': reaped,
                   'controlled_environment': {key: settings[key] for key in
                       ('SOURCE_DATE_EPOCH', 'PYTHONHASHSEED', 'PIP_ONLY_BINARY')}}
        try:
            with (WORK / 'commands.jsonl').open('a', encoding='utf-8') as out:
                out.write(json.dumps(receipt) + '\n')
        except OSError as error:
            cleanup_errors.append(f'receipt write failed: {error}')
    if failure is not None or cleanup_errors:
        failure = failure or AssertionError('command cleanup failed')
        failure.args = (str(failure) + '\n' + output[-5000:] +
                        ('\ncleanup: ' + '; '.join(cleanup_errors) if cleanup_errors else ''),)
        raise failure
    return output


def elf_machine(path):
    """Bounded native Linux identity check, not host authenticity attestation."""
    with Path(path).open('rb') as binary:
        header = binary.read(20)
    if len(header) != 20 or header[:6] != b'\x7fELF\x02\x01':
        raise RuntimeError('Expected a 64-bit little-endian ELF executable')
    machine = {62: 'x86_64', 183: 'aarch64'}.get(int.from_bytes(header[18:20], 'little'))
    if machine is None:
        raise RuntimeError('Unsupported ELF machine')
    return machine


def execution_identity(expected_machine, expected_python):
    """Check observed interpreter AND the selected distribution's actual binary.

    RECORD is an installed-file integrity oracle, not a signature or OS isolation.
    Portable package provenance deliberately excludes these platform-specific data.
    """
    from protoc import PROTOC_EXE
    observed = platform.machine()
    if (sys.platform != 'linux' or expected_machine not in ('x86_64', 'aarch64') or
            observed != expected_machine or elf_machine(sys.executable) != expected_machine):
        raise RuntimeError('Expected-platform mismatch: expected Linux/' + expected_machine +
                           '; observed ' + sys.platform + '/' + observed)
    if '.'.join(map(str, sys.version_info[:2])) != expected_python:
        raise RuntimeError('Expected-Python mismatch: ' + expected_python)
    distribution = importlib.metadata.distribution('protoc-wheel-0')
    if distribution.version != '21.11':
        raise RuntimeError('Expected compiler distribution protoc-wheel-0==21.11')
    selected = Path(PROTOC_EXE).resolve(strict=True)
    packaged = Path(distribution.locate_file('protoc/data/bin/protoc')).resolve(strict=True)
    if selected != packaged:
        raise RuntimeError('Selected compiler is not distribution executable')
    entry = next(item for item in distribution.files if str(item) == 'protoc/data/bin/protoc')
    data = selected.read_bytes()
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode()
    if (entry.hash is None or entry.hash.mode != 'sha256' or entry.hash.value != digest or
            entry.size != len(data)):
        raise RuntimeError('Selected compiler RECORD identity mismatch')
    machine = elf_machine(selected)
    if machine != expected_machine:
        raise RuntimeError('Selected compiler architecture mismatch')
    version = command([selected, '--version']).strip()
    if version != 'libprotoc 3.21.11':
        raise RuntimeError('Selected compiler version mismatch: ' + repr(version))
    return {'format': 1, 'expected_machine': expected_machine, 'expected_python': expected_python,
            'python_machine': observed, 'python_platform': sys.platform, 'python': sys.version,
            'python_executable': sys.executable, 'python_elf_machine': elf_machine(sys.executable),
            'python_binary_sha256': sha(Path(sys.executable).read_bytes()),
            'compiler_distribution': {'name': 'protoc-wheel-0', 'version': distribution.version},
            'compiler_version': version, 'compiler_executable': str(selected),
            'compiler_binary_sha256': sha(data), 'compiler_elf_machine': machine,
            'compiler_record_sha256': digest,
            'ci': {key: os.environ.get(key) for key in
                   ('GITHUB_SHA', 'GITHUB_HEAD_REF', 'GITHUB_RUN_ID', 'RUNNER_ARCH', 'RUNNER_OS')}}


def copy_source(name):
    root = WORK / name
    source = root / 'sdk/python'
    shutil.copytree(REPO / 'sdk/python', source,
                    ignore=shutil.ignore_patterns('__pycache__', '*.egg-info', 'build', 'dist'))
    canonical = root / 'protocol/protobuf/mantis.proto'
    canonical.parent.mkdir(parents=True)
    shutil.copyfile(SCHEMA, canonical)
    return source


def build(source, out, *, isolated=False, expected=0, kind=None):
    args = [sys.executable, '-m', 'build']
    if not isolated:
        args.append('--no-isolation')
    args += ['--' + kind] if kind else ['--wheel', '--sdist']
    output = command(args + ['--outdir', out, source], expected=expected)
    if expected == 0:
        verify_build_identity(output)
    return output


def verify_build_identity(output):
    # Compare every successful backend hook, including isolated overlay hooks,
    # to the independently inspected distribution executable, not backend constants.
    records = [json.loads(line.removeprefix('MANTIS_COMPILER_IDENTITY '))
               for line in output.splitlines() if line.startswith('MANTIS_COMPILER_IDENTITY ')]
    assert records, 'missing executed compiler identity diagnostics'
    reference = json.loads((WORK / 'execution.json').read_text())
    for record in records:
        assert record['distribution'] == {'name': 'protoc-wheel-0', 'version': '21.11'}
        assert record['version'] == 'libprotoc 3.21.11' and record['exit'] == 0
        assert record['argv'][1:] == ['--version']
        assert record['binary_sha256'] == reference['compiler_binary_sha256'], 'different selected compiler binary'


def unpack(sdist, name):
    target = WORK / name
    target.mkdir()
    with tarfile.open(sdist, 'r:gz') as archive:
        # Reject traversal/links/devices, even on Python 3.10 without filter=data.
        for item in archive.getmembers():
            parts = PurePosixPath(item.name)
            assert not parts.is_absolute() and '..' not in parts.parts
            assert item.isfile() or item.isdir()
        for item in archive.getmembers():
            destination = target / item.name
            if item.isdir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.extractfile(item).read())
    roots = list(target.iterdir())
    assert len(roots) == 1 and roots[0].is_dir(), roots
    return roots[0]


def verify_wheel(path, *, client_source=None):
    """Independent strict artifact oracle, not a signature/authenticity scheme."""
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        assert len(names) == len(set(names)), 'duplicate archive members'
        dist = 'mantis_studio_client-0.1.0.dist-info/'
        expected = {'mantis/__init__.py', 'mantis/calibration.py', 'mantis/mantis_pb2.py',
                    'mantis/mantis.proto', 'mantis/_provenance.json'}
        expected |= {dist + name for name in ('METADATA', 'WHEEL', 'top_level.txt', 'LICENSE', 'RECORD')}
        assert set(names) == expected, ('unexpected/missing files', set(names) ^ expected)
        content = {name: archive.read(name) for name in names}
    expected_client = (REPO / 'sdk/python/mantis/__init__.py').read_bytes() if client_source is None else client_source
    assert content['mantis/__init__.py'] == expected_client, 'wheel client differs from qualified source'
    assert content['mantis/calibration.py'] == (REPO / 'sdk/python/mantis/calibration.py').read_bytes(), 'wheel calibration differs from qualified source'
    metadata = BytesParser().parsebytes(content[dist + 'METADATA'])
    assert metadata['Name'] == 'mantis-studio-client'
    assert metadata['Version'] == '0.1.0'
    assert metadata['Requires-Python'] == '>=3.10'
    assert metadata.get_all('Requires-Dist') == ['protobuf<7,>=3.20']
    wheel = BytesParser().parsebytes(content[dist + 'WHEEL'])
    assert wheel['Root-Is-Purelib'] == 'true' and wheel['Tag'] == 'py3-none-any'
    assert wheel['Generator'] == 'setuptools (75.8.2)'
    records = list(csv.reader(io.StringIO(content[dist + 'RECORD'].decode())))
    assert {row[0] for row in records} == set(names) and len(records) == len(names)
    for name, digest, size in records:
        if name == dist + 'RECORD':
            assert digest == size == ''
        else:
            encoded = base64.urlsafe_b64encode(hashlib.sha256(content[name]).digest()).rstrip(b'=').decode()
            assert digest == 'sha256=' + encoded and size == str(len(content[name])), name
    provenance = json.loads(content['mantis/_provenance.json'])
    assert content['mantis/mantis.proto'] == SCHEMA.read_bytes(), 'noncanonical schema'
    assert provenance['schema_sha256'] == sha(SCHEMA.read_bytes())
    assert provenance['generated_sha256'] == sha(content['mantis/mantis_pb2.py']), 'stale generated module'
    assert set(provenance) == {'format', 'schema', 'schema_sha256', 'compiler_distribution',
                               'compiler_version', 'build_protobuf', 'generated_sha256',
                               'descriptor_set_sha256'}, 'unexpected provenance representation'
    assert provenance['format'] == 2 and provenance['schema'] == 'protocol/protobuf/mantis.proto'
    assert provenance['compiler_distribution'] == {'name': 'protoc-wheel-0', 'version': '21.11'}
    assert provenance['compiler_version'] == 'libprotoc 3.21.11'
    assert provenance['build_protobuf'] == '3.20.0'
    assert provenance['descriptor_set_sha256'] == sha((WORK / 'canonical.pb').read_bytes())
    for name, data in content.items():
        assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
        assert str(REPO).encode() not in data and str(WORK).encode() not in data
    return provenance


def install(wheel, name, runtime):
    root = WORK / 'installed' / name
    venv.EnvBuilder(with_pip=False, symlinks=(os.name != 'nt')).create(root)
    python = root / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
    command([sys.executable, '-m', 'pip', '--python', python, 'install',
             '--only-binary=:all:', 'protobuf==' + runtime, wheel])
    command([sys.executable, '-m', 'pip', '--python', python, 'check'])
    command([python, '-I', PROBE, WORK / 'canonical.pb'])
    return python


def installed_control(python, runtime, *, source=None, test=None, diagnostic=None):
    """Reuse the real-peer oracle, but never add a source/build import path."""
    args = [python, '-I', REPO / 'tests/unit/python_control.py', '--installed']
    if test:
        args.append(test)
    output = command(args + ['-v'], expected=1 if diagnostic else 0)
    records = [json.loads(line.removeprefix('MANTIS_INSTALLED_CONTROL '))
               for line in output.splitlines() if line.startswith('MANTIS_INSTALLED_CONTROL ')]
    assert len(records) == 1, 'missing/duplicate installed control identity'
    identity = records[0]
    assert identity['isolated'] == 1 and identity['protobuf'] == runtime
    assert Path(identity['prefix']).resolve() == python.parent.parent.resolve()
    expected = {'mantis': source if source is not None else (REPO / 'sdk/python/mantis/__init__.py').read_bytes(),
                'mantis.calibration': (REPO / 'sdk/python/mantis/calibration.py').read_bytes()}
    for module, data in expected.items():
        assert identity['modules'][module]['sha256'] == sha(data), 'installed control source mismatch'
    if diagnostic:
        receipt = json.loads((WORK / 'commands.jsonl').read_text().splitlines()[-1])
        assert receipt['exit'] == 1 and not receipt['timed_out'], 'negative was not an ordinary test failure'
        assert diagnostic in output and 'Ran 1 test' in output, 'intended control RED missing'
    with (WORK / 'installed-controls.jsonl').open('a', encoding='utf-8') as out:
        out.write(json.dumps({'identity': identity, 'test': test, 'expected_diagnostic': diagnostic}) + '\n')
    return identity


def installed_jobs(python, wheel):
    """Bind SAME-process control/Job execution to independently verified payloads."""
    output = command([python, '-I', '-B', PROBE.with_name('installed_job_probe.py')])
    records = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
    identity, = [r for r in records if r.get('phase') == 'installed-job-origins']
    result, = [r for r in records if r.get('phase') == 'installed-job-result']
    assert result['successful'] and not result['leaked_threads'], 'combined installed suite failed'
    assert Path(identity['prefix']).resolve() == python.parent.parent.resolve()
    with zipfile.ZipFile(wheel) as archive:
        expected_client = sha(archive.read('mantis/__init__.py'))
        expected_wire = sha(archive.read('mantis/mantis_pb2.py'))
    for name, entry in identity['origins'].items():
        assert entry['sha256'] == (expected_wire if name == 'wire' else expected_client), name
    for name, entry in identity['fixtures'].items():
        assert entry['sha256'] == sha((REPO / 'tests/unit' / (name + '.py')).read_bytes()), name
    with (WORK / 'installed-combined.jsonl').open('a', encoding='utf-8') as out:
        out.write(json.dumps({'wheel_sha256': sha(wheel.read_bytes()),
                              'identity': identity, 'result': result}) + '\n')


class Artifacts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.a = copy_source('root-a')
        cls.b = copy_source('root-b')
        cls.out_a, cls.out_b = WORK / 'dist-a', WORK / 'dist-b'
        command([sys.executable, '-m', 'pip', 'list', '--format=json'])
        # Independent descriptor oracle can be selected to a different protoc;
        # both compiler identities/commands are retained in receipts.
        oracle = os.environ.get('MANTIS_DESCRIPTOR_PROTOC')
        if not oracle:
            from protoc import PROTOC_EXE
            oracle = PROTOC_EXE
        command([oracle, '--version'])
        command([oracle, '-I' + str(SCHEMA.parent), '--descriptor_set_out=' + str(WORK / 'canonical.pb'), SCHEMA])
        build(cls.a, cls.out_a, isolated=True)
        build(cls.b, cls.out_b)
        cls.wheel = next(cls.out_a.glob('*.whl'))
        cls.sdist = next(cls.out_a.glob('*.tar.gz'))
        cls.rebuilt_source = unpack(cls.sdist, 'unpacked-without-checkout')
        # Prove no relative traversal back to root-a is needed for the rebuild.
        cls.a.parents[1].rename(WORK / 'original-checkout-unavailable')
        build(cls.rebuilt_source, WORK / 'rebuilt', kind='wheel', isolated=True)
        cls.rebuilt = next((WORK / 'rebuilt').glob('*.whl'))
        receipts = {str(p.relative_to(WORK)): sha(p.read_bytes()) for directory in
                    (cls.out_a, cls.out_b, WORK / 'rebuilt') for p in directory.iterdir()}
        (WORK / 'artifacts.json').write_text(json.dumps(receipts, indent=2) + '\n')

    def test_execution_identity_negatives_and_restored_positive(self):
        wrong = 'aarch64' if EXPECTED_MACHINE == 'x86_64' else 'x86_64'
        output = command([sys.executable, '-B', Path(__file__).resolve(), '--identity-only',
                          '--work-dir', WORK / 'wrong-platform', '--expected-machine', wrong,
                          '--expected-python', EXPECTED_PYTHON], expected=1)
        self.assertIn('RuntimeError: Expected-platform mismatch:', output)
        self.assertFalse((WORK / 'wrong-platform/execution.json').exists())
        fake = WORK / 'false-protoc'
        fake.write_text('#!' + sys.executable + '\nprint("libprotoc 3.21.11")\n')
        fake.chmod(0o755)
        self.assertEqual(command([fake, '--version']).strip(), 'libprotoc 3.21.11')
        with patch('protoc.PROTOC_EXE', str(fake)):
            with self.assertRaisesRegex(RuntimeError, 'Selected compiler is not distribution executable'):
                execution_identity(EXPECTED_MACHINE, EXPECTED_PYTHON)
        # A real binary's header, not an echoed runner label, is checked.
        self.assertEqual(elf_machine(sys.executable), EXPECTED_MACHINE)
        with self.assertRaisesRegex(RuntimeError, '64-bit little-endian ELF'):
            elf_machine(fake)
        with patch.object(platform, 'machine', return_value=wrong):
            with self.assertRaisesRegex(RuntimeError, 'Expected-platform mismatch'):
                execution_identity(EXPECTED_MACHINE, EXPECTED_PYTHON)
        restored = execution_identity(EXPECTED_MACHINE, EXPECTED_PYTHON)
        self.assertEqual(restored, json.loads((WORK / 'execution.json').read_text()))
        (WORK / 'identity-controls.json').write_text(json.dumps({
            'wrong_platform': 'rejected', 'false_compiler_self_report': 'rejected',
            'restored': restored}, indent=2) + '\n')

    def test_command_timeout_retains_receipt_and_reaps_child(self):
        pidfile = WORK / 'hang-child.pid'
        output = command([sys.executable, PROBE.with_name('hang_fixture.py'), pidfile],
                         timeout=2, expect_timeout=True)
        self.assertIn('parent-and-child-ready', output)
        receipt = json.loads((WORK / 'commands.jsonl').read_text().splitlines()[-1])
        self.assertTrue(receipt['timed_out'])
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pidfile.read_text()), 0)

    def test_command_runner_regressions(self):
        command([sys.executable, '-B', PROBE.with_name('command_test.py')],
                env={'MANTIS_COMMAND_EVIDENCE': str(WORK / 'command-regressions')})

    def test_pep517_metadata_and_pip_sdist_build(self):
        output = WORK / 'pip-sdist-wheel'
        frontend = WORK / 'frontends/pip-only'
        venv.EnvBuilder(with_pip=False, symlinks=(os.name != 'nt')).create(frontend)
        python = frontend / 'bin/python'
        # No protoc/setuptools in the host; the host runtime intentionally differs
        # from the build requirement. Only the PEP517 overlay may supply tools.
        command([sys.executable, '-m', 'pip', '--python', python, 'install',
                 'pip==25.1.1', 'protobuf==6.33.5'])
        command([python, '-m', 'pip', 'list', '--format=json'])
        log = command([python, '-m', 'pip', 'wheel', '--verbose', '--no-deps',
                       '--wheel-dir', output, self.sdist])
        # pip prefixes hook stdout with two spaces in verbose mode.
        verify_build_identity('\n'.join(line.lstrip() for line in log.splitlines()))
        wheel = next(output.glob('*.whl'))
        verify_wheel(wheel)
        self.assertEqual(wheel.read_bytes(), self.wheel.read_bytes())
        python = install(wheel, 'pip-sdist-wheel', '3.20.0')
        installed_control(python, '3.20.0')
        installed_jobs(python, wheel)
        python = install(wheel, 'pip-sdist-wheel-max', '6.33.5')
        installed_control(python, '6.33.5')
        installed_jobs(python, wheel)

    def test_archives_and_independent_root_reproducibility(self):
        self.assertEqual(self.wheel.read_bytes(), next(self.out_b.glob('*.whl')).read_bytes())
        self.assertEqual(self.sdist.read_bytes(), next(self.out_b.glob('*.tar.gz')).read_bytes())
        self.assertEqual(self.wheel.read_bytes(), self.rebuilt.read_bytes())
        provenance = verify_wheel(self.wheel)
        self.assertEqual(provenance, verify_wheel(self.rebuilt))
        with tarfile.open(self.sdist) as archive:
            files = {PurePosixPath(m.name).relative_to('mantis_studio_client-0.1.0').as_posix(): archive.extractfile(m).read()
                     for m in archive.getmembers() if m.isfile()}
        expected = {'_build_backend.py', '_schema/mantis.proto', '_schema/provenance.json',
                    'mantis/mantis_pb2.py', 'pyproject.toml', 'LICENSE', 'README.md', 'PKG-INFO',
                    'MANIFEST.in', 'setup.cfg', 'mantis/__init__.py', 'mantis/calibration.py',
                    'mantis/_provenance.json', 'mantis/mantis.proto'}
        self.assertEqual(set(files), expected, 'unexpected/private sdist input')
        self.assertEqual(files['_schema/mantis.proto'], SCHEMA.read_bytes())
        self.assertEqual(json.loads(files['_schema/provenance.json']), provenance)
        self.assertEqual(files['mantis/mantis_pb2.py'], zipfile.ZipFile(self.wheel).read('mantis/mantis_pb2.py'))
        self.assertFalse(any(name.endswith(('.so', '.dll', '.pyd', '.exe', '.pyc')) or name.startswith('/')
                             or '..' in PurePosixPath(name).parts for name in files))

    def test_installed_wheel_and_sdist_wheel_runtime_boundaries(self):
        # Minimum advertised release and representative current major boundary.
        for wheel, name, runtime in ((self.wheel, 'wheel-min', '3.20.0'),
                                     (self.rebuilt, 'sdist-min', '3.20.0'),
                                     (self.wheel, 'wheel-max', '6.33.5'),
                                     (self.rebuilt, 'sdist-max', '6.33.5')):
            with self.subTest(name=name):
                python = install(wheel, name, runtime)
                installed_control(python, runtime)
                # Existing tests' build/python entry is deliberately nonexistent:
                # -I plus installed module-path probe prevents checkout fallback.
                for script in ('tests/unit/python_calibration.py', 'tests/unit/python_capture.py',
                               'tests/contract/protocol_fields.py'):
                    command([python, '-I', REPO / script, WORK / 'absent-native-build'])
                installed_jobs(python, wheel)
                # Reuse a disposable consumer, not the artifact or checkout SDK.
                if name == 'wheel-min':
                    self.installed_job_sensitivity(python, wheel)
                    self.installed_job_sensitivity(python, wheel, interaction=True)

    def installed_job_sensitivity(self, python, wheel, *, interaction=False):
        package, = python.parent.parent.glob('lib/python*/site-packages/mantis')
        module = package / '__init__.py'
        originals = {p: p.read_bytes() for p in package.iterdir() if p.is_file()}
        checkout = (REPO / 'sdk/python/mantis/__init__.py').read_bytes()
        artifact = wheel.read_bytes()
        original = originals[module]
        needle = (b'            sock.settimeout(Client._remaining(deadline))' if interaction else
                  b'        self._remaining(deadline)\n        return report')
        replacement = b'            sock.settimeout(5)' if interaction else b'        return report'
        mutant = original.replace(needle, replacement)
        self.assertEqual(original.count(needle), 1, 'late-report mutation target must be unique')
        self.assertNotEqual(mutant, original)
        probe = PROBE.with_name('installed_job_probe.py')
        mode = '--interaction-sensitivity' if interaction else '--sensitivity'
        diagnostic = ('combined Job/control receive exceeded outer budget' if interaction else
                      'AssertionError: TimeoutError not raised')
        receipt_name = 'installed-interaction-sensitivity.json' if interaction else 'installed-job-sensitivity.json'
        failure = None
        restoration_errors = []
        receipt = {'artifact': str(wheel), 'artifact_sha256': sha(artifact),
                   'checkout_sdk_sha256': sha(checkout), 'installed_module': str(module),
                   'original_sha256': sha(original), 'mutant_sha256': sha(mutant),
                   'original_installed_files': {p.name: sha(data) for p, data in originals.items()}}
        try:
            module.write_bytes(mutant)
            output = command([python, '-I', '-B', probe, mode], expected=1)
            self.assertIn(diagnostic, output,
                          'not the intended late-report assertion RED')
            self.assertIn('FAILED (failures=1)', output)
            self.assertNotIn('ERROR:', output)
            self.assertNotIn('Peer teardown also failed', output)
            self.assertIn('"phase": "installed-job-origins"', output)
            self.assertIn('"successful": false', output)
        except BaseException as error:
            failure = error
        finally:
            try:
                module.write_bytes(original)
                restored = {p: p.read_bytes() for p in package.iterdir() if p.is_file()}
                receipt['restored_sha256'] = sha(restored[module])
                receipt['restored_installed_files'] = {p.name: sha(data) for p, data in restored.items()}
                self.assertEqual(restored, originals, 'installed package restoration must be byte-exact')
                self.assertEqual((REPO / 'sdk/python/mantis/__init__.py').read_bytes(), checkout)
                self.assertEqual(wheel.read_bytes(), artifact)
                command([python, '-I', '-B', probe, mode])
            except BaseException as error:
                restoration_errors.append(f'{type(error).__name__}: {error}')
            receipt.update(failure=str(failure) if failure else None, restoration_errors=restoration_errors)
            try:
                (WORK / receipt_name).write_text(json.dumps(receipt, indent=2) + '\n')
            except BaseException as error:
                restoration_errors.append(f'receipt write failed: {error}')
        if failure is not None:
            if restoration_errors:
                preserve_secondary(failure, 'installed Job restoration also failed: ' +
                                   '; '.join(restoration_errors))
            raise failure
        self.assertEqual(restoration_errors, [], 'installed Job restoration failed')

    def test_installed_job_qualifier_regressions(self):
        command([sys.executable, '-B', PROBE.with_name('installed_job_qualifier_test.py')])

    def test_installed_job_adapter_rejects_unisolated_and_absent_fixtures(self):
        # Independent of test ordering: use a private malformed-install consumer.
        python = install(self.wheel, 'job-adapter-failures', '3.20.0')
        probe = PROBE.with_name('installed_job_probe.py')
        output = command([python, '-B', probe], expected=1)
        self.assertIn('installed Job probe requires -I and a private venv', output)
        absent = WORK / 'absent-fixtures/sdk/installed_job_probe.py'
        absent.parent.mkdir(parents=True)
        shutil.copyfile(probe, absent)
        output = command([python, '-I', '-B', absent], expected=1)
        self.assertIn('FileNotFoundError', output)
        self.assertIn('python_control.py', output)
        module, = python.parent.parent.glob('lib/python*/site-packages/mantis/mantis_pb2.py')
        original = module.read_bytes()
        failure = None
        try:
            module.unlink()
            output = command([python, '-I', '-B', probe], expected=1)
            self.assertIn('mantis_pb2', output)
            self.assertNotIn('"phase": "installed-job-result"', output)
        except BaseException as error:
            failure = error
        finally:
            try:
                module.write_bytes(original)
            except BaseException as cleanup:
                if failure is None:
                    raise
                preserve_secondary(failure, 'installed wire restoration also failed: ' + str(cleanup))
        if failure is not None:
            raise failure
        self.assertEqual(module.read_bytes(), original)
        command([python, '-I', '-B', probe])

    def test_pre353_installed_control_red_exact_restored_green(self):
        # This is the actual ca04 client, not a replacement/fake Client. Keep only
        # its small control-method slice as a fixture; verify the COMPLETE module
        # against the immutable parent before building/installing it normally.
        source = copy_source('control-causal')
        client = source / 'mantis/__init__.py'
        original = client.read_bytes()
        client_start = original.index(b'class Client:')
        start = original.index(b'    @staticmethod\n    def _remaining(', client_start)
        end = original.index(b'    def preview(', start)
        legacy = (original[:start] + (REPO / 'tests/sdk/fixtures/control-before353.txt').read_bytes() +
                  original[end:]).replace(b'from google.protobuf.message import DecodeError\n', b'')
        # Job composition changes another portion of the whole-module oracle.
        # Reconstruct the ORIGINAL historical bytes, never reseal that oracle to
        # a hybrid of the old control methods and the current Job implementation.
        job_start = legacy.index(b'@dataclass(frozen=True)\nclass Job:')
        job_end = legacy.index(b'@dataclass(frozen=True)\nclass Preview:', job_start)
        legacy = (legacy[:job_start] + (REPO / 'tests/sdk/fixtures/job-before-deadlines.txt').read_bytes() + b'\n' +
                  legacy[job_end:]).replace(b'import math\n', b'')
        self.assertEqual(sha(legacy), 'ef9625080a80d1e5c8ca2ca9887d53e55aa160a6aeaf327e28a54a9865b71e20',
                         'ca04 historical client reconstruction changed')
        client.write_bytes(legacy)
        build(source, WORK / 'control-before353', kind='wheel')
        red_wheel = next((WORK / 'control-before353').glob('*.whl'))
        verify_wheel(red_wheel, client_source=legacy)
        # The new artifact/source-match gate must also reject an otherwise valid,
        # internally rehashed wheel containing the historical production client.
        with self.assertRaisesRegex(AssertionError, 'wheel client differs from qualified source'):
            verify_wheel(red_wheel)
        for runtime in ('3.20.0', '6.33.5'):
            python = install(red_wheel, 'control-before353-' + runtime, runtime)
            installed_control(python, runtime, source=legacy,
                              test='Control.test_trickling_progress_cannot_extend_exchange_deadline',
                              diagnostic='AssertionError: TimeoutError not raised')
            installed_control(python, runtime, source=legacy,
                              test='Control.test_malformed_protobuf_is_a_bounded_protocol_error',
                              diagnostic='google.protobuf.message.DecodeError:')
        client.write_bytes(original)
        self.assertEqual(sha(client.read_bytes()), sha(original), 'accepted source not restored exactly')
        build(source, WORK / 'control-restored', kind='wheel')
        restored = next((WORK / 'control-restored').glob('*.whl'))
        verify_wheel(restored)
        self.assertEqual(restored.read_bytes(), self.wheel.read_bytes())
        for runtime in ('3.20.0', '6.33.5'):
            python = install(restored, 'control-restored-' + runtime, runtime)
            installed_control(python, runtime)
        (WORK / 'control-sensitivity.json').write_text(json.dumps({
            'historical_commit': 'ca04e7ab056a5b12904f96ca5a0112cd1dd3dd7e',
            'historical_client_sha256': sha(legacy), 'restored_client_sha256': sha(client.read_bytes()),
            'accepted_client_sha256': sha(original), 'red_wheel_sha256': sha(red_wheel.read_bytes()),
            'restored_wheel_sha256': sha(restored.read_bytes()),
            'qualified_wheel_sha256': sha(self.wheel.read_bytes()),
            'protobuf_edges': ['3.20.0', '6.33.5'],
            'historical_installed_deadline_and_malformed': 'RED',
            'exact_restored_installed_full_control_suite': 'GREEN'}, indent=2) + '\n')

    def test_missing_stale_generated_and_missing_changed_schema_fail_closed(self):
        cases = {'missing-generated': 'mantis/mantis_pb2.py',
                 'stale-generated': 'mantis/mantis_pb2.py',
                 'missing-schema': '_schema/mantis.proto',
                 'changed-schema': '_schema/mantis.proto',
                 'missing-provenance': '_schema/provenance.json'}
        for name, relative in cases.items():
            with self.subTest(name=name):
                source = unpack(self.sdist, name)
                target = source / relative
                if name.startswith('missing'):
                    target.unlink()
                else:
                    target.write_bytes(target.read_bytes() + b'\n// corrupt\n' if name == 'changed-schema'
                                       else b'# stale generated code\n')
                out = WORK / (name + '-out')
                output = build(source, out, kind='wheel', expected=1)
                diagnostics = {
                    'missing-generated': ('FileNotFoundError', 'mantis_pb2.py'),
                    'stale-generated': ('RuntimeError: Missing or stale generated module',),
                    'missing-schema': ('FileNotFoundError', '_schema/mantis.proto'),
                    'changed-schema': ('RuntimeError: Schema/generated provenance mismatch',),
                    'missing-provenance': ('FileNotFoundError', '_schema/provenance.json'),
                }
                for diagnostic in diagnostics[name]:
                    self.assertIn(diagnostic, output, 'not the intended source rejection')
                self.assertFalse(out.exists() and any(out.iterdir()), 'partial artifact retained')
                self.assertFalse((source / 'build').exists(), 'build scratch leaked into source')
        source = copy_source('missing-canonical')
        (source.parent.parent / 'protocol/protobuf/mantis.proto').unlink()
        output = build(source, WORK / 'missing-canonical-out', expected=1, kind='sdist')
        self.assertIn('FileNotFoundError', output)
        self.assertIn('protocol/protobuf/mantis.proto', output)
        self.assertFalse((WORK / 'missing-canonical-out').exists() and any((WORK / 'missing-canonical-out').iterdir()))

    def test_checkout_stale_generated_is_not_a_fallback(self):
        source = copy_source('stale-checkout')
        (source / 'mantis/mantis_pb2.py').write_text('# obsolete\n')
        out = WORK / 'stale-checkout-out'
        output = build(source, out, expected=1, kind='wheel')
        self.assertIn('RuntimeError: Missing or stale generated module', output)
        self.assertFalse(out.exists() and any(out.iterdir()))

    def test_compiler_failure_version_and_runtime_mismatch_retire_scratch(self):
        source = copy_source('fault-backend')
        spec = importlib.util.spec_from_file_location('fault_backend', source / '_build_backend.py')
        backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(backend)
        scratch = WORK / 'fault-scratch'
        scratch.mkdir()
        from protoc import PROTOC_EXE
        real_version = backend._distribution_version
        with patch.object(tempfile, 'tempdir', str(scratch)):
            with patch('protoc.PROTOC_EXE', str(WORK / 'absent-compiler')):
                with self.assertRaises(FileNotFoundError) as rejected:
                    backend.build_wheel(str(WORK / 'no-compiler-output'))
                self.assertEqual(rejected.exception.filename, str(WORK / 'absent-compiler'))
            with patch.object(backend, '_run', return_value=b'libprotoc 99.0'):
                with self.assertRaisesRegex(RuntimeError, 'Compiler version mismatch'):
                    backend.build_wheel(str(WORK / 'wrong-compiler-output'))
            # Distribution and executed compiler versions must not be conflated.
            with patch.object(backend, '_distribution_version', side_effect=lambda name: '3.21.11' if name == 'protoc-wheel-0' else real_version(name)):
                with self.assertRaisesRegex(RuntimeError, 'Expected compiler distribution protoc-wheel-0==21.11; found 3.21.11'):
                    backend.build_wheel(str(WORK / 'wrong-distribution-output'))
            with patch.object(backend, '_run', return_value=b'libprotoc 21.11\n'):
                with self.assertRaisesRegex(RuntimeError, "executed b'libprotoc 21.11'"):
                    backend.build_wheel(str(WORK / 'conflated-compiler-output'))
            with patch.object(backend, '_distribution_version', side_effect=lambda name: '6.0' if name == 'protobuf' else real_version(name)):
                with self.assertRaisesRegex(RuntimeError, 'Expected build protobuf'):
                    backend.build_sdist(str(WORK / 'wrong-runtime-output'))
            real_run = backend._run
            def failed_compiler(args):
                if '--version' in args:
                    return real_run(args)
                # Real subprocess nonzero after partial generated output.
                output = next(arg.split('=', 1)[1] for arg in args if arg.startswith('--python_out='))
                Path(output, 'mantis_pb2.py').write_bytes(b'partial')
                return real_run([PROTOC_EXE, '--not-a-real-protoc-option'])
            with patch.object(backend, '_run', side_effect=failed_compiler):
                with self.assertRaises(subprocess.CalledProcessError) as rejected:
                    backend.build_wheel(str(WORK / 'failed-compiler-output'))
                self.assertEqual(rejected.exception.cmd, [PROTOC_EXE, '--not-a-real-protoc-option'])
                self.assertEqual(rejected.exception.stderr, b'Missing value for flag: --not-a-real-protoc-option\n')
        self.assertEqual(list(scratch.iterdir()), [], 'partial generation scratch retained')
        for name in ('no-compiler-output', 'wrong-compiler-output', 'wrong-runtime-output', 'failed-compiler-output',
                     'wrong-distribution-output', 'conflated-compiler-output'):
            self.assertFalse((WORK / name).exists(), name)

    def test_versioned_sdist_provenance_rejection_and_restoration(self):
        source = unpack(self.sdist, 'identity-provenance')
        manifest = source / '_schema/provenance.json'
        original = manifest.read_bytes()
        for field, wrong in (('format', 1), ('compiler_distribution', {'name': 'protoc-wheel-0', 'version': '3.21.11'}),
                             ('compiler_version', 'libprotoc 21.11')):
            changed = json.loads(original)
            changed[field] = wrong
            manifest.write_text(json.dumps(changed))
            out = WORK / ('wrong-provenance-' + field)
            output = build(source, out, expected=1, kind='wheel')
            self.assertIn('RuntimeError: Schema/generated provenance mismatch', output)
            self.assertFalse(out.exists() and any(out.iterdir()))
        manifest.write_bytes(original)
        self.assertEqual(sha(manifest.read_bytes()), sha(original))
        build(source, WORK / 'identity-provenance-restored', kind='wheel')
        restored = next((WORK / 'identity-provenance-restored').glob('*.whl'))
        self.assertEqual(restored.read_bytes(), self.wheel.read_bytes())
        verify_wheel(restored)

    def test_malformed_tampered_wheels_and_installed_missing_module(self):
        malformed = WORK / 'malformed.whl'
        malformed.write_bytes(b'not a wheel')
        with self.assertRaises(zipfile.BadZipFile):
            verify_wheel(malformed)
        with zipfile.ZipFile(self.wheel) as original:
            original_files = {name: original.read(name) for name in original.namelist()}
        for mode in ('missing', 'stale', 'schema', 'native', 'traversal', 'stale-rehashed', 'schema-rehashed'):
            files = original_files.copy()
            if mode == 'missing':
                del files['mantis/mantis_pb2.py']
            elif mode.startswith('stale'):
                files['mantis/mantis_pb2.py'] = b'# stale\n'
            elif mode.startswith('schema'):
                files['mantis/mantis.proto'] += b'\n// changed\n'
            elif mode == 'native':
                files['mantis/private.so'] = b'native'
            else:
                files['../escaped.py'] = b'bad'
            if mode.endswith('rehashed'):
                record_name = 'mantis_studio_client-0.1.0.dist-info/RECORD'
                rows = io.StringIO()
                writer = csv.writer(rows, lineterminator='\n')
                for name, data in files.items():
                    if name != record_name:
                        digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode()
                        writer.writerow([name, 'sha256=' + digest, len(data)])
                writer.writerow([record_name, '', ''])
                files[record_name] = rows.getvalue().encode()
            artifact = WORK / (mode + '.whl')
            with zipfile.ZipFile(artifact, 'w') as out:
                for name, data in files.items():
                    out.writestr(name, data)
            with self.subTest(mode=mode), self.assertRaises(AssertionError) as rejected:
                verify_wheel(artifact)
            if mode.endswith('rehashed'):
                self.assertIn('stale generated module' if mode.startswith('stale') else 'noncanonical schema',
                              str(rejected.exception))
        python = install(self.wheel, 'installed-removed', '3.20.0')
        module = next(python.parent.parent.glob('lib/python*/site-packages/mantis/mantis_pb2.py'))
        original = module.read_bytes()
        module.unlink()
        output = command([python, '-I', PROBE], expected=1)
        self.assertIn('mantis_pb2', output)
        module.write_bytes(original)
        command([python, '-I', PROBE, WORK / 'canonical.pb'])

    def test_frozen_input_upload_survives_hidden_exclusion(self):
        # Model only upload-artifact's documented default hidden-path exclusion,
        # not the remote service. Actual downloaded CI bundles remain a review gate.
        manifest = json.loads((WORK / 'inputs.json').read_text())
        hidden = '.github/workflows/python-sdk.yml'
        self.assertIn(hidden, manifest)
        visible = {name for name in manifest
                   if not any(part.startswith('.') for part in PurePosixPath(name).parts)}
        self.assertEqual(set(manifest) - visible, {hidden})
        # The rejected directory export loses exactly this qualified input.
        with self.assertRaisesRegex(AssertionError, 'missing frozen input'):
            self.assertTrue(set(manifest) <= visible, 'missing frozen input')
        # The nonhidden ZIP itself survives the same filter; its member names do
        # not pass through that filter. Compare actual bytes, not a path string.
        export = WORK / 'frozen-inputs.zip'
        self.assertFalse(any(part.startswith('.') for part in export.name.split('/')))
        self.assertTrue(export.is_file(), 'missing complete frozen-input export')
        with zipfile.ZipFile(export) as archive:
            self.assertEqual(len(archive.namelist()), len(manifest))
            self.assertEqual(set(archive.namelist()), {'frozen-inputs/' + name for name in manifest})
            for name, digest in manifest.items():
                self.assertEqual(sha(archive.read('frozen-inputs/' + name)), digest, name)
        # Unlisted/private files beside frozen inputs must never enter export.
        scratch = WORK / 'export-sensitivity'
        shutil.copytree(WORK / 'frozen-inputs', scratch / 'frozen-inputs')
        secret = scratch / 'frozen-inputs/.unlisted-private'
        secret.write_text('unqualified fixture, must not be exported')
        write_frozen_input_export(scratch, manifest)
        self.assertEqual((scratch / 'frozen-inputs.zip').read_bytes(), export.read_bytes())
        selected = scratch / 'frozen-inputs' / hidden
        original = selected.read_bytes()
        selected.write_bytes(original + b'\n# corrupt fixture\n')
        with self.assertRaisesRegex(AssertionError, 'frozen input hash mismatch'):
            write_frozen_input_export(scratch, manifest)
        selected.write_bytes(original)
        selected.unlink()
        with self.assertRaises(FileNotFoundError):
            write_frozen_input_export(scratch, manifest)
        selected.write_bytes(original)
        write_frozen_input_export(scratch, manifest)
        self.assertEqual((scratch / 'frozen-inputs.zip').read_bytes(), export.read_bytes())
        for invalid in ('../escape', '/absolute', 'a/../escape', 'a\\b', './alias'):
            with self.subTest(path=invalid), self.assertRaisesRegex(AssertionError, 'unsafe frozen input path'):
                write_frozen_input_export(scratch, {invalid: sha(b'')})
        selected.unlink()
        selected.symlink_to(WORK / 'frozen-inputs' / hidden)
        with self.assertRaisesRegex(AssertionError, 'symlink frozen input'):
            write_frozen_input_export(scratch, manifest)
        selected.unlink()
        selected.write_bytes(original)
        write_frozen_input_export(scratch, manifest)
        self.assertEqual((scratch / 'frozen-inputs.zip').read_bytes(), export.read_bytes())

    def test_disposable_old_backend_red_exact_restored_green(self):
        source = copy_source('bypass')
        project = source / 'pyproject.toml'
        original = project.read_bytes()
        original_digest = sha(original)
        project.write_text(original.decode().replace('build-backend = "_build_backend"',
                                                     'build-backend = "setuptools.build_meta"')
                           .replace('backend-path = ["."]\n', ''))
        out = WORK / 'bypass-red'
        command([sys.executable, '-m', 'build', '--no-isolation', '--wheel', '--outdir', out, source])
        with self.assertRaises(AssertionError):
            verify_wheel(next(out.glob('*.whl')))
        root = WORK / 'installed/bypass-red'
        venv.EnvBuilder(with_pip=False, symlinks=(os.name != 'nt')).create(root)
        python = root / 'bin/python'
        command([sys.executable, '-m', 'pip', '--python', python, 'install',
                 'protobuf==3.20.0', next(out.glob('*.whl'))])
        output = command([python, '-I', PROBE], expected=1)
        self.assertIn('mantis_pb2', output)
        project.write_bytes(original)
        self.assertEqual(sha(project.read_bytes()), original_digest)
        build(source, WORK / 'bypass-restored', kind='wheel')
        restored = next((WORK / 'bypass-restored').glob('*.whl'))
        verify_wheel(restored)
        self.assertEqual(restored.read_bytes(), self.wheel.read_bytes())
        install(restored, 'bypass-restored', '3.20.0')
        (WORK / 'sensitivity.json').write_text(json.dumps({'original_pyproject_sha256': original_digest,
            'restored_pyproject_sha256': sha(project.read_bytes()), 'old_backend_installed_import': 'RED',
            'restored_backend_installed_import': 'GREEN'}) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--expected-machine', required=True, choices=('x86_64', 'aarch64'))
    parser.add_argument('--expected-python', required=True, choices=('3.10', '3.11', '3.12', '3.13', '3.14'))
    parser.add_argument('--identity-only', action='store_true')
    args, tests = parser.parse_known_args()
    WORK = args.work_dir.resolve()
    if WORK.is_relative_to(REPO):
        parser.error('--work-dir must be outside the original source/build tree')
    WORK.mkdir(parents=True, exist_ok=False)
    (WORK / 'tmp').mkdir()
    EXPECTED_MACHINE, EXPECTED_PYTHON = args.expected_machine, args.expected_python
    identity = execution_identity(EXPECTED_MACHINE, EXPECTED_PYTHON)
    (WORK / 'execution.json').write_text(json.dumps(identity, indent=2) + '\n')
    inputs = [*sorted((REPO / 'sdk/python').rglob('*')), *sorted((REPO / 'tests/sdk').rglob('*')),
              SCHEMA, REPO / '.github/workflows/python-sdk.yml',
              REPO / 'tests/ci/python_sdk_workflow_test.py',
              REPO / 'tests/ci/python_control_workflow_test.py',
              *(REPO / name for name in ('tests/unit/python_calibration.py', 'tests/unit/python_capture.py',
                                         'tests/unit/python_control.py', 'tests/unit/python_job.py',
                                         'tests/integration/acceptance.py',
                                         'tests/contract/protocol_fields.py'))]
    hashes = {str(path.relative_to(REPO)): sha(path.read_bytes()) for path in inputs
              if path.is_file() and '__pycache__' not in path.parts}
    (WORK / 'inputs.json').write_text(json.dumps(hashes, indent=2) + '\n')
    for relative in hashes:
        frozen = WORK / 'frozen-inputs' / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / relative, frozen)
    write_frozen_input_export(WORK, hashes)
    if not args.identity_only:
        result = unittest.main(argv=[sys.argv[0]] + tests, verbosity=2, exit=False).result
        for relative, digest in hashes.items():
            assert sha((REPO / relative).read_bytes()) == digest, 'input changed during artifact qualification: ' + relative
            assert sha((WORK / 'frozen-inputs' / relative).read_bytes()) == digest, 'frozen input changed: ' + relative
        sys.exit(0 if result.wasSuccessful() else 1)
