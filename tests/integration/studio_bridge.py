"""Deterministic wire fault fixture exercising StudioBridge through mantis-client."""
from pathlib import Path
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
    corrupt = root / "corrupt.packet"
    corrupt.write_bytes(b"invalid packet fixture")
    failures = []
    operations = []
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

    def serve():
        mode, revision = "active", 0
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
                        mode = request.plugin_enable.id.split(":", 1)[1]
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
                        if mode == "offline":
                            continue
                        if mode == "auth":
                            reply.error.code = 1
                            reply.error.message = "Invalid local access token"
                        else:
                            revision += 1
                            reply.project_path = f"fixture-project-{revision}"
                            device = reply.devices.add(id="real-device", name="Fixture camera")
                            device.capabilities.append("org.mantis.camera.image-stream.v1")
                            reply.captures.add(id="active", active=mode != "idle")
                            artifact = reply.artifacts.add(id="missing" if mode == "auto-missing" else "retained",
                                                           state="FINALIZED")
                            artifact.type = "org.mantis.PointCloud" if mode == "auto-missing" else "org.mantis.RawCapture"
                    elif command == "artifact_data":
                        if request.artifact_data.id == "transport-data":
                            mode = "offline"
                            continue
                        reply.data.transport = "local-mapped-file"
                        reply.data.format_version = 1
                        reply.data.locator = str({"valid": valid, "corrupt": corrupt}.get(
                            request.artifact_data.id, root / "missing.packet"))
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
        stop.set()
        thread.join(timeout=6)
        listener.close()
    assert not thread.is_alive(), "Wire fixture did not terminate"
    assert not failures, failures
    assert result.returncode == 0, result.returncode
    assert operations == ["reject-busy", "reject-io", "reject-invalid", "accepted", "reject-offline", "transport"], operations
