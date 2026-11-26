"""LIGHT fixture/oracle controls: these never launch a native product."""
import io
from contextlib import redirect_stdout
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import live_daemon as driver

spec = importlib.util.spec_from_file_location('live_probe', Path(__file__).with_name('live_daemon_probe.py'))
assert spec is not None and spec.loader is not None
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='live75-fixture-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.previous = ctypes.c_int()
        self.assertEqual(self.libc.prctl(37, ctypes.byref(self.previous), 0, 0, 0), 0)

    def tearDown(self):
        current = ctypes.c_int()
        self.assertEqual(self.libc.prctl(37, ctypes.byref(current), 0, 0, 0), 0)
        self.assertEqual(current.value, self.previous.value, 'subreaper state leaked')
        self.assertFalse(any(t.name == 'live-daemon-output' for t in __import__('threading').enumerate()))

    def fixture(self, source, marker=b'fixture ready'):
        script = self.root / 'fixture.py'
        script.write_text(source)
        return probe.OwnedDaemon([sys.executable, '-I', '-B', str(script)],
                                 os.environ.copy(), self.root, marker)

    def test_readiness_and_normal_exit(self):
        owner = self.fixture('print("fixture ready", flush=True)\ninput()\n')
        # stdin is not a lifecycle gate: use a pipe owned by the test instead.
        script = self.root / 'fixture.py'
        script.write_text('import os\nprint("fixture ready", flush=True)\nos.read(0,1)\n')
        read, write = os.pipe()
        try:
            original = subprocess.Popen
            def launch(*args, **kwargs):
                return original(*args, stdin=read, **kwargs)
            with patch.object(probe.subprocess, 'Popen', launch):
                with owner:
                    owner.check()
                    os.write(write, b'x')
                    self.assertEqual(owner.process.wait(timeout=5), 0)
            with self.assertRaises(ProcessLookupError):
                os.killpg(owner.process.pid, 0)
        finally:
            os.close(read)
            os.close(write)

    def test_missing_marker_is_not_ready(self):
        owner = self.fixture('print("not ready", flush=True)\n')
        with self.assertRaisesRegex(RuntimeError, 'exited|readiness'):
            with owner:
                self.fail('accepted missing marker')
        self.assertIsNotNone(owner.process.returncode)

    def test_output_overflow_is_not_success(self):
        owner = self.fixture('import os\nos.write(1,b"x" * 131072)\n')
        with self.assertRaisesRegex(RuntimeError, 'output bound'):
            with owner:
                self.fail('accepted overflow')
        self.assertLessEqual(len(owner.output), probe.LOG_LIMIT)
        self.assertIsNotNone(owner.process.returncode)

    def test_resistant_descendant_and_unrelated_child_on_exception(self):
        # Readiness is written only after the actual descendant has installed
        # SIGTERM resistance. No sleep establishes the relevant ordering.
        owner = self.fixture('''import os, signal
r,w=os.pipe()
pid=os.fork()
if pid == 0:
    os.close(r)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.write(w,b'x')
    os.close(w)
    signal.pause()
else:
    os.close(w)
    os.read(r,1)
    os.close(r)
    print('fixture ready',flush=True)
    signal.pause()
''')
        unrelated_script = self.root / 'unrelated.py'
        unrelated_script.write_text('import signal\nsignal.pause()\n')
        unrelated = subprocess.Popen([sys.executable, '-I', '-B', unrelated_script],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        primary = OSError(5, 'initiating fixture failure')
        try:
            with self.assertRaises(OSError) as caught:
                with owner:
                    raise primary
            self.assertIs(caught.exception, primary)
            self.assertEqual(caught.exception.errno, 5)
            self.assertIsNone(unrelated.poll(), 'cleanup signalled unrelated child')
            with self.assertRaises(ProcessLookupError):
                os.killpg(owner.process.pid, 0)
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)

    def test_cleanup_failure_preserves_errno_and_identity(self):
        primary = OSError(28, 'first fixture error')
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')

        # A failure restoring the observation setting occurs after real cleanup;
        # the test restores it explicitly so the injected diagnostic cannot leak.
        with self.assertRaises(OSError) as caught:
            with owner:
                actual = owner.libc
                class BrokenRestore:
                    def prctl(self, *args):
                        actual.prctl(*args)
                        return -1
                owner.libc = BrokenRestore()
                raise primary
        self.assertIs(caught.exception, primary)
        self.assertEqual(primary.errno, 28)
        self.assertIn('cleanup failed', str(primary.__cause__))

    def assert_retired(self, owner):
        self.assertIsNotNone(owner.process.poll())
        self.assertTrue(owner.process.stdout.closed)
        self.assertFalse(owner.reader.is_alive())
        with self.assertRaises(ProcessLookupError):
            os.killpg(owner.process.pid, 0)
        current = ctypes.c_int()
        self.assertEqual(self.libc.prctl(37, ctypes.byref(current), 0, 0, 0), 0)
        self.assertEqual(current.value, self.previous.value)

    def check_fault(self, owner, operation, primary):
        # Preserve the actual traceback, not assertRaises' cleared traceback.
        def initiating_boundary():
            operation()
        caught, names = None, []
        expected_args = primary.args
        expected_errno = getattr(primary, 'errno', None)
        try:
            initiating_boundary()
        except BaseException as error:
            caught = error
            names = []
            traceback = error.__traceback__
            while traceback is not None:
                names.append(traceback.tb_frame.f_code.co_name)
                traceback = traceback.tb_next
        try:
            self.assertIs(caught, primary)
            assert caught is not None
            self.assertIn('initiating_boundary', names)
            self.assertIn('operation', names)
            self.assertEqual(caught.args, expected_args)
            self.assertEqual(getattr(caught, 'errno', None), expected_errno)
            self.assert_retired(owner)
        finally:
            # Even the rejected implementation must not contaminate later tests.
            try:
                if owner.process is not None:
                    stream = owner.process.stdout
                    getattr(stream, 'stream', stream).close()
            finally:
                self.assertEqual(self.libc.prctl(36, self.previous.value, 0, 0, 0), 0)

    def test_partial_reader_start_preserves_primary_and_restores_resources(self):
        owner = self.fixture('import signal\nsignal.pause()\n')
        primary = RuntimeError("can't start new thread")
        def operation():
            with patch.object(probe.threading.Thread, 'start', side_effect=primary):
                with owner:
                    self.fail('accepted failed thread start')
        self.check_fault(owner, operation, primary)

    def test_reader_started_then_start_throws_is_still_joined(self):
        owner = self.fixture('import signal\nsignal.pause()\n')
        primary = OSError(11, 'thread startup interrupted')
        start = probe.threading.Thread.start
        def interrupted(thread):
            start(thread)
            raise primary
        def operation():
            with patch.object(probe.threading.Thread, 'start', interrupted):
                with owner:
                    self.fail('accepted interrupted thread start')
        self.check_fault(owner, operation, primary)

    def test_throwing_stream_close_preserves_enospc_and_restores_resources(self):
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')
        primary = OSError(28, 'original ENOSPC')
        class CloseFault:
            def __init__(self, stream):
                self.stream = stream
            def __getattr__(self, name):
                return getattr(self.stream, name)
            def close(self):
                self.stream.close()
                raise OSError(5, 'pipe close EIO')
        def operation():
            with owner:
                owner.process.stdout = CloseFault(owner.process.stdout)
                raise primary
        # Unwrap only in the test's emergency cleanup, after checking the result.
        try:
            self.check_fault(owner, operation, primary)
            self.assertEqual(primary.errno, 28)
            self.assertIn('cleanup failed: OSError', str(primary.__cause__))
        finally:
            owner.process.stdout = owner.process.stdout.stream

    def test_join_failure_does_not_skip_stream_and_state_restoration(self):
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')
        primary = OSError(28, 'original ENOSPC')
        def operation():
            with owner:
                join = owner.reader.join
                def interrupted(*args, **kwargs):
                    join(*args, **kwargs)
                    raise OSError(4, 'join interrupted after retirement')
                owner.reader.join = interrupted
                raise primary
        self.check_fault(owner, operation, primary)
        self.assertIn('cleanup failed: InterruptedError', str(primary.__cause__))

    def test_close_error_without_primary_is_not_swallowed(self):
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')
        failure = OSError(5, 'pipe close EIO')
        owner.__enter__()
        try:
            owner.process.terminate()
            owner.process.wait(timeout=5)
            close = owner.process.stdout.close
            def failing_close():
                close()
                raise failure
            with patch.object(owner.process.stdout, 'close', failing_close):
                with self.assertRaises(OSError) as caught:
                    owner.__exit__(None, None, None)
            self.assertIs(caught.exception, failure)
            self.assert_retired(owner)
        finally:
            owner.process.stdout.close()
            self.assertEqual(self.libc.prctl(36, self.previous.value, 0, 0, 0), 0)

    def test_injected_exception_retirement_and_receipt(self):
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')
        primary = probe.InjectedFailure('Injected installed-daemon post-Job failure')
        client = SimpleNamespace(snapshot=lambda: (_ for _ in ()).throw(ConnectionRefusedError()))
        output = io.StringIO()
        with redirect_stdout(output), self.assertRaises(probe.InjectedFailure) as caught:
            with probe.owned_session(owner.argv, owner.env, owner.cwd, owner.ready_line, client):
                raise primary
        self.assertIs(caught.exception, primary)
        self.assertIsNone(primary.__cause__)
        record = json.loads(output.getvalue())
        self.assertTrue(record['retired'])
        self.assertFalse(record['successful'])

    def test_ordinary_live_exit_is_not_hidden_by_forced_cleanup(self):
        owner = self.fixture('import signal\nprint("fixture ready",flush=True)\nsignal.pause()\n')
        with self.assertRaisesRegex(RuntimeError, 'not shut down'):
            with owner:
                pass
        with self.assertRaises(ProcessLookupError):
            os.killpg(owner.process.pid, 0)


