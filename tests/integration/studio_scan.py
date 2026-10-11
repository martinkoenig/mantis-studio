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
import time

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / 'python'))
from mantis import mantis_pb2 as w

capture_test = '--capture' in sys.argv
out = build / ('ui-m3c/wire' if capture_test else 'ui-m3b/wire')
out.mkdir(parents=True, exist_ok=True)
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(16)
listener.settimeout(.2)
token = 'scan-wire-fixture-token'
stop = threading.Event()
requests, failures = [], []
mode = 10 if capture_test else 0
active = False
offline = False
operations = []
held_threads = []
if capture_test:
    subprocess.run([str(build / "bin/mantis-studio-scan-tests"), str(out), "fixtures"], check=True)


def receive(sock, n):
    data = b''
    while len(data) < n:
        part = sock.recv(n - len(data))
        if not part:
            raise ConnectionError('fixture EOF')
        data += part
    return data


def serve():
    global mode, active, offline
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
                    if capture_test:
                        if req.events.after == 90:
                            for name, count in collections.Counter(requests).items():
                                response.events.add(kind=name, sequence=count)
                        else:
                            mode = req.events.after
                            assert 10 <= mode <= 31
                            offline = mode == 25
                            active = mode in (11, 21, 22, 28, 31)
                    else:
                        mode = req.events.after
                        assert mode in range(1, 4)
                elif capture_test and kind == 'snapshot':
                    if offline:
                        continue
                    response.project_path = 'B' if mode == 23 else 'A'
                    if mode != 14:
                        d = response.devices.add(id='stereo-parent', name='Public wire stereo descriptor')
                        if mode == 15:
                            d.id = 'x' * 4097
                        if mode == 16:
                            d.id = ''
                        if mode == 27:
                            d.parent = 'another-parent'
                        if mode != 13:
                            d.capabilities.append('org.mantis.camera.frameset-stream.v1')
                        if mode == 29:
                            d.capabilities.extend(['unknown-capability'] * 65)
                        if mode == 12:
                            response.devices.add().CopyFrom(d)
                        if mode == 26:
                            for index in range(256):
                                response.devices.add(id=f'extra-{index}', capabilities=['org.mantis.camera.image-stream.v1'])
                    c = response.captures.add(id='wire-capture', active=active)
                    if mode == 21:
                        c.id = ''
                    if mode == 28:
                        c.id = 'x' * 4097
                    if mode in (11, 22):
                        response.captures.add(id='other-capture' if mode == 11 else 'wire-capture', active=True)
                    response.artifacts.add(id='raw-advertised', type='org.mantis.RawCapture', state='FINALIZED')
                    if mode != 23:
                        response.artifacts.add(id='fixture-cloud', type='org.mantis.PointCloud', state='FINALIZED', schema_version=1, hash='fixture')
                elif capture_test and kind in ('capture_start', 'capture_stop'):
                    operations.append({'mode': mode, 'command': kind})
                    if kind == 'capture_start':
                        assert list(req.capture_start.devices) == ['stereo-parent']
                        assert not active and mode in (17, 18, 19, 20), (mode, kind)
                        if mode == 18:
                            offline = True
                            continue
                        if mode == 19:
                            held = conn.dup()
                            def timeout_socket(sock=held):
                                with sock:
                                    stop.wait(6.2)
                            worker = threading.Thread(target=timeout_socket)
                            held_threads.append(worker)
                            worker.start()
                            continue
                        if mode == 20:
                            time.sleep(.3)  # completion lease, repeated input and route changes
                            active = True
                            response.result_id = 'wire-capture'
                        else:
                            response.error.code = 6
                            response.error.component = 'wire.capture_start'
                            response.error.message = 'Start rejected by fixture daemon'
                    else:
                        assert req.capture_stop.id == 'wire-capture' and active and mode in (20, 31)
                        if mode == 31:
                            response.error.code = 6
                            response.error.component = 'wire.capture_stop'
                            response.error.message = 'Stop rejected by fixture daemon'
                        else:
                            active = False
                elif capture_test and kind == 'artifact_data':
                    assert req.artifact_data.id == 'fixture-cloud'
                    response.data.artifact_id = req.artifact_data.id
                    response.data.transport = 'local-mapped-file'
                    response.data.format_version = 1
                    response.data.locator = str(out / 'cloud.packet')
                elif capture_test and kind == 'preview':
                    assert req.preview.id == 'wire-capture'
                    response.data.transport = 'local-mapped-file'
                    response.data.format_version = 2
                    response.data.locator = str(out / 'frames.packet')
                    response.data.lease_id = 'fixture-preview'
                elif capture_test and kind == 'preview_release':
                    assert req.preview_release.id == 'fixture-preview'
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
    proc = subprocess.run([str(build / 'bin/mantis-studio-scan-tests'), str(out), 'capture-wire' if capture_test else 'wire'],
                          env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=75 if capture_test else 35)
    (out / 'fixture.log').write_text(proc.stdout)
    print(proc.stdout)
    counts = dict(collections.Counter(requests))
    (out / 'requests.json').write_text(json.dumps(counts, indent=2) + '\n')
    assert proc.returncode == 0, proc.stdout
    assert not failures, failures
    if capture_test:
        expected = [
            {'mode': 20, 'command': 'capture_start'}, {'mode': 20, 'command': 'capture_stop'},
            {'mode': 17, 'command': 'capture_start'}, {'mode': 31, 'command': 'capture_stop'},
            {'mode': 18, 'command': 'capture_start'}, {'mode': 19, 'command': 'capture_start'}]
        assert operations == expected, operations
        assert counts.get('capture_start') == 4 and counts.get('capture_stop') == 2, counts
        assert counts.get('artifact_data') == 2, counts  # initial A; A→B→A; never route/source loads
        assert counts.get('preview', 0) > 0 and counts.get('preview_release', 0) == counts['preview'], counts
        assert set(counts) <= {'events','snapshot','preview','preview_release','artifact_data','capture_start','capture_stop'}, counts
        (out / 'operations.json').write_text(json.dumps(operations, indent=2) + '\n')
        print('PASS: guarded capture start/stop, rejections/closure/timeout, one preview path, zero navigation data loads', counts)
    else:
        assert counts.get('events') == 3, counts
        assert set(counts) <= {'snapshot', 'preview', 'events'}, counts
        assert 4 <= counts.get('snapshot', 0) < 35, counts
        assert counts.get('preview', 0) > 0, counts
        print('PASS: unchanged bridge snapshot/preview polling; zero Scan capture, stop, replay, pipeline, artifact_data, plugin or project writes', counts)
finally:
    stop.set()
    thread.join(timeout=3)
    listener.close()
    for worker in held_threads:
        worker.join(timeout=2)
