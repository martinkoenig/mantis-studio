"""Protocol clients, immutable preview references, restart and replay without hardware."""
from pathlib import Path
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / "python"))
import mantis
with tempfile.TemporaryDirectory(prefix="mantis-acquisition-") as directory:
    root = Path(directory)
    profile = {"format_version": 1, "measurement_cameras": {
        "left": {"sensor_identity": "ov9281 18-0060"}, "right": {"sensor_identity": "ov9281 20-0060"}},
        "mode": {"width": 64, "height": 48, "fourcc": "GREY", "fps": 120},
        "calibration_id": "fixture.calibration", "calibration_revision": 7}
    (root / "profile.json").write_text(json.dumps(profile))
    with socket.socket() as socket_probe:
        socket_probe.bind(("127.0.0.1", 0)); port = socket_probe.getsockname()[1]
    env = dict(os.environ, MANTIS_TOKEN="acquisition-" + os.urandom(16).hex(), MANTIS_PORT=str(port),
               MANTIS_X1_PROFILE=str(root / "profile.json"), MANTIS_X1_FAKE="normal",
               QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
    client = mantis.connect(port=port, token=env["MANTIS_TOKEN"])
    daemon = None
    log = open(root / "daemon.log", "w+")
    def until(fn, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = fn()
            if value: return value
            time.sleep(0.01)
        raise AssertionError("Condition timed out")
    def start():
        process = subprocess.Popen([str(build / "bin/mantisd"), "--project", str(root / "Project.mantis")], env=env, stdout=log, stderr=log)
        def ready():
            if process.poll() is not None: log.seek(0); raise AssertionError(log.read())
            try: return client.snapshot()
            except (OSError, mantis.MantisError): return None
        until(ready)
        return process
    def cli(*args): return json.loads(subprocess.check_output([str(build / "bin/mantis-cli"), *args], env=env, text=True))
    try:
        daemon = start()
        devices = client.devices.list()
        parent = next(d for d in devices if d.plugin_id == "org.mantis.x1" and not d.parent)
        assert len(parent.children) == 2
        children = [d for d in devices if d.parent == parent.id]
        assert {d.metadata["role"] for d in children} == {"left", "right"}
        assert client.devices.info(parent.id).id == parent.id
        assert cli("devices", "info", parent.id)["devices"][0]["id"] == parent.id
        # CLI creates the capture through the same protocol used by both SDKs.
        started = cli("capture", "start", parent.id)["captures"][0]
        capture = mantis.Capture(client, started["id"], started["raw_artifact"])
        until(lambda: capture.status().framesets_committed >= 24)
        status = capture.status()
        assert status.framesets_produced >= status.framesets_committed
        assert status.queue_capacity == 32 and status.queue_high_water <= 32
        assert status.preview_drops > 0 and status.dropped == 0 and not status.error
        assert status.total_bytes > 0 and status.writer_mb_s > 0
        assert status.diagnostics["copy_count"] == "1"
        assert status.diagnostics["sequence_agreement"] == "aligned"
        assert cli("capture", "status", capture.id)["captures"][0]["id"] == capture.id
        ref = until(lambda: client.preview(capture))
        with ref:
            assert ref.format_version == 2
            old_bytes = Path(ref.locator).read_bytes()
            assert old_bytes.startswith(b"MANTIS02")
            until(lambda: capture.status().framesets_committed > status.framesets_committed + 12)
            assert Path(ref.locator).read_bytes() == old_bytes  # immutable while acquisition continues
        assert not Path(ref.locator).exists()
        # Studio is a protocol client; its dual view must have actually received frames.
        studio = build / "bin/mantis-studio"
        if studio.exists():
            ui_log = open(root / "studio.log", "w+")
            ui = subprocess.Popen([str(studio), "--quit-after", "1800"], env=env, stdout=ui_log, stderr=ui_log)
            assert ui.wait(timeout=10) == 0
            ui_log.flush(); text = (root / "studio.log").read_text()
            assert "STUDIO_DUAL_PREVIEW_READY" in text, text
            assert "ReferenceError" not in text and "TypeError" not in text, text
            ui_log.close()
        stopped = cli("capture", "stop", capture.id)["captures"][0]
        assert not stopped["active"] and not stopped["error"]
        final_status = capture.status()
        assert final_status.framesets_produced == final_status.framesets_committed
        artifacts = client.capture.list().artifacts
        recorded = next(a for a in artifacts if a.id == capture.raw_artifact)
        assert recorded.schema_version == 2 and recorded.state == "FINALIZED"
        try:
            client._call(artifact_data=mantis.wire.Id(id=capture.raw_artifact))
            raise AssertionError("Segment advertised as standalone packet")
        except mantis.MantisError as error: assert error.code == 8
        report = client.replay.verify(capture.raw_artifact)
        assert report["framesets"] >= 24 and report["left_frames"] == report["right_frames"] == report["framesets"]
        assert report["raw_integrity"] == report["replay"] == "PASS" and report["passes"] == 2
        assert cli("replay", "verify", capture.raw_artifact)["digest"] == report["digest"]
        realtime = client.replay.start(capture.raw_artifact, real_time=True)
        realtime.wait(timeout=15)
        # C++ Client SDK drives the same service, including preview maps/release.
        subprocess.check_call([str(build / "bin/mantis-acquisition-client-tests")], env=env)
        active = client.capture.start(parent.id)
        until(lambda: active.status().framesets_committed >= 24)
        daemon.kill(); daemon.wait(timeout=5)
        env.pop("MANTIS_X1_PROFILE"); env.pop("MANTIS_X1_FAKE")
        daemon = start()
        assert not any(d.plugin_id == "org.mantis.x1" for d in client.devices.list())
        assert client.replay.verify(capture.raw_artifact)["digest"] == report["digest"]
        provisional = next(a for a in client.artifacts.list() if a.id == active.raw_artifact)
        assert provisional.state == "RECOVERABLE"
        recovered = client.artifacts.recover(active.raw_artifact)
        assert recovered.state == "FINALIZED"
        assert client.replay.verify(active.raw_artifact)["framesets"] >= 24
        client.shutdown(); assert daemon.wait(timeout=5) == 0
        print("Fake X1 CLI/Python/C++ controls, dual preview, immutable references, kill/recovery and hardware-free replay passed")
    finally:
        if daemon and daemon.poll() is None: daemon.kill(); daemon.wait(timeout=5)
        log.close()
