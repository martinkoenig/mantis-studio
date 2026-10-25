"""Deterministic wire fault fixture exercising StudioBridge through mantis-client."""
from pathlib import Path
import collections
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / "python"))
from mantis import mantis_pb2 as wire

with tempfile.TemporaryDirectory(prefix="mantis-bridge-errors-") as directory:
    root = Path(directory)
    valid = root / "valid.packet"
    wrong_type = root / "valid.packet.wrong"
    corrupt = root / "corrupt.packet"
    corrupt.write_bytes(b"invalid packet fixture")
    failures = []
    operations = []
    data_requests = collections.Counter()
    release = threading.Event()
    held_threads = []
    stop = threading.Event()
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    listener.settimeout(0.1)
    port = listener.getsockname()[1]

    def exact(connection, count):
        result = bytearray()
        while len(result) < count:
            chunk = connection.recv(count - len(result))
            assert chunk, "Unexpected truncated fixture request"
            result.extend(chunk)
        return bytes(result)

    def retry_description(mode):
        candidates = {
            "auto-missing": ("missing", "not-found"),
            "retry-corrupt": ("corrupt", "corrupt"),
            "retry-repaired": ("corrupt", "valid"),
            "retry-new": ("fresh", "valid"),
            "retry-valid": ("valid", "valid"),
            "retry-transient": ("transient", "io"),
            "retry-transient-fixed": ("transient", "valid"),
            "retry-loss": ("loss", "loss"),
            "retry-restored": ("loss", "valid"),
            "retry-auth-data": ("auth-data", "auth-loss"),
            "retry-auth-restored": ("auth-data", "valid"),
            "retry-revision": ("revision", "corrupt"),
            "retry-revision-fixed": ("revision", "valid"),
            "retry-A": ("same", "corrupt"),
            "retry-B": ("same", "valid"),
            "retry-async-hold": ("held", "valid"),
            "retry-async-B": ("held", "valid"),
            "retry-empty-project": ("valid", "valid"),
            "retry-empty": (None, None),
            "retry-wrong-type": ("wrong-type", "wrong-type"),
            "retry-older": ("current", "corrupt"),
            "retry-older-loaded": ("current", "corrupt"),
            "retry-duplicates": ("duplicate", "valid"),
            "retry-clock": ("clock-failure", "io"),
            "retry-clock-fixed": ("clock-failure", "valid"),
        }
        id, failure = candidates[mode]
        project = {"retry-A": "/retry/A", "retry-B": "/retry/B",
                   "retry-async-hold": "/retry/async-A", "retry-async-B": "/retry/async-B",
                   "retry-empty-project": ""}.get(mode, "fixture-stable-project")
        return project, id, failure

    def serve():
        mode, revision, last_project = "active", 0, ""
        try:
            while not stop.is_set():
                try:
                    connection, _ = listener.accept()
                except TimeoutError:
                    continue
                with connection:
                    connection.settimeout(5)
                    size = struct.unpack(">I", exact(connection, 4))[0]
                    assert 0 < size <= 4 * 1024 * 1024
                    request = wire.Request.FromString(exact(connection, size))
                    assert request.protocol_version == 1 and request.token == "bridge-fixture-token"
                    reply = wire.Response(protocol_version=1, request_id=request.request_id)
                    command = request.WhichOneof("command")
                    if command == "plugin_enable":
                        assert request.plugin_enable.id.startswith("fixture:")
                        value = request.plugin_enable.id.split(":", 1)[1]
                        if value == "release":
                            release.set()
                        else:
                            mode = value
                    elif command == "pipeline_run":
                        recipe = request.pipeline_run.recipe
                        operations.append(recipe)
                        if recipe == "transport":
                            mode = "offline"
                            continue  # Genuine peer closure during a request.
                        if recipe.startswith("reject"):
                            reply.error.code = {"reject-busy": 6, "reject-io": 5,
                                                "reject-invalid": 1, "reject-offline": 6}[recipe]
                            reply.error.component = "fixture.pipeline"
                            reply.error.message = "Rejected fixture operation"
                            if recipe == "reject-offline":
                                mode = "offline"
                    elif command == "snapshot":
                        if mode in ("offline", "retry-offline"):
                            continue
                        if mode in ("auth", "retry-auth-offline"):
                            reply.error.code = 1
                            reply.error.message = "Invalid local access token"
                        else:
                            revision += 1
                            stable = mode == "auto-missing" or mode.startswith("retry-")
                            project, id, failure = retry_description(mode) if stable else (f"fixture-project-{revision}", None, None)
                            reply.project_path = last_project = project
                            device = reply.devices.add(id="real-device", name="Fixture camera")
                            device.capabilities.append("org.mantis.camera.image-stream.v1")
                            reply.captures.add(id="active", active=not stable and mode != "idle")
                            reply.artifacts.add(id="retained", type="org.mantis.RawCapture", state="FINALIZED")
                            if mode.startswith("manual:"):
                                id = mode.split(":", 1)[1]
                            if mode in ("retry-older", "retry-older-loaded"):
                                reply.artifacts.add(id="older", type="org.mantis.PointCloud", state="FINALIZED", schema_version=1)
                            if mode == "retry-duplicates":
                                reply.artifacts.add(id=id, type="org.mantis.PointCloud", state="FINALIZED", schema_version=1)
                            if id:
                                reply.artifacts.add(id=id, type="org.mantis.PointCloud", state="FINALIZED",
                                                    schema_version=1, hash="h2" if mode == "retry-revision-fixed" else "h1", bytes=123, chunks=1)
                    elif command == "artifact_data":
                        id = request.artifact_data.id
                        data_requests[(last_project, id)] += 1
                        stable = mode == "auto-missing" or mode.startswith("retry-")
                        project, candidate, failure = retry_description(mode) if stable else (last_project, id, None)
                        if mode == "retry-async-hold":
                            assert not held_threads, "More than one pending artifact fetch"
                            held = connection.dup()
                            reply.data.transport, reply.data.format_version, reply.data.locator = "local-mapped-file", 1, str(valid)
                            payload = reply.SerializeToString()
                            def finish(sock=held, data=payload):
                                try:
                                    with sock:
                                        assert release.wait(8), "Held worker was never released"
                                        sock.sendall(struct.pack(">I", len(data)) + data)
                                except BaseException as error:
                                    failures.append(error)
                            worker = threading.Thread(target=finish)
                            held_threads.append(worker)
                            worker.start()
                            continue
                        if id == "transport-data":
                            mode = "offline"
                            continue
                        if id == "older" and mode == "retry-older-loaded": failure = "valid"
                        if failure in ("not-found", "io", "loss", "auth-loss"):
                            reply.error.code = 2 if failure == "not-found" else 1 if failure == "auth-loss" else 5
                            reply.error.component, reply.error.message = "fixture.artifact", "Artifact access refused"
                            if failure == "loss": mode = "retry-offline"
                            if failure == "auth-loss": mode = "retry-auth-offline"
                        else:
                            reply.data.transport = "local-mapped-file"
                            reply.data.format_version = 1
                            locator = wrong_type if failure == "wrong-type" else valid if failure == "valid" else corrupt if failure == "corrupt" else {"valid": valid, "corrupt": corrupt}.get(id, root / "missing.packet")
                            reply.data.locator = str(locator)
                    elif command == "events":
                        for (project, id), count in data_requests.items():
                            reply.events.add(kind="artifact_data", component=id, message=project, sequence=count)
                    else:
                        raise AssertionError(f"Unexpected fixture command: {command}")
                    data = reply.SerializeToString()
                    connection.sendall(struct.pack(">I", len(data)) + data)
        except BaseException as error:
            failures.append(error)

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        result = subprocess.run([str(build / "bin/mantis-studio-bridge-tests"), str(valid)],
                                env=dict(os.environ, MANTIS_PORT=str(port), MANTIS_TOKEN="bridge-fixture-token"),
                                text=True, capture_output=True, timeout=30)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
    finally:
        release.set()
        stop.set()
        thread.join(timeout=6)
        for worker in held_threads: worker.join(timeout=6)
        listener.close()
    assert not thread.is_alive(), "Wire fixture did not terminate"
    assert not failures, failures
    assert result.returncode == 0, result.returncode
    receipt = [{"project": project, "artifact": id, "artifact_data": count} for (project, id), count in sorted(data_requests.items())]
    out = build / "ui-m3a"
    out.mkdir(parents=True, exist_ok=True)
    (out / "wire-requests.json").write_text(json.dumps(receipt, indent=2) + "\n")
    (out / "wire.log").write_text(result.stdout + result.stderr)
    assert data_requests[("fixture-stable-project", "missing")] == 1, dict(data_requests)
    print("PASS: permanent missing auto fetch attempted once; subsequent confirmed snapshots suppressed")
    assert operations == ["reject-busy", "reject-io", "reject-invalid", "accepted", "reject-offline", "transport"], operations
