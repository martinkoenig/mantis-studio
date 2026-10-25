"""Actual public C++ client/bridge against a bounded deterministic wire peer.
The fixture has no production filesystem/catalog/data authority.
"""
from pathlib import Path
import collections,json,os,socket,struct,subprocess,sys,threading
build=Path(sys.argv[1]).resolve();sys.path.insert(0,str(build/'python'))
from mantis import mantis_pb2 as w
out=build/'ui-m2a/wire';out.mkdir(parents=True,exist_ok=True)
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(16);listener.settimeout(.2)
port=listener.getsockname()[1];token='projects-wire-fixture-token';stop=threading.Event();requests=[];failures=[];mode='initial'
def receive(sock,n):
    data=b''
    while len(data)<n:
        part=sock.recv(n-len(data))
        if not part:raise ConnectionError('fixture EOF')
        data+=part
    return data
def serve():
    global mode
    try:
        while not stop.is_set():
            try:conn,_=listener.accept()
            except socket.timeout:continue
            with conn:
                size,=struct.unpack('!I',receive(conn,4));assert 0<size<=4*1024*1024
                req=w.Request.FromString(receive(conn,size));kind=req.WhichOneof('command');requests.append(kind)
                r=w.Response(protocol_version=1,request_id=req.request_id)
                assert req.token==token
                if kind=='plugin_enable':
                    assert req.plugin_enable.id.startswith('projects-fixture:');mode=req.plugin_enable.id.split(':',1)[1]
                elif kind=='pipeline_run':
                    assert req.pipeline_run.recipe=='projects-reject';r.error.code=6;r.error.component='fixture.pipeline';r.error.message='Rejected fixture operation'
                elif kind=='project_open':
                    # Controlled capability audit only; disabled GUI actions emitted none.
                    assert req.project_open.path=='/runtime/Ambiguous.mantis' and req.project_open.create
                    mode='ambiguous';continue  # Operation took effect; response is genuinely lost.
                elif kind=='snapshot':
                    if mode=='offline':continue
                    if mode=='auth':r.error.code=1;r.error.component='fixture.auth';r.error.message='Invalid local access token'
                    else:
                        r.project_path='/runtime/Ambiguous.mantis' if mode=='ambiguous' else '/runtime/New.mantis' if mode=='new' else '/runtime/Wire.mantis'
                        for i in range(6):
                            a=r.artifacts.add();a.id=f'artifact-{i:05}';a.type='org.mantis.RawCapture' if i%2==0 else 'org.mantis.CalibrationTarget';a.state='FINALIZED';a.chunks=i
                else:raise AssertionError(f'unintended operation/data-plane request: {kind}')
                payload=r.SerializeToString();conn.sendall(struct.pack('!I',len(payload))+payload)
    except BaseException as e:failures.append(repr(e))
thread=threading.Thread(target=serve);thread.start()
env=dict(os.environ,MANTIS_PORT=str(port),MANTIS_TOKEN=token,QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software')
try:
    proc=subprocess.run([str(build/'bin/mantis-studio-projects-tests'),str(out),'wire'],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=35)
    (out/'fixture.log').write_text(proc.stdout);print(proc.stdout)
    counts=dict(collections.Counter(requests));(out/'requests.json').write_text(json.dumps(counts,indent=2)+'\n')
    assert proc.returncode==0,proc.stdout
    assert not failures,failures
    assert counts.get('pipeline_run')==1 and counts.get('plugin_enable')==4,counts
    assert counts.get('project_open')==1,counts
    assert set(counts)<= {'snapshot','pipeline_run','plugin_enable','project_open'},counts
    assert counts.get('snapshot',0)<40,counts
    print('PASS: public Client/StudioBridge/Projects, source-specific passive interactions, refusal/busy lease, stale/auth/reconnect; zero GUI project_open/capture/replay/artifact_data/preview requests; one controlled uncertain-outcome public-wire audit',counts)
finally:
    stop.set();thread.join(timeout=3);listener.close()
