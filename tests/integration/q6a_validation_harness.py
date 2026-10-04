"""Hardware-free regression tests for the official validation harness and cleanup."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("q6a", ROOT / "scripts/validate_x1_q6a.py")
q6a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q6a)


def graph(enabled=True):
    text = "Media controller API version 6.8\nbus info        platform:acb3000.isp\n"
    for index, (src, pad, dst, sink) in enumerate(q6a.LINKS):
        text += (f'- entity {index + 1}: {src} (2 pads, 1 link, 0 routes)\n'
                 f'    pad{pad}: Source\n'
                 f'        -> "{dst}":{sink} [{"ENABLED" if enabled else ""}]\n')
    return text + '- entity 100: rgb (1 pad, 1 link)\n    pad0: Source\n        -> "rgb-video":0 [ENABLED,IMMUTABLE]\n'


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mantis-harness-tests-")
        self.addCleanup(self.temp.cleanup)
        self.args = q6a.arguments(["--smoke", "--output-root", self.temp.name, "--storage", self.temp.name])
        self.validation = q6a.Validation(self.args)

    def test_reference_profile_rejected_without_rewriting(self):
        reference = json.loads((ROOT / "profiles/x1-q6a.json").read_text())
        q6a.validate_profile(reference)
        for key, value in (("format_version", 1), ("hardware_sync_configured", True),
                           ("max_v4l2_delta_ns", 4000000), ("max_v4l2_delta_ns", 100000000)):
            changed = dict(reference, **{key: value})
            with self.assertRaisesRegex(RuntimeError, key):
                q6a.validate_profile(changed)
        with self.assertRaisesRegex(RuntimeError, "vertical_blanking"):
            q6a.validate_profile(dict(reference, mode=dict(reference["mode"], vertical_blanking=20)))

    def test_fake_and_capacity_fail_before_mutation(self):
        for env, reason, free in (({"MANTIS_X1_FAKE": "normal"}, "rejects MANTIS_X1_FAKE", 10**12),
                                  ({}, "Insufficient storage", 1)):
            with patch.dict(os.environ, env, clear=True), patch.object(q6a.shutil, "disk_usage", return_value=SimpleNamespace(free=free)):
                with self.assertRaisesRegex(RuntimeError, reason), contextlib.redirect_stdout(io.StringIO()):
                    self.validation.preflight()
            self.assertIsNone(self.validation.daemon)
            self.assertFalse(self.validation.service_restore)

    def test_runtime_snapshot_preserves_source_and_forbids_conflict_changes(self):
        source = Path(self.temp.name) / "profile.json"
        original = (ROOT / "profiles/x1-q6a.json").read_bytes()
        source.write_bytes(original)
        self.args.profile = source
        with patch.dict(os.environ, {}, clear=True), patch.object(q6a.shutil, "which", return_value="available"), contextlib.redirect_stdout(io.StringIO()):
            self.validation.preflight()
        self.assertEqual(source.read_bytes(), original)
        effective = json.loads(Path(self.validation.env["MANTIS_X1_PROFILE"]).read_text())
        self.assertFalse(effective["runtime_setup"]["disable_conflicting_links"])
        q6a.validate_profile(effective)
        self.assertEqual(len(self.validation.env["MANTIS_TOKEN"]), 64)

    def test_dynamic_graph_and_format_readback(self):
        links = q6a.graph_links(graph())
        self.assertEqual(set(links), set(q6a.LINKS) | {("rgb", 0, "rgb-video", 0)})
        self.assertTrue(all("ENABLED" in links[link] for link in q6a.LINKS))
        self.assertTrue(all(q6a.graph_links(graph(False))[link] == "" for link in q6a.LINKS))
        text = "Width/Height : 1280/720\nPixel Format : 'Y10P'\nBytes per Line : 1600\nSize Image : 1152000\n"
        q6a.validate_capture_format(text, "left")
        with self.assertRaisesRegex(RuntimeError, "bytesperline=1600"):
            q6a.validate_capture_format(text.replace("1600", "1280"), "left")

    def test_initial_pairing_failure_snapshot_is_preserved(self):
        metrics = {"max_v4l2_delta_ns": "5000000", "pairing_nearest_candidate_distance_ns": "6000000",
                   "left.observed_max_period_ns": "12000000"}
        events = [SimpleNamespace(sequence=5, component="capture.diagnostics", message=json.dumps(metrics))]
        self.validation.client = SimpleNamespace(events=lambda **kwargs: events if kwargs.get("after") == 4 else [SimpleNamespace(sequence=4)])
        with patch.object(self.validation, "assert_service_stopped"), \
             patch.object(self.validation, "command", side_effect=RuntimeError("Nearest camera timestamps exceed profile pairing limit")), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "Nearest camera timestamps"):
                self.validation.capture()
        result = json.loads((self.validation.output / "pairing.json").read_text())
        self.assertEqual(result["short_pairing_check"], "FAIL")
        self.assertEqual(result["failure_diagnostics"], metrics)

    def test_service_absent_inactive_and_active_restore(self):
        for load, state in (("not-found", "inactive"), ("loaded", "inactive"), ("loaded", "active")):
            v = q6a.Validation(self.args)
            active = [state]
            actions = []
            def command(argv, **kwargs):
                if "--value" in argv:
                    return active[0] + "\n", 0
                return f"LoadState={load}\nActiveState={state}\n", 0
            def action(name):
                actions.append(name)
                active[0] = "inactive" if name == "stop" else "active"
            with patch.object(v, "command", side_effect=command), patch.object(v, "service_action", side_effect=action), patch.object(q6a.shutil, "which", return_value="systemctl"), contextlib.redirect_stdout(io.StringIO()):
                v.service()
                v.cleanup()
            self.assertEqual(actions, ["stop", "start"] if state == "active" else [])
            self.assertFalse(v.report["cleanup_errors"])

    def test_partial_link_failure_restores_only_attempted_measurement_links(self):
        v = self.validation
        v.args.disable_measurement_links = True
        calls = []
        def mutate(link, enabled):
            calls.append((link, enabled))
            if len(calls) == 2:
                raise RuntimeError("injected link failure")
        with patch.object(Path, "glob", return_value=[Path("/dev/media73")]), patch.object(v, "command", return_value=(graph(), 0)), patch.object(v, "graph", return_value=graph()), patch.object(v, "set_link", side_effect=mutate), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "injected link failure"):
                v.media_before()
            v.cleanup()
        self.assertEqual(v.media, "/dev/media73")
        self.assertEqual(calls, [(q6a.LINKS[0], False), (q6a.LINKS[1], False),
                                 (q6a.LINKS[1], True), (q6a.LINKS[0], True)])
        self.assertFalse(v.report["cleanup_errors"])

    def test_cleanup_error_turns_success_into_failure_and_preserves_logs(self):
        v = self.validation
        v.report.update(result="PASS", stage="complete", reason="done")
        v.service_restore = True
        with patch.object(v, "service_action", side_effect=RuntimeError("restore denied")), contextlib.redirect_stdout(io.StringIO()):
            v.cleanup()
            v.finish()
        summary = json.loads((v.output / "summary.json").read_text())
        self.assertEqual(summary["result"], "FAIL")
        self.assertEqual(summary["stage"], "cleanup")
        self.assertIn("restore denied", summary["reason"])
        for name in ("summary.txt", "daemon.log", "devices.json", "pairing.json", "media-before.txt", "media-after.txt"):
            self.assertTrue((v.output / name).exists(), name)

    def test_command_failure_and_timeout_are_not_hidden(self):
        with self.assertRaisesRegex(RuntimeError, "exited 7: exact fixture failure"):
            self.validation.command([sys.executable, "-c", "import sys; print('exact fixture failure'); sys.exit(7)"])
        with self.assertRaisesRegex(RuntimeError, "timed out"):
            self.validation.command([sys.executable, "-c", "import time; time.sleep(30)"], timeout=0.1)

    def test_post_capture_resolves_renumbered_nodes_and_reads_all_selected_pads(self):
        v = self.validation
        v.media = "/dev/media73"
        v.original_graph_links = q6a.graph_links(graph())
        v.report["discovery"] = {"left": {"video_node": "/dev/video83"}, "right": {"video_node": "/dev/video97"}}
        calls = []
        def command(argv, **kwargs):
            calls.append(argv)
            if "-e" in argv:
                return {"ov9281 18-0060": "/dev/v4l-subdev61", "msm_vfe2_video0": "/dev/video83",
                        "ov9281 20-0060": "/dev/v4l-subdev79", "msm_vfe3_video0": "/dev/video97"}[argv[-1]], 0
            if "--get-fmt-video" in argv:
                return "Pixel Format : 'Y10P'\nWidth/Height : 1280/720\nBytes per Line : 1600\nSize Image : 1152000\n", 0
            if "--get-ctrl=vertical_blanking" in argv:
                return "vertical_blanking: 196\n", 0
            if "--get-v4l2" in argv:
                return "[fmt:Y10_1X10/1280x720 field:none]\n", 0
            raise AssertionError(argv)
        with patch.object(v, "command", side_effect=command), patch.object(v, "graph", return_value=graph()), contextlib.redirect_stdout(io.StringIO()):
            v.hardware_after()
        self.assertEqual(v.report["hardware"]["left"]["sensor_node"], "/dev/v4l-subdev61")
        self.assertEqual(v.report["hardware"]["right"]["video_node"], "/dev/video97")
        self.assertEqual(sum("--get-v4l2" in argv for argv in calls), 14)
        self.assertTrue(all(argv[argv.index("-d") + 1] == "/dev/media73" for argv in calls if argv[0] == "media-ctl"))

    def test_ctest_failure_is_reported_even_if_sixteen_tests_are_printed(self):
        v = self.validation
        v.args.full = True
        def command(argv, **kwargs):
            if argv[0] == "ctest":
                raise RuntimeError("CTest exited 8 despite printed 16/16")
            return "", 0
        with patch.object(v, "command", side_effect=command), patch.object(v, "preflight"), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(v.run(), 1)
        self.assertEqual(v.report["stage"], "ctest-debug")
        self.assertIn("Debug tests: FAIL", (v.output / "summary.txt").read_text())

    def test_success_report_never_claims_sustained_storage(self):
        v = self.validation
        with contextlib.ExitStack() as stack, contextlib.redirect_stdout(io.StringIO()):
            for method in ("preflight", "builds", "service", "media_before", "start_daemon", "discovery", "capture", "hardware_after"):
                stack.enter_context(patch.object(v, method))
            self.assertEqual(v.run(), 0)
        self.assertEqual(json.loads((v.output / "summary.json").read_text())["sustained_storage_acceptance"], "NOT ESTABLISHED")
        self.assertIn("RESULT: PASS", (v.output / "summary.txt").read_text())

    def test_shell_exit_int_term_stop_only_owned_daemon_and_restore_service(self):
        # The wrapper runs this interpreter shim, which injects hardware fixtures
        # into the actual runner while preserving its signal/finally behavior.
        shim = Path(self.temp.name) / "python-shim"
        shim.write_text(f'''#!{sys.executable}
import importlib.util, json, os, signal, subprocess, sys, time
from pathlib import Path
spec = importlib.util.spec_from_file_location("q6a", sys.argv[1])
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)
def ready(v):
    v.enter("fixture-hardware")
    v.daemon = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"], start_new_session=True)
    v.service_restore = True
    v.report["service"] = {{"exists": True}}
    Path(os.environ["FIXTURE_READY"]).write_text(str(v.daemon.pid))
    if os.environ["FIXTURE_MODE"] == "EXIT":
        raise RuntimeError("injected capture failure")
    time.sleep(60)
def service_action(v, action):
    Path(os.environ["FIXTURE_RESTORED"]).write_text(action)
def command(v, argv, **kw):
    return "active\\n", 0
for name in ("preflight", "builds", "service", "media_before"):
    setattr(q.Validation, name, lambda v: None)
q.Validation.start_daemon = ready
q.Validation.service_action = service_action
q.Validation.command = command
signal.signal(signal.SIGINT, q.interrupted)
signal.signal(signal.SIGTERM, q.interrupted)
sys.exit(q.Validation(q.arguments(sys.argv[2:])).run())
''')
        shim.chmod(0o700)
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        try:
            for mode, sig in (("EXIT", None), ("INT", signal.SIGINT), ("TERM", signal.SIGTERM)):
                ready = Path(self.temp.name) / f"ready-{mode}"
                restored = Path(self.temp.name) / f"restored-{mode}"
                output = Path(self.temp.name) / mode
                env = dict(os.environ, MANTIS_VALIDATION_PYTHON=str(shim), FIXTURE_READY=str(ready),
                           FIXTURE_RESTORED=str(restored), FIXTURE_MODE=mode)
                process = subprocess.Popen([str(ROOT / "scripts/validate-x1-q6a.sh"), "--smoke", "--output-root", str(output)],
                                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    deadline = time.monotonic() + 5
                    while not ready.exists():
                        self.assertIsNone(process.poll())
                        self.assertLess(time.monotonic(), deadline)
                        time.sleep(0.01)
                    if sig:
                        process.send_signal(sig)
                    text = process.communicate(timeout=15)[0].decode()
                    self.assertNotEqual(process.returncode, 0, text)
                    self.assertIn("RESULT: FAIL", text)
                    self.assertEqual(restored.read_text(), "start")
                    with self.assertRaises(ProcessLookupError):
                        os.kill(int(ready.read_text()), 0)
                    self.assertIsNone(unrelated.poll())
                    summary = json.loads(next(output.glob("*/summary.json")).read_text())
                    self.assertEqual(summary["stage"], "fixture-hardware")
                    self.assertIn("injected capture failure" if mode == "EXIT" else "Interrupted by SIGTERM", summary["reason"])
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.communicate(timeout=5)
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
