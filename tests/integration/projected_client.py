"""Actual loopback framing, C++ SDK and CLI with a test-only projected daemon."""
from pathlib import Path
import json, os, socket, subprocess, sys, tempfile, time
build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='projected-client-', dir=build) as directory:
    root = Path(directory)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    env = os.environ.copy(); env.update(MANTIS_PORT=str(port), MANTIS_TOKEN='projected-local-test-token')
    log = open(root/'server.log', 'w+')
    server = subprocess.Popen([str(build/'bin/mantis-projected-control-server'),
        str(build/'contract-plugins/libmantis-projected-contract.so'), str(build/'contract-plugins'),
        str(root/'project'), str(port)], env=env, stdout=log, stderr=log)
    def cli(*args, ok=True):
        result = subprocess.run([str(build/'bin/mantis-cli'), *args], env=env, text=True, capture_output=True, timeout=15)
        if ok: assert result.returncode == 0, result.stderr; return json.loads(result.stdout)
        assert result.returncode != 0; return result.stderr
    try:
        for _ in range(100):
            if server.poll() is not None: log.seek(0); raise AssertionError(log.read())
            ready = subprocess.run([str(build/'bin/mantis-cli'),'projected','devices'], env=env, capture_output=True)
            if ready.returncode == 0: break
            time.sleep(.03)
        else: raise AssertionError('Projected server startup timeout')
        subprocess.run([str(build/'bin/mantis-projected-client-tests')], env=env, check=True, timeout=20)
        graph = cli('projected','devices')['projected_devices'][0]; assert graph['parent_id']=='parent-alpha'
        captures = cli('projected','list')['projected_captures']; assert len(captures)==1
        capture=captures[0]; assert cli('projected','status',capture['id'])['projected_captures'][0]['state']=='PROJECTED_COMPLETED'
        reference=cli('projected','bundle',capture['id'])['data']; assert reference['format_version']==3
        assert Path(reference['locator']).read_bytes()[:8]==b'MANTIS03'
        cli('projected','stop',capture['id'],capture['run_id'],'stale',ok=False)
        raw=capture['raw_artifact_id']
        assert cli('projected','validate','org.example.projected-contract','parent-alpha','--program-from-raw',raw,'--queue-capacity','1')['projected_validation']['accepted']
        def start_from_raw(request_id):
            return cli('projected', 'start', 'org.example.projected-contract', 'parent-alpha',
                       '--program-from-raw', raw, '--request-id', request_id)['projected_captures'][0]
        started = start_from_raw('cli-explicit-retry')
        retry = start_from_raw('cli-explicit-retry')
        assert (started['id'], started['run_id'], started['raw_artifact_id']) == (
            retry['id'], retry['run_id'], retry['raw_artifact_id'])
        for _ in range(100):
            status = cli('projected', 'status', started['id'])['projected_captures'][0]
            if status['storage_state'] == 'PROJECTED_STORAGE_FINALIZED': break
            time.sleep(.01)
        else: raise AssertionError('CLI projected finalization timeout')
        program = root/'program.json'
        program.write_text(json.dumps({
            'type': 'org.mantis.AcquisitionProgram', 'schema_version': 1,
            'identity': {'id': 'cli-program', 'hash': {'presence': 'PROJECTED_UNKNOWN'},
                         'content': {'presence': 'PROJECTED_UNAVAILABLE'}},
            'participants': {'cameras': [{'component': 'camera-alpha', 'stream': 'image-stream',
                                         'role': 'imaging'}],
                             'emitters': ['emitter-alpha'], 'controllers': ['controller-alpha']},
            'steps': [{'index': 17, 'label': 'OFF frame',
                       'emitters': [{'emitter': 'emitter-alpha', 'state': 'PROJECTED_OFF'}],
                       'capture': {'mode': 'PROJECTED_FREE_RUNNING', 'cameras': ['camera-alpha']},
                       'evidence_requirement': 'PROJECTED_COMMANDED_ONLY',
                       'required_scope': 'PROJECTED_CONTROLLER_REGISTER', 'max_duration_ns': '100000000'}],
            'repetitions': '1', 'bounds': {'max_duration_ns': '2000000000',
                'max_on_duration_ns': '1000000000', 'max_step_instances': '100', 'max_commands': '100',
                'max_events': '100', 'max_bytes': '67108864', 'max_in_flight_captures': '1'}
        }))
        assert cli('projected', 'validate', 'org.example.projected-contract', 'parent-alpha',
                   '--program', str(program))['projected_validation']['accepted']
        inline = cli('projected', 'start', 'org.example.projected-contract', 'parent-alpha',
                     '--program', str(program), '--request-id', 'cli-inline')['projected_captures'][0]
        assert inline['program']['id'] == 'cli-program'
        program=root/'invalid.json'; program.write_text('{"unknown_field":true}')
        assert 'unknown_field' in cli('projected','validate','org.example.projected-contract','parent-alpha','--program',str(program),ok=False)
        program.write_text('x'*(512*1024+1))
        assert '512 KiB' in cli('projected','start','org.example.projected-contract','parent-alpha','--program',str(program),ok=False)
        cli('shutdown'); assert server.wait(timeout=5)==0
    finally:
        if server.poll() is None: server.terminate(); server.wait(timeout=5)
        log.close()
print('Projected loopback SDK and CLI passed')