class Oracle(unittest.TestCase):
    def test_checkout_classes_cannot_impersonate_installed_origins(self):
        class Client:
            def _call(self):
                pass
        class Job:
            def wait(self):
                pass
            def wait_report(self):
                pass
        fake = SimpleNamespace(Client=Client, Job=Job)
        with self.assertRaisesRegex(AssertionError, 'non-installed SDK origin'):
            probe.installed_identity(fake, SimpleNamespace(__file__=__file__), {})

    def test_executable_mapping_origin_is_checked(self):
        with tempfile.TemporaryDirectory(prefix='live75-map-') as directory:
            binary = Path(directory) / 'mantisd'
            binary.write_bytes(b'fixture, not native')
            mapping = '1000-2000 r-xp 0000 00:00 1 ' + str(binary) + '\n'
            with patch.object(probe.Path, 'read_text', return_value=mapping):
                self.assertEqual(probe.loaded_identity(123, binary), {'mantisd': probe.digest(binary)})
            with patch.object(probe.Path, 'read_text', return_value=mapping +
                              '2000-3000 r-xp 0000 00:00 2 /etc/hosts\n'):
                with self.assertRaisesRegex(AssertionError, 'mapping origin'):
                    probe.loaded_identity(123, binary)

    def test_only_exact_hash_bound_fixture_mappings_are_allowed(self):
        with tempfile.TemporaryDirectory(prefix='live75-map-') as directory:
            root = Path(directory)
            binary, plugin, foreign = (root / n for n in ('mantisd', 'virtual.so', 'foreign.so'))
            for path in (binary, plugin, foreign):
                path.write_bytes(path.name.encode())
            daemon_map = '1000-2000 r-xp 0000 00:00 1 ' + str(binary) + '\n'
            plugin_map = '2000-3000 r-xp 0000 00:00 2 ' + str(plugin) + '\n'
            expected = {plugin: probe.digest(plugin)}
            with patch.object(probe.Path, 'read_text', return_value=daemon_map + plugin_map):
                self.assertEqual(probe.loaded_identity(123, binary, expected),
                                 {p.name: probe.digest(p) for p in (binary, plugin)})
            with patch.object(probe.Path, 'read_text', return_value=daemon_map):
                with self.assertRaisesRegex(AssertionError, 'missing fixture executable mapping'):
                    probe.loaded_identity(123, binary, expected)
            with patch.object(probe.Path, 'read_text', return_value=daemon_map + plugin_map):
                with self.assertRaisesRegex(AssertionError, 'fixture executable hash mismatch'):
                    probe.loaded_identity(123, binary, {plugin: '0' * 64})
            with patch.object(probe.Path, 'read_text', return_value=daemon_map + plugin_map +
                              '3000-4000 r-xp 0000 00:00 3 ' + str(foreign) + '\n'):
                with self.assertRaisesRegex(AssertionError, 'mapping origin'):
                    probe.loaded_identity(123, binary, expected)

    def test_success_is_rejected_by_negative_oracle(self):
        with self.assertRaisesRegex(AssertionError, 'unexpectedly succeeded'):
            probe.require_error(lambda: None, ValueError)

    def test_wrong_error_type_is_not_a_negative_pass(self):
        with self.assertRaises(TypeError):
            probe.require_error(lambda: (_ for _ in ()).throw(TypeError('wrong')), ValueError)

    def test_injected_failure_oracle_rejects_wrong_exit_and_cleanup(self):
        text = json.dumps({'phase': 'installed-live-daemon-injected-failure',
                           'successful': False, 'retired': True}) + '\n' + \
            'InjectedFailure: Injected installed-daemon post-Job failure\n'
        driver.require_injected_failure(1, text)
        for status in (0, -9, 2):
            with self.subTest(status=status), self.assertRaises(AssertionError):
                driver.require_injected_failure(status, text)
        with self.assertRaisesRegex(AssertionError, 'cleanup failed'):
            driver.require_injected_failure(1, text + 'live-daemon cleanup failed')
        with self.assertRaises(ValueError):
            driver.require_injected_failure(1, 'InjectedFailure: Injected installed-daemon post-Job failure')
        with self.assertRaisesRegex(AssertionError, 'unconfirmed'):
            driver.require_injected_failure(1, text.replace('"retired": true', '"retired": false'))

    def test_first_error_secondary_preserves_errno(self):
        primary = OSError(5, 'original')
        probe.secondary(primary, RuntimeError('private secondary'))
        self.assertEqual(primary.args, (5, 'original'))
        self.assertEqual(primary.errno, 5)
        self.assertNotIn('private secondary', str(primary.__cause__))


