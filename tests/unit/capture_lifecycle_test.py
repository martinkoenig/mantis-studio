"""Controlled lifecycle states; no processes, real clocks or retry-until-green."""
from pathlib import Path
from types import SimpleNamespace as State
import sys
import unittest
import subprocess
import tempfile
import shutil
import json
import io

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from capture_lifecycle import OwnedProcesses, await_progress, capture_evidence, emit_failure, initial_capture, require_capture, retain_failure


def capture(**changes):
    return State(**(dict(id="original", raw_artifact="raw-original", active=True,
                        frames=7, error="") | changes))


def snapshot(*captures):
    return State(captures=captures)


class LifecycleTests(unittest.TestCase):
    def test_exported_evidence_survives_all_runner_files_being_removed(self):
        output = io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory)
            (evidence / "daemon.log").write_text("disk failure; token=fixture-secret\n")
            (evidence / "studio.log").write_text("Studio ready\n")
            (evidence / "capture-state.json").write_text(json.dumps({
                "observations": [capture_evidence(snapshot(capture(error="disk failure")))],
            }))
            emit_failure(evidence, output, secret="fixture-secret")
        self.assertFalse(evidence.exists())
        records = [json.loads(line.removeprefix("ACCEPTANCE_FAILURE_EVIDENCE "))
                   for line in output.getvalue().splitlines()]
        self.assertNotIn("fixture-secret", output.getvalue())
        self.assertIn("disk failure; token=[REDACTED]", records[0]["tail"])
        self.assertEqual(records[1]["tail"], "Studio ready\n")
        self.assertEqual(json.loads(records[2]["tail"])["observations"][0][0]["id"], "original")

    def test_output_is_bounded_escaped_and_redacts_a_token_crossing_tail_boundary(self):
        output = io.StringIO()
        token = "fixture-secret"
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory)
            suffix = b"\n::error::untrusted\x1b[31m\xff" + b"z" * 20
            payload = b"x" * 20000 + token.encode() + suffix
            (evidence / "daemon.log").write_bytes(payload)
            limit = len(suffix) + 4  # Cut through the token, not at its beginning.
            emit_failure(evidence, output, secret=token, limit=limit)
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(all(line.startswith("ACCEPTANCE_FAILURE_EVIDENCE ") for line in lines))
        self.assertTrue(all(len(line) < 1000 for line in lines))
        record = json.loads(lines[0].removeprefix("ACCEPTANCE_FAILURE_EVIDENCE "))
        self.assertEqual(record["size_bytes"], len(payload))
        self.assertEqual(record["omitted_prefix_bytes"], 20000)
        self.assertEqual(record["tail"], "[REDACTED]" + suffix.decode(errors="backslashreplace"))
        self.assertNotIn(token, output.getvalue())
        self.assertNotIn("\x1b", output.getvalue())

    def test_missing_optional_log_is_explicit_and_invalid_bound_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = io.StringIO()
            emit_failure(Path(directory), output)
            for line in output.getvalue().splitlines():
                self.assertEqual(json.loads(line.removeprefix("ACCEPTANCE_FAILURE_EVIDENCE "))["read_error"],
                                 "FileNotFoundError")
            with self.assertRaises(ValueError):
                emit_failure(Path(directory), output, limit=0)

    def test_readiness_failure_before_return_does_not_orphan_child(self):
        state = dict(alive=True, reaped=False)
        process = State(poll=lambda: None if state["alive"] else -9,
                        kill=lambda: state.update(alive=False),
                        wait=lambda: state.update(reaped=True))
        group = OwnedProcesses(launch=lambda: process)
        def unready(child):
            self.assertIs(child, process)
            self.assertEqual(list(group), [process])
            raise TimeoutError("readiness timeout")
        try:
            with self.assertRaisesRegex(TimeoutError, "readiness timeout"):
                group.start_ready(ready=unready)
        finally:
            group.close()
        self.assertFalse(state["alive"])
        self.assertTrue(state["reaped"])

    def test_failed_run_evidence_survives_temporary_project_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            evidence = Path(directory) / "retained"
            root.mkdir()
            (root / "daemon.log").write_text("Injected disk failure\n")
            (root / "studio.log").write_text("Studio ready\n")
            records = [capture_evidence(snapshot(capture(error="Injected disk failure")))]
            retain_failure(root, evidence, records, [None], [-15, None])
            shutil.rmtree(root)
            self.assertEqual((evidence / "daemon.log").read_text(), "Injected disk failure\n")
            self.assertEqual((evidence / "studio.log").read_text(), "Studio ready\n")
            state = json.loads((evidence / "capture-state.json").read_text())
            self.assertEqual(state["observations"][0][0]["id"], "original")
            self.assertEqual(state["observations"][0][0]["error"], "Injected disk failure")
            self.assertEqual(state["studio_exits"], [-15, None])
            self.assertEqual(state["daemon_exits"], [None])

    def test_restart_timeout_still_owns_and_reaps_the_second_process(self):
        class Process:
            code = None
            killed = 0
            reaped = 0
            def poll(self): return self.code
            def kill(self):
                self.killed += 1
                self.code = -9
            def wait(self, timeout=None):
                if self.code is None:
                    raise subprocess.TimeoutExpired("test-process", timeout)
                self.reaped += 1
                return self.code
        group = OwnedProcesses(launch=Process)
        first = group.start()
        first.code = 0
        second = group.start()
        try:
            with self.assertRaises(subprocess.TimeoutExpired):
                second.wait(timeout=8)
        finally:
            group.close()
        self.assertEqual(list(group), [first, second])
        self.assertEqual(first.killed, 0)
        self.assertEqual(second.killed, 1)
        self.assertEqual(second.reaped, 1)
        self.assertEqual(second.poll(), -9)

    def test_original_advances_after_intermediate_snapshot(self):
        clock = [0.0]
        observations = iter((snapshot(capture()), snapshot(capture(frames=8))))
        waits = []
        def wait(seconds):
            waits.append(seconds)
            clock[0] += seconds
        actual = await_progress(lambda: next(observations), capture(), "after UI termination",
                                now=lambda: clock[0], wait=wait)
        self.assertEqual(actual.id, "original")
        self.assertEqual(actual.raw_artifact, "raw-original")
        self.assertEqual(actual.frames, 8)
        self.assertEqual(waits, [.05])

    def test_missing_original_never_substitutes_another_capture(self):
        with self.assertRaisesRegex(AssertionError, 'original.*raw-original'):
            require_capture(snapshot(capture(id="replacement", frames=100)), capture(), "disconnect")

    def test_duplicate_or_changed_raw_identity_fails(self):
        for state in (snapshot(capture(), capture()), snapshot(capture(raw_artifact="new"))):
            with self.assertRaises(AssertionError):
                require_capture(state, capture(), "restart")

    def test_inactive_or_failed_capture_cannot_pass_with_more_frames(self):
        for state in (capture(active=False, frames=8), capture(error="Injected disk failure", frames=8)):
            # The previous acceptance predicate accepted both observations.
            self.assertTrue(next(c for c in snapshot(state).captures if c.id == "original").frames > 7)
            with self.assertRaisesRegex(AssertionError, 'after crash'):
                await_progress(lambda: snapshot(state), capture(), "after crash")

    def test_error_diagnostic_preserves_phase_identity_and_cause(self):
        with self.assertRaises(AssertionError) as result:
            require_capture(snapshot(capture(error="Injected disk failure")), capture(), "UI exited")
        for value in ("UI exited", "original", "raw-original", "Injected disk failure"):
            self.assertIn(value, str(result.exception))

    def test_counter_reversal_fails(self):
        with self.assertRaisesRegex(AssertionError, 'reversed'):
            await_progress(lambda: snapshot(capture(frames=6)), capture(), "restart")

    def test_progress_deadline_has_last_observation(self):
        clock = [0.0]
        def wait(seconds):
            clock[0] += seconds
        with self.assertRaisesRegex(AssertionError, 'frame progress deadline') as result:
            await_progress(lambda: snapshot(capture()), capture(), "restart", timeout=.1,
                           now=lambda: clock[0], wait=wait)
        self.assertIn('"frames": 7', str(result.exception))

    def test_startup_does_not_guess_between_active_captures(self):
        for state in (snapshot(), snapshot(capture(), capture(id="other"))):
            with self.assertRaises(AssertionError):
                initial_capture(state, "Studio ready")

    def test_evidence_excludes_credentials_and_unrestricted_fields(self):
        state = capture(token="PRIVATE_TEST_SENTINEL", private_prompt="PRIVATE_TEST_SENTINEL")
        self.assertNotIn("PRIVATE_TEST_SENTINEL", str(capture_evidence(snapshot(state))))


if __name__ == "__main__":
    unittest.main()
