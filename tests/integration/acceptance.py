"""Real processes, real Protobuf, loaded DSOs, SQLite recovery and Qt client survival."""
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
from mantis import mantis_pb2 as wire

with tempfile.TemporaryDirectory(prefix="mantis-acceptance-") as directory:
    root = Path(directory)
    env = os.environ.copy()
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN="acceptance-" + os.urandom(16).hex(), QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
    client = mantis.connect(port=port, token=env["MANTIS_TOKEN"])
    log = open(root / "daemon.log", "w+")
    daemon = None
    studios = []
    def start():
        process = subprocess.Popen([str(build / "bin/mantisd"), "--project", str(root / "Project.mantis")], env=env, stdout=log, stderr=log)
        for _ in range(100):
            if process.poll() is not None:
                log.seek(0)
                raise AssertionError(log.read())
            try:
                client.snapshot()
                return process
            except (OSError, mantis.MantisError): time.sleep(0.05)
        raise AssertionError("Daemon startup timeout")
    def cli(*args):
        return json.loads(subprocess.check_output([str(build / "bin/mantis-cli"), *args], env=env, text=True))
    def until(predicate, timeout=10):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate(): return
            time.sleep(0.05)
        raise AssertionError("Condition timed out")
    try:
        daemon = start()
        devices = client.devices.list()
        assert len(devices) == 1 and devices[0].id == "virtual-scanner"
        assert "org.mantis.camera.image-stream.v1" in devices[0].capabilities
        assert cli("devices", "list")["devices"][0]["id"] == devices[0].id
        # Wire schema compatibility and authorization failures are structured, not daemon crashes.
        bad = mantis.connect(port=port, token="incorrect")
        try:
            bad.snapshot()
            raise AssertionError("Wrong token accepted")
        except mantis.MantisError: pass
        # Unknown fields survive the wire parser; incompatible versions fail explicitly.
        def raw_request(request, extra=b""):
            payload = request.SerializeToString() + extra
            with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
                import struct
                sock.sendall(struct.pack("!I", len(payload)) + payload)
                size, = struct.unpack("!I", client._receive(sock, 4))
                return wire.Response.FromString(client._receive(sock, size))
        request = wire.Request(protocol_version=1, request_id="unknown-fields", token=env["MANTIS_TOKEN"], snapshot=wire.Empty())
        assert not raw_request(request, b"\xf8\x07\x01").HasField("error")
        request.protocol_version = 99
        assert raw_request(request).HasField("error")
        capture = client.capture.start(devices[0])
        artifact = client.pipeline.run(capture=capture, recipe="example").wait()
        client.export(artifact, root / "python.ply")
        capture.stop()
        assert (root / "python.ply").read_text().startswith("ply\n")
        assert "element vertex 3072" in (root / "python.ply").read_text()
        replay = client.pipeline.run(raw_artifact=capture.raw_artifact).wait()
        client.export(replay, root / "replay.ply")
        assert (root / "replay.ply").read_bytes() == (root / "python.ply").read_bytes()
        cli("workflow", str(root / "cli.ply"))
        assert (root / "cli.ply").read_bytes() == (root / "python.ply").read_bytes()
        studio_bin = build / "bin/mantis-studio"
        active = None
        if studio_bin.exists():
            ui_log = open(root / "studio.log", "w+")
            studio = subprocess.Popen([str(studio_bin), "--acceptance-export", str(root / "studio.ply")], env=env, stdout=ui_log, stderr=ui_log)
            studios.append(studio)
            until(lambda: (root / "studio.ply").exists())
            until(lambda: "STUDIO_ACCEPTANCE_READY" in (root / "studio.log").read_text())
            assert studio.poll() is None
            assert (root / "studio.ply").read_bytes() == (root / "python.ply").read_bytes()
            active = next(c for c in client.snapshot().captures if c.active)
        else:
            capture = client.capture.start(devices[0])
            active = next(c for c in client.snapshot().captures if c.id == capture.id)
        crash = client.pipeline.run(capture=active.id, recipe="crash-test")
        try:
            crash.wait()
            raise AssertionError("Crash test unexpectedly succeeded")
        except mantis.MantisError as e:
            assert "status" in str(e)
        assert daemon.poll() is None
        failed = next(p for p in client.snapshot().plugins if p.id == "org.mantis.crash-test")
        assert failed.state == "failed" and failed.diagnostic
        assert any("crash-test" in event.message for event in client.events())
        until(lambda: next(c for c in client.snapshot().captures if c.id == active.id).frames > active.frames)
        assert all(p.poll() is None for p in studios)
        # Disconnect and restart the actual Studio while capture remains daemon-owned.
        for studio in studios: studio.terminate(); studio.wait(timeout=5)
        if studio_bin.exists():
            studio = subprocess.Popen([str(studio_bin), "--quit-after", "1400"], env=env, stdout=ui_log, stderr=ui_log)
            assert studio.wait(timeout=8) == 0
            ui_log.flush(); ui_log.seek(0)
            ui_text = ui_log.read()
            assert "failed to load" not in ui_text.lower(), ui_text
            assert "ReferenceError" not in ui_text and "TypeError" not in ui_text, ui_text
            ui_log.close()
        assert next(c for c in client.snapshot().captures if c.id == active.id).active
        client.pipeline.run(capture=active.id).wait()
        client.enable_plugin("org.mantis.crash-test", False)
        assert next(p for p in client.snapshot().plugins if p.id == "org.mantis.crash-test").state == "disabled"
        # Hard daemon termination leaves a genuinely provisional capture, then startup recovers it.
        raw = active.raw_artifact
        prior_ids = {a.id for a in client.artifacts.list() if a.state == "FINALIZED"}
        daemon.kill(); daemon.wait(timeout=5)
        daemon = start()
        assert prior_ids <= {a.id for a in client.artifacts.list()}
        recoverable = next(a for a in client.artifacts.list() if a.id == raw)
        assert recoverable.state == "RECOVERABLE" and recoverable.chunks > 0
        recovered = client.artifacts.recover(raw)
        assert recovered.state == "FINALIZED"
        client.pipeline.run(raw_artifact=raw).wait()
        client.shutdown(); assert daemon.wait(timeout=8) == 0
        print("PASS: Python, CLI, " + ("Studio/viewport/UI restart, " if studio_bin.exists() else "headless client reconnect, ") + "deterministic replay, isolated crash, capture continuity, daemon crash recovery")
    finally:
        for process in studios:
            if process.poll() is None: process.kill(); process.wait()
        if daemon and daemon.poll() is None: daemon.kill(); daemon.wait()
        log.close()