class ExportOracle(unittest.TestCase):
    # Independent analytic grid, not a saved producer payload/digest or a call to
    # the validator to generate its own expected points. Different sequence phases
    # must all remain valid (capture timing is deliberately not fixed).
    HEADER = ('ply\nformat ascii 1.0\ncomment units millimeters\nelement vertex 3072\n'
              'property float x\nproperty float y\nproperty float z\nend_header\n')

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='live75-export-')
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'points.ply'
        self.rows = [f'{column - 32} {row - 24} {((column * 3 + row * 5 + 7) % 256) / 16}\n'
                     for row in range(48) for column in range(64)]

    def validate(self, text):
        self.path.write_text(text, encoding='ascii')
        probe.validate_export(self.path)

    def test_analytic_grid_and_capture_phase(self):
        self.validate(self.HEADER + ''.join(self.rows))
        other = [f'{x - 32} {y - 24} {((3*x + 5*y + 253) % 256) / 16}\n'
                 for y in range(48) for x in range(64)]
        self.validate(self.HEADER + ''.join(other))

    def test_empty_one_truncated_extra_and_garbage_bodies(self):
        for rows in ([], self.rows[:1], self.rows[:-1], self.rows + self.rows[:1],
                     ['garbage xyz\n'] * 3072, ['1 2\n'] * 3072, ['1 2 3 4\n'] * 3072):
            with self.subTest(length=len(rows)), self.assertRaises(probe.ExportValidationError):
                self.validate(self.HEADER + ''.join(rows))
        self.validate(self.HEADER + ''.join(self.rows))  # Exact restoration.

    def test_nonfinite_anywhere_in_body_is_rejected(self):
        for value in ('nan', 'inf', '-inf', '1e999'):
            rows = list(self.rows)
            rows[123] = '1 2 ' + value + '\n'  # Not one of the geometry samples.
            with self.subTest(value=value), self.assertRaisesRegex(probe.ExportValidationError, 'nonfinite'):
                self.validate(self.HEADER + ''.join(rows))

    def test_structure_properties_units_and_size_are_checked(self):
        body = ''.join(self.rows)
        for header in (self.HEADER.replace('3072', '1'),
                       self.HEADER.replace('ascii', 'binary_little_endian'),
                       self.HEADER.replace('property float x\n', ''),
                       self.HEADER.replace('property float y', 'property float x'),
                       self.HEADER.replace('millimeters', 'meters'),
                       self.HEADER.replace('end_header', 'element face 0\nend_header')):
            with self.subTest(header=header), self.assertRaisesRegex(probe.ExportValidationError, 'header'):
                self.validate(header + body)
        with self.assertRaisesRegex(probe.ExportValidationError, 'export size'):
            self.validate('x' * (1024 * 1024))
        self.path.write_bytes(b'\xff')
        with self.assertRaisesRegex(probe.ExportValidationError, 'non-ASCII'):
            probe.validate_export(self.path)

    def test_finite_but_wrong_grid_or_intensity_is_rejected(self):
        for index, line in ((0, '-32 -24 0.01\n'), (64, '-32 -23 0\n'),
                            (24*64+32, '0 1 0\n'), (3071, '31 23 0\n')):
            rows = list(self.rows)
            rows[index] = line
            with self.subTest(index=index), self.assertRaises(probe.ExportValidationError):
                self.validate(self.HEADER + ''.join(rows))

    def test_invalid_export_exit_error_and_retirement_oracle(self):
        record = {'phase': 'installed-live-daemon-invalid-export', 'successful': False, 'retired': True}
        text = json.dumps(record) + '\nExportValidationError: PLY vertex count mismatch\n'
        self.assertEqual(driver.require_invalid_export(1, text), record)
        for status, output in ((0, text), (2, text), (-9, text),
                               (1, text + 'live-daemon cleanup failed'),
                               (1, text.replace('count mismatch', 'unrelated failure')),
                               (1, text.replace('"retired": true', '"retired": false'))):
            with self.subTest(status=status, output=output), self.assertRaises(AssertionError):
                driver.require_invalid_export(status, output)
        with self.assertRaises(ValueError):
            driver.require_invalid_export(1, 'ExportValidationError: PLY vertex count mismatch')


