"""A real listener remains quiet across its former five-second idle timeout."""
from pathlib import Path
import os
import selectors
import socket
import subprocess
import sys
import tempfile
import time

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / "python"))
import mantis

with tempfile.TemporaryDirectory(prefix="mantis-listener-") as directory:
    env = os.environ.copy()
    env.pop("MANTIS_X1_PROFILE", None)
    env.pop("MANTIS_X1_FAKE", None)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    token = "listener-" + os.urandom(16).hex()
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN=token)
    client = mantis.connect(port=port, token=token)
    daemon = subprocess.Popen([str(build / "bin/mantisd"), "--project", str(Path(directory) / "Project.mantis")],
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while True:
            assert daemon.poll() is None, "Daemon exited during startup"
            try:
                client.snapshot()
                break
            except (OSError, mantis.MantisError):
                assert time.monotonic() < deadline, "Daemon startup timed out"
                # Readiness retries have a deadline; the idle test below does not poll.
                time.sleep(0.01)
        errors = bytearray()
        with selectors.DefaultSelector() as selector:
            selector.register(daemon.stderr, selectors.EVENT_READ)
            # Intentionally exceed the old SO_RCVTIMEO; wait for observable output.
            deadline = time.monotonic() + 6.2
            while time.monotonic() < deadline:
                for key, _ in selector.select(max(0, deadline - time.monotonic())):
                    data = os.read(key.fileobj.fileno(), 65536)
                    assert data, "Daemon stderr closed while listener should be waiting"
                    errors.extend(data)
        assert b"Accept failed" not in errors, errors.decode(errors="replace")
        assert client.devices.list(), "Listener stopped responding after idle wait"
        client.shutdown()
        _, tail = daemon.communicate(timeout=5)
        errors.extend(tail)
        assert daemon.returncode == 0 and b"Accept failed" not in errors, errors.decode(errors="replace")
        print("Blocking idle listener remains quiet and handles requests/shutdown after six seconds")
    finally:
        if daemon.poll() is None:
            daemon.kill()
            daemon.communicate(timeout=5)
