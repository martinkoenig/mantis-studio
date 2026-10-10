"""Authenticated public-wire Devices fixture; accepts only read-only commands."""
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

out = build / 'ui-m2b/wire'
out.mkdir(parents=True, exist_ok=True)
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(16)
listener.settimeout(.2)
token = 'devices-wire-fixture-token'
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
                size, = struct.unpack('!I', receive(conn, 4))
                assert 0 < size <= 4 * 1024 * 1024
                req = w.Request.FromString(receive(conn, size))
                kind = req.WhichOneof('command')
                requests.append(kind)
                assert req.token == token
                response = w.Response(protocol_version=1, request_id=req.request_id)
                if kind == 'events':
                    # Explicit read-only fixture controls; never a hardware command.
                    mode = req.events.after
                    assert mode in range(1, 7)
                elif kind == 'snapshot':
                    if mode == 1:
                        continue
                    if mode == 2:
                        response.error.code = 1
                        response.error.component = 'fixture.auth'
                        response.error.message = 'Invalid local access token'
                    else:
                        response.project_path = '/fixture/Devices.mantis'
                        if mode != 3:
                            for id, cap, parent in (
                                ('demo-device', 'org.mantis.camera.frameset-stream.v1', ''),
                                ('left', 'org.mantis.camera.image-stream.v1', 'demo-device'),
                                ('right', 'org.mantis.camera.image-stream.v1', 'demo-device'),
                                ('emitter', 'org.mantis.emitter.power-control.v1', 'demo-device'),
                                ('image-only', 'org.mantis.camera.image-stream.v1', ''),
                                ('unknown', 'org.example.sensor.raw.v7', ''),
                            ):
                                d = response.devices.add(id=id, name=id, parent=parent,
                                                         plugin_id='org.example.device')
                                d.capabilities.append(cap)
                                if id == 'demo-device':
                                    d.children.extend(['left', 'right', 'emitter'])
                                d.metadata['vendor.literal'] = '<b>Untrusted metadata</b>'
                            if mode == 6:
                                d = response.devices[-1]
                                d.name = 'Untrusted \u202e third-party \x01 descriptor'
                                for i in range(200):
                                    d.metadata[f'key-{i}'] = '<b>literal</b>' * 500
                            response.plugins.add(id='org.example.device', state='Failed' if mode == 5 else 'Enabled',
                                                 diagnostic='Plugin-only diagnostic', version='fixture-1')
                            response.events.add(sequence=2**64-1, kind='warning', component='runtime',
                                                message='demo-device mentioned without device attribution')
                else:
                    raise AssertionError(f'unintended mutation / data-plane request: {kind}')
                payload = response.SerializeToString()
                conn.sendall(struct.pack('!I', len(payload)) + payload)
    except BaseException as error:
        failures.append(repr(error))


thread = threading.Thread(target=serve)
thread.start()
env = dict(os.environ, MANTIS_PORT=str(listener.getsockname()[1]), MANTIS_TOKEN=token,
           QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
try:
    proc = subprocess.run([str(build / 'bin/mantis-studio-devices-tests'), str(out), 'wire'],
                          env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35)
    (out / 'fixture.log').write_text(proc.stdout)
    print(proc.stdout)
    counts = dict(collections.Counter(requests))
    (out / 'requests.json').write_text(json.dumps(counts, indent=2) + '\n')
    assert proc.returncode == 0, proc.stdout
    assert not failures, failures
    assert counts.get('events') == 6, counts
    assert set(counts) <= {'snapshot', 'events'}, counts
    assert 6 <= counts.get('snapshot', 0) < 35, counts
    print('PASS: only expected read-only calls; zero capture, projected-light, preview, firmware, project_open or settings calls', counts)
finally:
    stop.set()
    thread.join(timeout=3)
    listener.close()