class VirtualFixture(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='live75-plugin-fixture-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.runtime, self.work = self.root / 'runtime', self.root / 'work'
        (self.runtime / 'plugins').mkdir(parents=True)
        (self.runtime / 'recipes').mkdir()
        self.work.mkdir()
        (self.runtime / 'recipes/example.json').write_bytes((driver.ROOT / 'recipes/example.json').read_bytes())
        for name in ('virtual-scanner', 'example-points', 'ply', 'x1'):
            (self.runtime / 'plugins' / (name + '.json')).write_text(json.dumps({
                'id': 'org.mantis.' + name, 'library': name + '.so'}))
            (self.runtime / 'plugins' / (name + '.so')).write_bytes(b'unit fixture, not native')

    def test_only_virtual_workflow_plugins_are_selected(self):
        plugins, recipes, identities = driver.virtual_fixture(self.runtime, self.work)
        self.assertEqual(len(identities), 3)
        self.assertFalse((plugins / 'x1.json').exists())
        self.assertFalse((plugins / 'x1.so').exists())
        self.assertEqual((recipes / 'example.json').read_bytes(), (driver.ROOT / 'recipes/example.json').read_bytes())

    def test_missing_library_is_rejected(self):
        (self.runtime / 'plugins/virtual-scanner.so').unlink()
        with self.assertRaisesRegex(AssertionError, 'missing plugin library'):
            driver.virtual_fixture(self.runtime, self.work)

    def test_symlinked_library_is_not_an_owned_runtime_closure(self):
        library = self.runtime / 'plugins/virtual-scanner.so'
        foreign = self.root / 'foreign.so'
        library.rename(foreign)
        library.symlink_to(foreign)
        with self.assertRaisesRegex(AssertionError, 'unsafe/missing plugin library'):
            driver.virtual_fixture(self.runtime, self.work)

    def test_symlinked_manifest_is_rejected(self):
        manifest = self.runtime / 'plugins/virtual-scanner.json'
        foreign = self.root / 'foreign.json'
        manifest.rename(foreign)
        manifest.symlink_to(foreign)
        with self.assertRaisesRegex(AssertionError, 'unsafe/missing plugin library'):
            driver.virtual_fixture(self.runtime, self.work)

    def test_parent_traversal_library_is_rejected(self):
        manifest = self.runtime / 'plugins/virtual-scanner.json'
        entry = json.loads(manifest.read_text())
        entry['library'] = '../foreign.so'
        manifest.write_text(json.dumps(entry))
        (self.runtime / 'foreign.so').write_bytes(b'unit fixture, not native')
        with self.assertRaisesRegex(AssertionError, 'unsafe/missing plugin library'):
            driver.virtual_fixture(self.runtime, self.work)

    def test_noncanonical_recipe_is_rejected(self):
        (self.runtime / 'recipes/example.json').write_text('{}')
        with self.assertRaisesRegex(AssertionError, 'recipe mismatch'):
            driver.virtual_fixture(self.runtime, self.work)


if __name__ == '__main__':
    unittest.main(verbosity=2)
