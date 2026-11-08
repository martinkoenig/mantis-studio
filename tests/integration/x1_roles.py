"""Authenticated opt-in public role APIs and restart, using explicit software-only X1 fixture."""
from pathlib import Path
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

build, fixture = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
sys.path.insert(0, str(build / 'python'))
import mantis
from mantis import mantis_pb2 as wire

with tempfile.TemporaryDirectory(prefix='mantis-x1-roles-') as directory:
    root = Path(directory)
    state_dir = root / 'state'
    state_dir.mkdir(mode=0o700)
    original = fixture.read_bytes()
    profile = root / 'profile.json'
    profile.write_bytes(original)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    token = 'roles-' + os.urandom(16).hex()
    env = dict(os.environ, MANTIS_TOKEN=token, MANTIS_PORT=str(port), MANTIS_X1_PROFILE=str(profile),
               MANTIS_X1_FAKE='normal', MANTIS_X1_FAKE_PACE='1')
    # Inherited locator alone must not grant writable authority.
    env['MANTIS_X1_ROLE_STATE_DIR'] = str(state_dir)
    client = mantis.connect(port=port, token=token)
    log = open(root / 'daemon.log', 'w+')
    daemon = None
    def until(fn, timeout=20):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            result = fn()
            if result: return result
            time.sleep(.01)
        raise AssertionError('Role fixture condition timed out')
    def start(opt_in):
        args = [str(build / 'bin/mantisd'), '--project', str(root / 'project')]
        if opt_in: args += ['--x1-role-state-dir', str(state_dir)]
        process = subprocess.Popen(args, env=env, stdout=log, stderr=log)
        def ready():
            assert process.poll() is None, 'Role fixture daemon exited before ready'
            try: return client.x1_roles.query()
            except (OSError, mantis.MantisError): return None
        until(ready)
        return process
    def stop():
        client._call(shutdown=wire.Empty())
        assert daemon.wait(timeout=20) == 0
    def retired():
        state = client.x1_roles.query()
        return state if not state.busy else None
    def rejects(fn, text=None):
        try: fn()
        except mantis.MantisError as error:
            if text: assert text in str(error), str(error)
        else: raise AssertionError('Rejected role operation unexpectedly succeeded')
    try:
        daemon = start(False)
        assert not client.x1_roles.query().supported
        parent = next(d for d in client.devices.list() if d.plugin_id == 'org.mantis.x1' and not d.parent)
        assert 'org.mantis.x1.role-config.v1' not in parent.capabilities
        state = client.x1_roles.query()
        rejects(lambda: client.x1_roles.apply(state, swapped=True), 'not enabled')
        assert not (state_dir / 'x1-role-map.v1.json').exists()
        stop()
        daemon = start(True)
        initial = client.x1_roles.query()
        assert initial.supported and initial.generation == 0 and not initial.swapped
        assert [c.identity for c in initial.cameras] == ['fixture/ov9281 18-0060', 'fixture/ov9281 20-0060']
        assert all(c.bus == 'fixture' and c.route[0] == c.sensor for c in initial.cameras)
        parent = next(d for d in client.devices.list() if d.id == initial.parent_id)
        assert 'org.mantis.x1.role-config.v1' in parent.capabilities
        bad = mantis.connect(port=port, token='invalid-role-token')
        rejects(lambda: bad.x1_roles.apply(initial, swapped=True), 'Invalid local access token')
        capture = client.capture.start(parent)
        rejects(lambda: client.x1_roles.apply(initial, swapped=True), 'retirement')
        capture.stop()
        historical = next(a.SerializeToString() for a in client.artifacts.list() if a.id == capture.raw_artifact)
        client.x1_roles.validate(initial, swapped=True).wait()
        initial = until(retired)
        applied = client.x1_roles.apply(initial, swapped=True)
        applied.wait()
        after = until(retired)
        assert after.swapped and after.generation == 1
        assert [c.identity for c in after.cameras] == ['fixture/ov9281 20-0060', 'fixture/ov9281 18-0060']
        rejects(lambda: client.x1_roles.apply(initial, swapped=True), 'Stale')
        subprocess.run([str(build / 'bin/mantis-x1-role-client-tests'), 'swapped'], env=env, check=True, timeout=30)
        until(retired)
        assert profile.read_bytes() == original
        assert json.loads((state_dir / 'x1-role-map.v1.json').read_bytes())['generation'] == 1
        stop()
        daemon = start(True)
        restarted = client.x1_roles.query()
        assert restarted.swapped and restarted.generation == 1
        assert restarted.parent_id == after.parent_id
        assert next(a.SerializeToString() for a in client.artifacts.list() if a.id == capture.raw_artifact) == historical
        client.replay.verify(capture.raw_artifact)
        assert restarted.discovery_token != after.discovery_token
        rejects(lambda: client.x1_roles.apply(after, swapped=False), 'Stale')
        client.x1_roles.apply(restarted, swapped=False).wait()
        assert until(retired).generation == 2
        assert not client.x1_roles.query().swapped
        assert profile.read_bytes() == original
        # An old/incorrect peer cannot become a successful empty query.
        class Missing:
            def _call(self, **kwargs): return wire.Response()
        rejects(lambda: mantis._X1Roles(Missing()).query(), 'does not provide')
        stop()
        (state_dir / 'x1-role-map.v1.json').write_text('{"format_version":99}')
        failed = subprocess.run([str(build / 'bin/mantisd'), '--project', str(root / 'project'),
                                 '--x1-role-state-dir', str(state_dir)], env=env, stdout=log, stderr=log, timeout=20)
        assert failed.returncode != 0
        print('Token, opt-in, C++/Python public Jobs, stale requests, durable restart and tamper rejection passed')
    finally:
        if daemon and daemon.poll() is None:
            daemon.terminate()
            daemon.wait(timeout=20)
        log.close()
