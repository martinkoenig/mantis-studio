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
