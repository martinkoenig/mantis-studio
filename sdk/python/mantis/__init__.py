"""Mantis Client SDK v0.1. No bindings to C++ runtime internals."""
from __future__ import annotations
from dataclasses import dataclass
import os
from pathlib import Path
import socket
import struct
import time
import uuid
from . import mantis_pb2 as wire

class MantisError(RuntimeError):
    def __init__(self, message, code=0, component=""):
        super().__init__(message)
        self.code, self.component = code, component

@dataclass(frozen=True)
class Artifact:
    id: str

@dataclass(frozen=True)
class Capture:
    client: Client
    id: str
    raw_artifact: str
    def stop(self):
        return self.client._call(capture_stop=wire.Id(id=self.id))

@dataclass(frozen=True)
class Job:
    client: Client
    id: str
    def cancel(self):
        self.client._call(job_cancel=wire.Id(id=self.id))
    def wait(self, timeout=30.0):
        deadline = time.monotonic() + timeout
        while True:
            for job in self.client.snapshot().jobs:
                if job.id != self.id:
                    continue
                if job.state == "Completed":
                    return Artifact(job.result_artifact) if job.result_artifact else None
                if job.state in ("Failed", "Cancelled"):
                    raise MantisError(job.diagnostics or job.state, component="job")
            if time.monotonic() >= deadline:
                raise TimeoutError("Job remains owned by mantisd; waiting timed out")
            time.sleep(0.025)

class _Devices:
    def __init__(self, client): self.client = client
    def list(self): return list(self.client._call(devices_list=wire.Empty()).devices)

class _Capture:
    def __init__(self, client): self.client = client
    def start(self, devices):
        if not isinstance(devices, (list, tuple)):
            devices = [devices]
        ids = [d if isinstance(d, str) else d.id for d in devices]
        response = self.client._call(capture_start=wire.CaptureStart(devices=ids))
        capture = response.captures[0]
        return Capture(self.client, capture.id, capture.raw_artifact)

class _Pipeline:
    def __init__(self, client): self.client = client
    def run(self, capture=None, recipe="example", *, raw_artifact=None):
        def ident(value): return value if isinstance(value, str) else value.id if value else ""
        response = self.client._call(pipeline_run=wire.PipelineRun(
            capture_id=ident(capture), recipe=recipe, raw_artifact_id=ident(raw_artifact)))
        return Job(self.client, response.result_id)

class _Artifacts:
    def __init__(self, client): self.client = client
    def list(self): return list(self.client._call(artifacts_list=wire.Empty()).artifacts)
    def recover(self, artifact):
        ident = artifact if isinstance(artifact, str) else artifact.id
        return self.client._call(artifact_recover=wire.Id(id=ident)).artifacts[0]

class Client:
    def __init__(self, port=None, token=None):
        self.port = int(port or os.environ.get("MANTIS_PORT", "47321"))
        self.token = token or os.environ.get("MANTIS_TOKEN", "")
        if not self.token:
            raise ValueError("Set MANTIS_TOKEN to the daemon's access token")
        self.devices, self.capture = _Devices(self), _Capture(self)
        self.pipeline, self.artifacts = _Pipeline(self), _Artifacts(self)
    @staticmethod
    def _receive(sock, count):
        result = bytearray()
        while len(result) < count:
            part = sock.recv(count - len(result))
            if not part:
                raise ConnectionError("mantisd closed the control connection")
            result.extend(part)
        return bytes(result)
    def _call(self, **command):
        request = wire.Request(protocol_version=1, request_id=str(uuid.uuid4()), token=self.token, **command)
        payload = request.SerializeToString()
        if len(payload) > 4 * 1024 * 1024:
            raise ValueError("Control message exceeds limit")
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as sock:
            sock.sendall(struct.pack("!I", len(payload)) + payload)
            size, = struct.unpack("!I", self._receive(sock, 4))
            if not 0 < size <= 4 * 1024 * 1024:
                raise MantisError("Invalid control response length")
            response = wire.Response.FromString(self._receive(sock, size))
        if response.protocol_version != 1 or response.request_id != request.request_id:
            raise MantisError("Response version/correlation mismatch")
        if response.HasField("error"):
            raise MantisError(response.error.message, response.error.code, response.error.component)
        return response
    def snapshot(self): return self._call(snapshot=wire.Empty())
    def project(self, path, *, create=False):
        return self._call(project_open=wire.ProjectOpen(path=str(Path(path).resolve()), create=create)).project_path
    def export(self, artifact, path, *, wait=True):
        ident = artifact if isinstance(artifact, str) else artifact.id
        response = self._call(export_artifact=wire.Export(artifact_id=ident, path=str(Path(path).resolve())))
        job = Job(self, response.result_id)
        return job.wait() if wait else job
    def events(self, after=0): return list(self._call(events=wire.EventsSince(after=after)).events)
    def enable_plugin(self, plugin, enabled=True):
        self._call(plugin_enable=wire.PluginEnable(id=plugin, enabled=enabled))
    def shutdown(self): self._call(shutdown=wire.Empty())

def connect(*, port=None, token=None):
    return Client(port, token)
