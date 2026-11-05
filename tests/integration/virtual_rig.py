"""Real daemon, public SDK, opt-in graph, schema-2 capture and verified replay."""
from pathlib import Path
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

from capture_lifecycle import OwnedProcesses

build = Path(sys.argv[1]).resolve()
daemon_executable = Path(sys.argv[2]).resolve()
source = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(build / "python"))
import mantis


def wait_until(predicate, phase, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.01)  # service readiness/progress only, never a timing oracle
    raise AssertionError(f"Timed out: {phase}")


with tempfile.TemporaryDirectory(prefix="mantis-virtual-rig-") as directory:
    root = Path(directory)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = os.environ.copy()
    for key in ("MANTIS_X1_PROFILE", "MANTIS_X1_FAKE", "MANTIS_VIRTUAL_RIG_PROFILE"):
        env.pop(key, None)
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN="virtual-rig-" + os.urandom(16).hex())
    client = mantis.connect(port=port, token=env["MANTIS_TOKEN"])
    processes = OwnedProcesses()
    with open(root / "daemon.log", "w+") as log:
        def start():
            def ready(process):
                def observe():
                    if process.poll() is not None:
                        raise AssertionError("Virtual rig daemon exited during startup")
                    try:
                        return client.snapshot()
                    except (OSError, mantis.MantisError):
                        return None
                wait_until(observe, "daemon readiness")
            return processes.start_ready(
                [str(daemon_executable), "--project", str(root / "Project.mantis")],
                ready=ready, env=env, stdout=log, stderr=log)
        try:
            daemon = start()
            assert [d.id for d in client.devices.list()] == ["virtual-scanner"]
            client.shutdown()
            assert daemon.wait(timeout=5) == 0
            profile = json.loads((source / "profiles/virtual-rig-community.json").read_text())
            profile["mode"] = "trigger"
            for camera in profile["cameras"]:
                camera.update(rate_num=30, rate_den=1)
            path = root / "rig.json"
            path.write_text(json.dumps(profile))
            env["MANTIS_VIRTUAL_RIG_PROFILE"] = str(path)
            daemon = start()
            devices = client.devices.list()
            parent = next(d for d in devices if d.plugin_id == "org.mantis.virtual-rig" and not d.parent)
            assert len(parent.children) == 2
            children = [d for d in devices if d.parent == parent.id]
            assert {d.metadata["role"] for d in children} == {"left", "right"}
            assert all("org.mantis.trigger.hardware.v1" not in d.capabilities for d in devices)
            assert next(d for d in devices if d.id == "virtual-scanner")
            # Existing idle discovery handles disappearance/reappearance; no new
            # simulator branch is added to runtime discovery or Session.
            hidden = root / "rig.unavailable"
            path.rename(hidden)
            assert not any(d.plugin_id == "org.mantis.virtual-rig" for d in client.devices.list())
            hidden.rename(path)
            assert client.devices.info(parent.id).id == parent.id
            capture = client.capture.start(parent)
            wait_until(lambda: capture.status().framesets_committed >= 6, "six committed FrameSets")
            capture.stop()
            status = capture.status()
            assert not status.error and status.framesets_produced == status.framesets_committed
            assert status.dropped == 0 and status.queue_saturation == 0
            assert status.diagnostics["provenance"] == "simulation-only"
            assert status.diagnostics["physical_exposure_skew"] == "unavailable"
            assert status.diagnostics["sync_quality"] == "software"
            raw = next(a for a in client.artifacts.list() if a.id == capture.raw_artifact)
            assert raw.schema_version == 2 and raw.state == "FINALIZED"
            report = client.replay.verify(capture.raw_artifact, timeout=10)
            assert report["framesets"] >= 6 and report["passes"] == 2
            assert report["raw_integrity"] == report["replay"] == "PASS"
            # Validate the shipped opt-in high-resolution/120-FPS configuration
            # through actual plugin discovery without a sustained capture.
            path.write_text((source / "profiles/virtual-rig-trigger.json").read_text())
            configured = next(d for d in client.devices.list()
                              if d.plugin_id == "org.mantis.virtual-rig" and not d.parent)
            assert configured.id == "org.mantis.virtual-rig:trigger-stereo"
            assert json.loads(configured.metadata["profile"])["cameras"][0]["rate_num"] == 120
            path.write_text(json.dumps(profile))
            assert client.devices.info(parent.id).id == parent.id
            restarted = client.capture.start(parent)
            assert restarted.status().diagnostics["generation"] != status.diagnostics["generation"]
            restarted.stop()
            client.shutdown()
            assert daemon.wait(timeout=5) == 0
        finally:
            processes.close()
print("Opt-in/default daemon discovery, Session capture, restart and two-pass replay passed")
