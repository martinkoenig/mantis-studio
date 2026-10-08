"""Independent, explicit storage-v1 fixture construction; NEVER run by tests.

This records the fixture derivation for review, not a tool to bless codec changes.
A byte mismatch is a regression. New encodings require new versions/fixtures.
No C++ source/catalog or codec is imported or parsed.
"""
from pathlib import Path
import struct
P = Path(__file__).parent
U = lambda n: struct.pack('<Q', n & ((1 << 64)-1))
S = lambda s: U(len(s.encode())) + s.encode()
D = lambda d: struct.pack('<d', d)
A = lambda xs: U(len(xs)) + b''.join(xs)
O = lambda x: b'\0' if x is None else b'\1'+x
E = lambda x: b'\2'+x
unknown, unavailable = b'\0', b'\1'
T = lambda name: S(name)+U(1)
ref = lambda id='cfg': S(id)+T('org.example.Config')+E(S('fnv1a64')+S('abcd'))+U(5)
program_ref = S('prog')+E(S('fnv1a64')+S('abcd'))+E(S('prog')+T('org.mantis.AcquisitionProgram')+E(S('fnv1a64')+S('abcd'))+U(2))
participants = A([S('cam')+S('stream')+S('left')])+A([S('emit')])+A([S('ctl')])
clock = lambda domain, name: S(domain)+S(name)+S('gen')
host = lambda n=0: U(n)+clock('host','runtime')
sensor = lambda n=0: U(n)+clock('sensor','device')
interval = sensor()+sensor(10)
frame = S('cam')+S('stream')+S('gen')+U(7)
frameset = S('run')+S('set')+S('gen')+U(7)
trigger = lambda n: S('run')+S('ctl')+S('gen')+U(n)
step = lambda n: S('run')+U(0)+U(n)
proof = lambda method=1,source='ctl': S(source)+bytes([method])+E(ref())
cal = lambda kind: S('cal')+U(1)+U(3)+E(S('camera-art' if kind=='CameraCalibration' else 'rig-art')+T('org.mantis.'+kind)+E(S('fnv1a64')+S('0123'))+U(3))
mapping = S('sensor')+S('device')+S('host')+S('runtime')+D(1.25)+D(-0.0)+D(0.0)+S('gen')+S('gen')+ref('mapping')
native = S('ctl')+S('gen')+U(0)
association = lambda n: frame+trigger(n)+b'\0'+proof(6,'cam')+E(native)
camera_frame = (frame+S('left')+U(4)+U(1)+E(sensor())+E(host(9))+E(b'\0')+
                E(E(U(10))+unavailable+E(U(10))+E(interval)+proof(6,'cam')+E(D(0.0)))+
                E(S('sync')+U(2)+b'\2'+E(association(2)))+E(cal('CameraCalibration'))+E(cal('RigCalibration'))+unavailable)
emitter_empty = S('emit')+unknown+unavailable+unknown+A([])
ack = lambda req,stage: S(req)+O(bytes([stage]))+O(b'\0')+proof()+E(sensor())+b'\0'
emitter_full = (S('emit')+E(S('on-request')+S('emit')+b'\1'+host())+
                E(ack('on-request',1))+E(O(b'\1')+b'\1'+proof(3)+E(sensor())+E(interval))+
                A([frame+E(frame+O(b'\1')+b'\2'+proof(4,'emit')+interval)]))
implementation = S('org.example.Executor')+U(1)+U(2)+U(3)+S('build')+E(ref())

def event(i):
    kind=[0,1,4][i]
    return (T('org.mantis.TriggerEvent')+trigger(i)+step(9)+S('req')+
            (E(native) if i==2 else unavailable)+O(bytes([kind]))+
            (E(ack('req',0)) if i==1 else unknown)+proof(0 if i==0 else 1)+E(sensor())+E(host(9))+E(host())+
            E(D(0.0))+E(mapping)+A([S('cam')])+(E(A([S('cam')])) if i==2 else unknown)+E(frame)+
            (E(association(2)) if i==2 else unknown))

