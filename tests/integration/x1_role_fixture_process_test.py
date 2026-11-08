"""Real owned-child readiness/restart failures; cleanup must reap, including forced kill."""
import os
import selectors
import signal
import subprocess
import sys
import unittest
from unittest.mock import patch

from x1_role_fixture_process import FixtureDaemon


class ReadinessFailure(RuntimeError):
    pass


class FixtureOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.children = []
        # Child explicitly ignores TERM before publishing its readiness marker.
        self.command = [sys.executable, '-u', '-c',
                        'import signal,sys; signal.signal(signal.SIGTERM,signal.SIG_IGN); '
                        'print("READY",flush=True); sys.stdin.read()']

    def ready(self, process):
        self.children.append(process)  # Independent rescue owner for a failing/mutated helper.
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            self.assertTrue(selector.select(timeout=5), 'Owned child readiness marker missing')
            self.assertEqual(process.stdout.readline(), b'READY\n')

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            for stream in (child.stdin, child.stdout):
                if stream:
                    stream.close()

    def start(self, owner, callback):
        return owner.start(self.command, callback, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def assert_reaped(self, child):
        self.assertEqual(child.returncode, -signal.SIGKILL)
        with self.assertRaises(ChildProcessError):
            os.waitpid(child.pid, os.WNOHANG)

    def test_readiness_failure_keeps_primary_and_reaps_exact_child(self):
        owner = FixtureDaemon(terminate_timeout=.1, kill_timeout=2)
        primary = ReadinessFailure('Injected readiness failure')

        def fail(process):
            self.ready(process)
            raise primary

        with self.assertRaises(ReadinessFailure) as caught:
            self.start(owner, fail)
        self.assertIs(caught.exception, primary)
        self.assertIsNone(owner.process)
        self.assert_reaped(self.children[0])

    def test_restart_retires_previous_and_failed_new_child(self):
        owner = FixtureDaemon(terminate_timeout=.1, kill_timeout=2)
        old = self.start(owner, self.ready)
        self.assertIsNone(old.poll())
        primary = ReadinessFailure('Injected restart readiness failure')

        def fail(process):
            self.ready(process)
            raise primary

        with self.assertRaises(ReadinessFailure) as caught:
            self.start(owner, fail)
        self.assertIs(caught.exception, primary)
        self.assertIsNone(owner.process)
        self.assertEqual(len(self.children), 2)
        for child in self.children:
            self.assert_reaped(child)

    def test_cleanup_signal_error_cannot_replace_readiness_failure(self):
        owner = FixtureDaemon(terminate_timeout=.1, kill_timeout=2)
        primary = ReadinessFailure('Original readiness error')

        def fail(process):
            self.ready(process)
            raise primary

        with patch.object(subprocess.Popen, 'terminate', side_effect=OSError('Injected TERM error')):
            with self.assertRaises(ReadinessFailure) as caught:
                self.start(owner, fail)
        self.assertIs(caught.exception, primary)
        self.assertTrue(any('OSError' in n for n in primary.__notes__))
        self.assertIsNone(owner.process)
        self.assert_reaped(self.children[0])


if __name__ == '__main__':
    unittest.main(verbosity=2)
