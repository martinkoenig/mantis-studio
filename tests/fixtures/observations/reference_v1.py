"""Independent MLOBS001 version-1 fixture authoring, never invoked by tests.
Refuses overwriting published fixtures. No production codec/catalog is parsed.
Shared nested values use the immutable independently authored L4 fixture grammar.
"""
import importlib.util
from pathlib import Path
import struct
P = Path(__file__).parent
spec = importlib.util.spec_from_file_location('v3', P.parent/'storage/reference_v3.py')
v = importlib.util.module_from_spec(spec); spec.loader.exec_module(v)
U,S,D,A,O,E,T = v.U,v.S,v.D,v.A,v.O,v.E,v.T

def attribute(name, scalar, shape, stride, unit, payload):
    return S(name)+bytes([scalar])+A([U(n) for n in shape])+A([U(n) for n in stride])+S(unit)+U(len(payload))+payload

def observation(mode):
    context = (v.camera_frame+E(v.frameset)+E(S('run')+U(1))+
        E(S('raw-art')+S('org.mantis.RawCapture')+U(3)+E(S('fnv1a64')+S('abcd'))+U(0))+
        S('optical')+S('Optical')+E(b''.join(D(n) for n in [1,0,.25,0,1,0,0,0,1])+E(v.ref('preprocess')))+
        A([S('emit')])+A([v.emitter_full])+A([S('emit')+S('pattern')+U(7)])+
        E(v.program_ref+v.step(9))+E(S('run')+U(4))+A([v.trigger(2)])+A([v.mapping])+
        S('org.example.synthetic-observation')+U(1)+U(0)+U(0)+S('fixture-build')+E(v.ref('producer-config'))+
        E(v.ref('parameters'))+A([v.ref('fixture-input')])+O(b'\1')+E(v.host(21))+U(0x80000000))
    n = 0 if mode in (2,3,5) else 2
    attrs=[]
    if n:
        prefix='org.mantis.laser.'
        attrs.append(attribute(prefix+'source_pixel',2,[2,2],[8,4],'pixel',struct.pack('<4f',.25,.5,.25,.5)))
        attrs.append(attribute(prefix+'quality_flags',4,[2],[4],'',struct.pack('<2I',*[0x40000003 if mode==1 else 0x40000000]*2)))
        for name in ['emitter_index','line_index']:
            attrs.append(attribute(prefix+name,4,[2],[4],'',struct.pack('<2I',0,0)))
        for name in ['emitter_valid','line_valid']:
            attrs.append(attribute(prefix+name,0,[2],[1],'',bytes([0 if mode==1 else 1]*2)))
        if mode!=1:
            attrs.append(attribute(prefix+'confidence',2,[2],[4],'',struct.pack('<2f',.75,.75)))
            attrs.append(attribute(prefix+'confidence_valid',0,[2],[1],'',b'\1\1'))
        if mode==4: attrs.append(attribute('org.example.future.width',3,[2],[8],'pixel',D(-0.0)+D(-0.0)))
    confidence = v.unavailable if mode==1 else E(S('org.example.confidence')+U(1)+U(2)+U(3)+S('test')+E(v.ref()))
    body=(T('org.mantis.LaserObservation')+E(S('run'))+S('producer')+S('producer-gen')+U(1)+context+
          O(bytes([1 if mode==3 else 2 if mode==5 else 0]))+U(n)+A([S('emit')])+
          A([S('emit')+S('pattern')+U(7)+S('stripe')])+A(attrs)+confidence+S('synthetic fixture'))
    return b'MLOBS001'+U(1)+U(len(body))+U(v.fnv(body))+body+b'MLOBEND1'+U(~len(body))
if __name__=='__main__':
    for mode,name in enumerate(['known','unknown','empty','failed','extension','unavailable']):
        with (P/(name+'.bin')).open('xb') as f: f.write(observation(mode))