def evidence(n):
    preds=[S('run')+U((n-1)*4)] if n else []
    if n==2: preds.append(S('run')+U(0))
    loss = b'\6'+S('cam')+E(U(0))+b'\1'+E(frame)+E(S('req'))
    return (T('org.mantis.AcquisitionEvidence')+S('run')+U(n*4)+program_ref+
            (E(step(9 if n==1 else 42)) if n in (1,2) else unknown)+A(preds)+participants+A([implementation])+
            A([emitter_full if n==1 else emitter_empty])+(E(frameset) if n==1 else unavailable)+
            A([camera_frame] if n==1 else [])+A([trigger(2)] if n==1 else [trigger(i) for i in range(3)] if n==2 else [])+
            A([mapping] if n==1 else [])+(E(cal('RigCalibration')) if n==1 else unknown)+
            O(bytes([[0,1,2,4][n]]))+bytes([3 if n==3 else 0])+
            A([S('lost')+S('emit')+b'\5'] if n==3 else [])+A([loss] if n==3 else [])+
            S('explicit failure' if n==3 else 'fixture'))
member = lambda kind, b: U(kind)+U(len(b))+b

def bundle(n):
    e=b'MEVID001'+U(1)+evidence(n)
    members=[member(1,e)]
    if n==1: members.append(member(2,(P/'mantis02.bin').read_bytes()))
    if n==2: members.extend(member(3,b'MTRIG001'+U(1)+event(i)) for i in range(3))
    return b'MANTIS03'+U(1)+T('org.mantis.AcquisitionBundle')+S('run')+U(n)+host(n*1000000)+U(len(members))+b''.join(members)

def header():
    a=(U(9)+S('capture')+A([S('emit')+b'\1'])+O(b'\2')+A([S('cam')])+O(S('ctl')+S('req')+A([S('cam')]))+
       O(b'\2')+b'\2'+U(2)+U(1000000000))
    b=U(42)+S('dark')+A([S('emit')+b'\0'])+O(b'\0')+A([])+O(None)+O(b'\1')+b'\0'+U(0)+U(2000000000)
    p=T('org.mantis.AcquisitionProgram')+program_ref+participants+A([a,b])+U(3)+b''.join(U(n) for n in [10000000000,3000000000,6,100,100,16*1024*1024,2])+b'\0'
    body=p+S('run')+S('execution')+b''.join(U(n) for n in [16,101,102,103,104,4096])
    return b'MRUNHDR3'+U(1)+U(len(body))+U(fnv(body))+body+b'MRUNEND3'+U(~len(body))

def fnv(b):
    h=14695981039346656037
    for v in b: h=((h^v)*1099511628211)&((1<<64)-1)
    return h

if __name__=='__main__':
    # Explicit one-time derivation only. Refuse replacing any existing fixture.
    for n,name in enumerate(['evidence','captured','trigger','terminal']):
        with (P/('mantis03-'+name+'.bin')).open('xb') as f: f.write(bundle(n))
    with (P/'run-header3.bin').open('xb') as f: f.write(header())
    b=bundle(0)
    with (P/'mrawrec3.bin').open('xb') as f:
        f.write(b'MRAWREC3'+U(len(b))+U(fnv(b))+U(~len(b))+b+U(len(b)^0x4d414e5449533033))

# Follow-up final daemon outcome fixture. Existing bundle/header/legacy fixtures stay frozen.
def run_outcome():
    emit = (S('emit') + E(S('off-request') + S('emit') + b'\0' + host()) + unavailable + unknown +
            A([frame + unknown, S('cam') + S('stream') + S('gen') + U(8) + unavailable]))
    abort = E(S('run')) + unavailable + E(b'\1') + unknown + E(b'\1') + A([emit]) + U(7) + U(23)
    # state failed=2, reason device_failure=3; structured Error status plugin_failed=7.
    out = (S('run') + S('execution') + b'\2' + b'\3' + unavailable +
           E(b'\7' + S('abort failed') + S('executor')) + unknown + unavailable + E(abort) + S('cleanup fault'))
    body = U(4) + out
    return b'MRUNOUT3' + U(1) + U(len(body)) + U(fnv(body)) + body + b'MOUTEND3' + U(~len(body))
