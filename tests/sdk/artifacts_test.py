"""Python-only Stage A qualification; all commands/logs retained in --work-dir.

Run in a venv containing build==1.2.2.post1 and pyproject's exact build tools.
No daemon, CMake, compiler campaign or source-tree PYTHONPATH is used.
"""
import argparse
import base64
import csv
from email.parser import BytesParser
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
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


def sha(data):
    return hashlib.sha256(data).hexdigest()


def command(args, *, cwd=None, expected=0, env=None, timeout=180):
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
    timed_out = False
    with (WORK / (label + '.log')).open('wb') as log:
        process = subprocess.Popen([str(arg) for arg in args], cwd=cwd or WORK,
                                   env=settings, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=(os.name != 'nt'))
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            if os.name == 'nt':
                process.terminate()
            else:
                os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if os.name == 'nt':
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
    receipt = {'argv': [str(arg) for arg in args], 'cwd': str(cwd or WORK),
               'exit': process.returncode, 'timed_out': timed_out, 'timeout_seconds': timeout,
               'log': label + '.log',
               'controlled_environment': {key: settings[key] for key in
                   ('SOURCE_DATE_EPOCH', 'PYTHONHASHSEED', 'PIP_ONLY_BINARY')}}
    with (WORK / 'commands.jsonl').open('a', encoding='utf-8') as out:
        out.write(json.dumps(receipt) + '\n')
    output = (WORK / (label + '.log')).read_text(errors='replace')
    if (expected == 0 and (process.returncode != 0 or timed_out)) or (expected != 0 and process.returncode == 0):
        raise AssertionError(f'command exit {process.returncode}, expected {expected}; {label}.log\n{output[-5000:]}')
    return output


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
    return command(args + ['--outdir', out, source], expected=expected)


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


def verify_wheel(path):
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
    assert provenance['protoc'] == '3.20.1' and provenance['build_protobuf'] == '3.20.0'
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

    def test_command_timeout_retains_receipt_and_reaps_child(self):
        pidfile = WORK / 'hang-child.pid'
        output = command([sys.executable, PROBE.with_name('hang_fixture.py'), pidfile],
                         timeout=2, expected=1)
        self.assertIn('parent-and-child-ready', output)
        receipt = json.loads((WORK / 'commands.jsonl').read_text().splitlines()[-1])
        self.assertTrue(receipt['timed_out'])
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pidfile.read_text()), 0)

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
        command([python, '-m', 'pip', 'wheel', '--no-deps',
                 '--wheel-dir', output, self.sdist])
        wheel = next(output.glob('*.whl'))
        verify_wheel(wheel)
        self.assertEqual(wheel.read_bytes(), self.wheel.read_bytes())
        install(wheel, 'pip-sdist-wheel', '3.20.0')

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
                # Existing tests' build/python entry is deliberately nonexistent:
                # -I plus installed module-path probe prevents checkout fallback.
                for script in ('tests/unit/python_calibration.py', 'tests/unit/python_capture.py',
                               'tests/contract/protocol_fields.py'):
                    command([python, '-I', REPO / script, WORK / 'absent-native-build'])

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
                build(source, out, kind='wheel', expected=1)
                self.assertFalse(out.exists() and any(out.iterdir()), 'partial artifact retained')
                self.assertFalse((source / 'build').exists(), 'build scratch leaked into source')
        source = copy_source('missing-canonical')
        (source.parent.parent / 'protocol/protobuf/mantis.proto').unlink()
        build(source, WORK / 'missing-canonical-out', expected=1, kind='sdist')
        self.assertFalse((WORK / 'missing-canonical-out').exists() and any((WORK / 'missing-canonical-out').iterdir()))

    def test_checkout_stale_generated_is_not_a_fallback(self):
        source = copy_source('stale-checkout')
        (source / 'mantis/mantis_pb2.py').write_text('# obsolete\n')
        out = WORK / 'stale-checkout-out'
        build(source, out, expected=1, kind='wheel')
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
                with self.assertRaises(FileNotFoundError):
                    backend.build_wheel(str(WORK / 'no-compiler-output'))
            with patch.object(backend, '_run', return_value=b'libprotoc 99.0'):
                with self.assertRaisesRegex(RuntimeError, 'Compiler version mismatch'):
                    backend.build_wheel(str(WORK / 'wrong-compiler-output'))
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
                with self.assertRaises(subprocess.CalledProcessError):
                    backend.build_wheel(str(WORK / 'failed-compiler-output'))
        self.assertEqual(list(scratch.iterdir()), [], 'partial generation scratch retained')
        for name in ('no-compiler-output', 'wrong-compiler-output', 'wrong-runtime-output', 'failed-compiler-output'):
            self.assertFalse((WORK / name).exists(), name)

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

    def test_disposable_old_backend_red_exact_restored_green(self):
        source = copy_source('bypass')
        project = source / 'pyproject.toml'
        original = project.read_bytes()
        original_digest = sha(original)
        project.write_text(original.decode().replace('build-backend = "_build_backend"',
                                                     'build-backend = "setuptools.build_meta"')
                           .replace('backend-path = ["."]\n', ''))
        out = WORK / 'bypass-red'
        build(source, out, kind='wheel')
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
    args, tests = parser.parse_known_args()
    WORK = args.work_dir.resolve()
    if WORK.is_relative_to(REPO):
        parser.error('--work-dir must be outside the original source/build tree')
    WORK.mkdir(parents=True, exist_ok=False)
    (WORK / 'tmp').mkdir()
    unittest.main(argv=[sys.argv[0]] + tests, verbosity=2)
