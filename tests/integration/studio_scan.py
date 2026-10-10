"""Authenticated public-wire Scan fixture: existing bridge polling, no Scan commands."""
from pathlib import Path
import collections
import json
import os
import socket
import struct
import subprocess
import sys
import threading

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / 'python'))
from mantis import mantis_pb2 as w

out = build / 'ui-m3b/wire'
out.mkdir(parents=True, exist_ok=True)
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(16)
listener.settimeout(.2)
token = 'scan-wire-fixture-token'
stop = threading.Event()
requests, failures = [], []
mode = 0


def receive(sock, n):
    data = b''
    while len(data) < n:
        part = sock.recv(n - len(data))
        if not part:
            raise ConnectionError('fixture EOF')
        data += part
    return data


def serve():
    global mode
    try:
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            with conn:
                conn.settimeout(5)
                size, = struct.unpack('!I', receive(conn, 4))
                assert 0 < size <= 4 * 1024 * 1024
                req = w.Request.FromString(receive(conn, size))
                kind = req.WhichOneof('command')
                requests.append(kind)
                assert req.token == token
                response = w.Response(protocol_version=1, request_id=req.request_id)
                if kind == 'events':
                    # Read-only test controls, not a production Scan command or new API.
                    mode = req.events.after
                    assert mode in range(1, 4)
                elif kind == 'snapshot':
                    if mode == 1:
                        continue
                    response.project_path = 'B' if mode == 2 else 'A'
                    d = response.devices.add(id='stereo-parent', name='Public wire stereo descriptor')
                    d.capabilities.append('org.mantis.camera.frameset-stream.v1')
                    response.captures.add(id='existing-daemon-capture', active=mode != 2)
                    response.artifacts.add(id='raw-advertised', type='org.mantis.RawCapture', state='FINALIZED')
                elif kind == 'preview':
                    # The preserved bridge owns this path; no frame is ready in this fixture.
                    response.error.code = 6
                    response.error.component = 'wire.preview'
                    response.error.message = 'Fixture has no preview frame'
                else:
                    raise AssertionError(f'unintended mutation or data request: {kind}')
                payload = response.SerializeToString()
                conn.sendall(struct.pack('!I', len(payload)) + payload)
    except BaseException as error:
        failures.append(repr(error))


thread = threading.Thread(target=serve)
thread.start()
env = dict(os.environ, MANTIS_PORT=str(listener.getsockname()[1]), MANTIS_TOKEN=token,
           QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
try:
    proc = subprocess.run([str(build / 'bin/mantis-studio-scan-tests'), str(out), 'wire'],
                          env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35)
    (out / 'fixture.log').write_text(proc.stdout)
    print(proc.stdout)
    counts = dict(collections.Counter(requests))
    (out / 'requests.json').write_text(json.dumps(counts, indent=2) + '\n')
    assert proc.returncode == 0, proc.stdout
    assert not failures, failures
    assert counts.get('events') == 3, counts
    assert set(counts) <= {'snapshot', 'preview', 'events'}, counts
    assert 4 <= counts.get('snapshot', 0) < 35, counts
    assert counts.get('preview', 0) > 0, counts
    print('PASS: unchanged bridge snapshot/preview polling; zero Scan capture, stop, replay, pipeline, artifact_data, plugin or project writes', counts)
finally:
    stop.set()
    thread.join(timeout=3)
    listener.close()
