"""Public wire → real StudioBridge collector → rendered Home integration; no hardware."""
from pathlib import Path
import json
import os
import socket
import struct
import subprocess
import sys
import threading

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / "python"))
from mantis import mantis_pb2 as wire
output = build / "ui-m1/wire"
output.mkdir(parents=True, exist_ok=True)
stop = threading.Event()
failures, operations, commands = [], [], []
with socket.socket() as listener:
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    listener.settimeout(0.1)
    def exact(connection, size):
        data = bytearray()
        while len(data) < size:
            chunk = connection.recv(size - len(data))
            assert chunk, "Truncated fixture request"
            data.extend(chunk)
        return bytes(data)
    def serve():
        offline, reconnected, point_advertised = False, False, False
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
                    assert request.protocol_version == 1 and request.token == "home-wire-fixture-token"
                    command = request.WhichOneof("command")
                    commands.append(command)
                    reply = wire.Response(protocol_version=1, request_id=request.request_id)
                    if command == "snapshot":
                        if offline:
                            continue
                        reply.project_path = "/fixture/<b>literal</b>/" + ("Reconnected.mantis" if reconnected else "Wire.mantis")
                        device = reply.devices.add(id="wire-camera", name="Wire fixture camera")
                        device.capabilities.append("org.mantis.camera.image-stream.v1")
                        reply.jobs.add(id="wire-job", name="Wire processing", state="Running", progress=0.64)
                        reply.jobs.add(id="wire-failed", name="Wire storage", state="Failed", diagnostics="Fixture write rejected")
                        reply.artifacts.add(id="wire-raw", type="org.mantis.RawCapture", state="FINALIZED", chunks=12)
                        if point_advertised:
                            reply.artifacts.add(id="missing-home-data", type="org.mantis.PointCloud", state="FINALIZED", schema_version=1)
                        reply.events.add(sequence=74, kind="info", component="wire.fixture", message="Read-only Home fixture snapshot")
                    elif command == "plugin_enable":
                        if request.plugin_enable.id == "home-fixture-advertise-cloud":
                            point_advertised = True
                        else:
                            assert request.plugin_enable.id == "home-fixture-reconnect"
                            offline, reconnected, point_advertised = False, True, False
                    elif command == "pipeline_run":
                        operations.append(request.pipeline_run.recipe)
                        if request.pipeline_run.recipe == "transport-home":
                            offline = True
                            continue
                        assert request.pipeline_run.recipe == "reject-home"
                        reply.error.code = 6
                        reply.error.component = "wire.pipeline"
                        reply.error.message = "Rejected fixture operation"
                    elif command == "artifact_data":
                        assert request.artifact_data.id == "missing-home-data"
                        reply.data.transport = "local-mapped-file"
                        reply.data.format_version = 1
                        reply.data.locator = str(output / "does-not-exist.packet")
                    else:
                        raise AssertionError(f"Home sent unexpected command {command}")
                    data = reply.SerializeToString()
                    connection.sendall(struct.pack(">I", len(data)) + data)
        except BaseException as error:
            failures.append(error)
    thread = threading.Thread(target=serve)
    thread.start()
    try:
        result = subprocess.run([str(build / "bin/mantis-studio-home-tests"), str(output), "wire"],
                                env=dict(os.environ, MANTIS_PORT=str(listener.getsockname()[1]),
                                         MANTIS_TOKEN="home-wire-fixture-token"),
                                text=True, capture_output=True, timeout=45)
        (output / "fixture.log").write_text(result.stdout + result.stderr)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
    finally:
        stop.set()
        thread.join(timeout=6)
    (output / "commands.json").write_text(json.dumps(commands, indent=2))
    assert not thread.is_alive(), "Fixture failed to terminate"
    assert not failures, failures
    assert result.returncode == 0, result.returncode
    assert operations == ["reject-home", "transport-home"], operations
    assert commands.count("artifact_data") == 1 and commands.count("plugin_enable") == 2, commands
    assert set(commands) == {"snapshot", "pipeline_run", "artifact_data", "plugin_enable"}, commands
print("PASS: bounded public-wire fixture; no capture, project, calibration or cancellation mutation")
