"""Controlled lifecycle states and the actual protocol; no timing-based ordering."""
from pathlib import Path
from types import SimpleNamespace as State
import sys
import unittest
import subprocess
import tempfile
import shutil
import json
import io
import hashlib
from unittest.mock import patch
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from capture_lifecycle import OwnedProcesses, await_progress, capture_evidence, emit_failure, initial_capture, require_capture, retain_failure, report_failure, log_summary


def capture(**changes):
    return State(**(dict(id="original", raw_artifact="raw-original", active=True,
                        frames=7, error="", framesets_produced=9, framesets_committed=7,
                        queue_depth=2, queue_capacity=32, queue_high_water=3,
                        queue_saturation=0, finalization_job_id="", diagnostics={}) | changes))


def snapshot(*captures):
    return State(captures=captures)


class LifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Compile the checked-in protocol, rather than duplicating a fake schema.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wire.pb"
            protocol = Path(__file__).resolve().parents[2] / "protocol/protobuf"
            subprocess.run(["protoc", "-I", str(protocol), "--descriptor_set_out=" + str(path),
                            "mantis.proto"], check=True, capture_output=True)
            cls.schema = descriptor_pb2.FileDescriptorSet.FromString(path.read_bytes()).file[0]
        cls.WireCapture = cls.capture_type(cls.schema)

    @staticmethod
    def capture_type(schema):
        pool = descriptor_pool.DescriptorPool()
        pool.AddSerializedFile(schema.SerializeToString())
        return message_factory.MessageFactory(pool).GetPrototype(
            pool.FindMessageTypeByName("mantis.wire.v1.Capture"))

    def test_real_schema_counters_and_diagnostics_survive_serialization(self):
        value = self.WireCapture(id="capture", raw_artifact="raw", active=True, frames=7,
                                 framesets_produced=11, framesets_committed=7,
                                 queue_depth=4, queue_capacity=32, queue_high_water=9,
                                 diagnostics={"recorder_first_append_ns": "1000",
                                              "recorder_max_append_ns": "6000",
                                              "recorder_queue_full_ns": "500",
                                              "recorder_failed_enqueue_wait_ns": "50000000",
                                              "recorder_source_paced": "0",
                                              "recorder_producer_started_ns": "125",
                                              "recorder_writer_started_ns": "119"})
        actual = self.WireCapture.FromString(value.SerializeToString())
        row, = capture_evidence(snapshot(actual))
        self.assertEqual(row["framesets_produced"], 11)
        self.assertEqual(row["framesets_committed"], 7)
        self.assertEqual(row["queue_depth"], 4)
        self.assertEqual(row["diagnostics"], dict(value.diagnostics))
        defaults, = capture_evidence(snapshot(self.WireCapture()))
        self.assertEqual(defaults["framesets_produced"], 0)  # Proto3 zero is present in schema.

    def test_missing_and_renamed_real_wire_fields_are_detected(self):
        for name in ("framesets_produced", "framesets_committed", "diagnostics"):
            for rename in (False, True):
                with self.subTest(field=name, rename=rename):
                    schema = descriptor_pb2.FileDescriptorProto()
                    schema.CopyFrom(self.schema)
                    message = next(m for m in schema.message_type if m.name == "Capture")
                    index = next(i for i, f in enumerate(message.field) if f.name == name)
                    if rename:
                        message.field[index].name = "renamed_" + name
                        if name == "diagnostics":
                            # protoc derives the entry type name from the map field.
                            message.field[index].type_name = ".mantis.wire.v1.Capture.RenamedDiagnosticsEntry"
                            next(m for m in message.nested_type if m.name == "DiagnosticsEntry").name = "RenamedDiagnosticsEntry"
                    else:
                        del message.field[index]
                    value = self.capture_type(schema)()
                    with self.assertRaisesRegex(AssertionError, "missing required field: " + name):
                        capture_evidence(snapshot(value))

    def test_legacy_fake_fields_cannot_masquerade_as_wire_counters(self):
        value = capture(produced=90, committed=70)
        del value.framesets_produced
        with self.assertRaisesRegex(AssertionError, "framesets_produced"):
            capture_evidence(snapshot(value))
        with self.assertRaisesRegex(AssertionError, "invalid required field: framesets_committed"):
            capture_evidence(snapshot(capture(framesets_committed=None)))

    def test_project_summary_is_bounded_and_excludes_untrusted_text_and_metadata(self):
        secret = "PRIVATE_TEST_SENTINEL"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diagnostics.log"
            metadata = {"recorder_max_append_ns": "6000", "raw_append_available": "0",
                        "recorder_queue_full_ns": secret, "recorder_token": secret,
                        "recorder_error": secret, "private_prompt": secret}
            lines = [f"{i}\terror\tcapture.diagnostics\t" + json.dumps(metadata) + "\n"
                     for i in range(1, 100)]
            path.write_text(secret * 10000 + "\n" + "".join(lines) +
                            "100\texport.completed\texport\t/" + secret + "\n" +
                            "101\terror\tcapture\t" + secret + "\n")
            result = log_summary(path, project=True)
        self.assertGreater(result["omitted_prefix_bytes"], 0)
        self.assertLessEqual(len(result["events"]), 32)
        self.assertNotIn(secret, json.dumps(result))
        diagnostic = result["events"][-2]["diagnostics"]
        self.assertEqual(diagnostic, {"recorder_max_append_ns": "6000", "raw_append_available": "0",
                                      "recorder_queue_full_ns": "[INVALID]"})
        self.assertEqual(result["events"][-1]["error"], "capture error (text withheld)")

    def test_evidence_failure_never_replaces_primary_or_leaks_secondary_text(self):
        primary = AssertionError("primary capture failure")
        for target in ("capture_lifecycle.retain_failure", "capture_lifecycle.emit_failure"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                output = io.StringIO()
                try:
                    raise primary
                except AssertionError:
                    with patch(target, side_effect=ValueError("PRIVATE_TEST_SENTINEL")):
                        report_failure(Path(directory), Path(directory)/"out", [], [], [], output)
                    with self.assertRaises(AssertionError) as raised:
                        raise
                self.assertIs(raised.exception, primary)
                self.assertIn("ValueError", output.getvalue())
                self.assertNotIn("PRIVATE_TEST_SENTINEL", output.getvalue())
        with patch("capture_lifecycle.retain_failure", side_effect=OSError("private")):
            report_failure(None, None, [], [], [], State(write=lambda text: (_ for _ in ()).throw(OSError())))

    def test_real_wire_untrusted_values_cannot_leak_through_retention_or_export(self):
        secret = "PRIVATE_TEST_SENTINEL"
        value = self.WireCapture(id=secret, raw_artifact=secret, error=secret,
                                 finalization_job_id=secret,
                                 diagnostics={"recorder_max_append_ns": secret,
                                              "recorder_source_paced": "1",
                                              "recorder_private": secret})
        with tempfile.TemporaryDirectory() as directory:
            root, evidence = Path(directory)/"root", Path(directory)/"evidence"
            root.mkdir()
            (root/"daemon.log").write_text("password=" + secret + "\n")
            (root/"studio.log").write_text("Authorization: Bearer " + secret + "\n")
            retain_failure(root, evidence, [capture_evidence(snapshot(value))], [0], [])
            shutil.rmtree(root)
            output = io.StringIO()
            emit_failure(evidence, output)  # Unknown credential, not the fixture token.
            self.assertNotIn(secret, output.getvalue())
            self.assertNotIn(secret, "".join(p.read_text() for p in evidence.iterdir()))
            record = json.loads((evidence/"capture-state.json").read_text())["observations"][0][0]
            self.assertEqual(record["id"], record["raw_artifact"])
            self.assertEqual(record["diagnostics"], {"recorder_max_append_ns": "[INVALID]",
                                                     "recorder_source_paced": "1"})

    def test_evidence_bounds_and_actual_write_failure_are_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "observation bound"):
                retain_failure(root, root/"out", [[]]*33, [], [])
            with self.assertRaisesRegex(AssertionError, "capture bound"):
                capture_evidence(snapshot(*[capture()]*17))
            blocked = root/"blocked"
            blocked.write_text("not a directory")
            output = io.StringIO()
            report_failure(root, blocked, [], [], [], output)
            self.assertIn("FileExistsError", output.getvalue())

    def test_unreadable_log_does_not_erase_available_capture_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch("capture_lifecycle.log_summary", side_effect=PermissionError("private path")):
                retain_failure(root, root/"out", [capture_evidence(snapshot(capture()))], [], [])
            state = json.loads((root/"out/capture-state.json").read_text())
            self.assertEqual(state["observations"][0][0]["framesets_committed"], 7)
            self.assertEqual(json.loads((root/"out/project-diagnostics.json").read_text()),
                             {"read_error": "PermissionError"})


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
        self.assertEqual(len(lines), 4)
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
            (root / "daemon.log").write_text("mantisd ready on 127.0.0.1:12345\nPRIVATE_TEST_SENTINEL\n")
            (root / "studio.log").write_text("STUDIO_ACCEPTANCE_READY private-artifact\n")
            (root / "Project.mantis").mkdir()
            (root / "Project.mantis/diagnostics.log").write_text(
                '1\terror\tcapture.diagnostics\t{"recorder_max_append_ns":"6000","token":"PRIVATE_TEST_SENTINEL"}\n')
            records = [capture_evidence(snapshot(capture(error="Injected disk failure")))]
            retain_failure(root, evidence, records, [None], [-15, None])
            shutil.rmtree(root)
            self.assertEqual(json.loads((evidence / "daemon.log").read_text())["events"], [{"event": "mantisd ready"}])
            self.assertEqual(json.loads((evidence / "studio.log").read_text())["events"], [{"event": "STUDIO_ACCEPTANCE_READY"}])
            state = json.loads((evidence / "capture-state.json").read_text())
            self.assertEqual(state["observations"][0][0]["id"], "sha256:" + hashlib.sha256(b"original").hexdigest())
            self.assertEqual(state["observations"][0][0]["error"], "capture error (text withheld)")
            self.assertEqual(state["observations"][0][0]["framesets_produced"], 9)
            project = json.loads((evidence / "project-diagnostics.json").read_text())
            self.assertEqual(project["events"][0]["diagnostics"], {"recorder_max_append_ns": "6000"})
            self.assertNotIn("PRIVATE_TEST_SENTINEL", "".join(p.read_text() for p in evidence.iterdir()))
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
