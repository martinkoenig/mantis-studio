"""CI validates M8a software only. All generated evidence is fixture evidence."""
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
BUILD=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(ROOT/'tools'))
from generate_calibration_target import DEFAULT, backend, generate, load_metadata, svg_bytes, validate
from x1_calibration_report import summary, validate_summary, fail, rotation_delta, stats, repeatability, activation_report, validate_x1_rig
from validate_x1_calibration import Harness, parse, measured
TOOL=BUILD/'bin/mantis-x1-calibration-validation'


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); cls.root=Path(cls.tmp.name)
        subprocess.run([BUILD/'bin/mantis-x1-calibration-evaluator-tests',cls.root],check=True,timeout=120)
        cls.sessions=[json.loads((cls.root/f'session-{n}.json').read_text()) for n in ('a','b','c')]

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def test_target_determinism_and_metadata(self):
        a=generate(DEFAULT,TOOL,self.root/'target.svg','test command')
        data=(self.root/'target.svg').read_bytes()
        b=generate(DEFAULT,TOOL,self.root/'target.svg','test command')
        self.assertEqual(a,b); self.assertEqual(data,(self.root/'target.svg').read_bytes())
        self.assertEqual(a['sha256'],hashlib.sha256(data).hexdigest())
        self.assertEqual(load_metadata(self.root/'target.json',TOOL),a)
        self.assertEqual((a['active_grid_width_mm'],a['active_grid_height_mm']),(320,240))
        # Frozen vector bytes independently checked against known Tier-1 dictionary.
        self.assertEqual(a['sha256'],'ead437fa538c41d7a2c8091bd60397e0c5230308aa78e24eeb41f2b92cdfaed9')
        (self.root/'target.svg').write_bytes(data+b' ')
        with self.assertRaises(ValueError): load_metadata(self.root/'target.json',TOOL)
        (self.root/'target.svg').write_bytes(data)

    def test_invalid_generation(self):
        for field,value in [('layout','white_square_at_origin_even_rows'),('nominal_square_size_mm',float('nan')),
                            ('nominal_marker_size_mm',40),('squares_x',0),('squares_y',6.5),('page_width_mm',100),('dictionary','DICT_4X4_50')]:
            s=DEFAULT|{field:value}
            with self.assertRaises(ValueError): validate(s)

    def test_svg_raster_detect_physical_ids(self):
        # Rasterize ACTUAL SVG rectangles, rather than another OpenCV board generator.
        for nx,ny in ((8,6),(7,5),(8,5),(7,6)):
            spec=DEFAULT|dict(squares_x=nx,squares_y=ny)
            generate(spec,TOOL,self.root/'render.svg','test')
            root=ET.fromstring((self.root/'render.svg').read_bytes()); scale=4
            w,h=1680,1188; pixels=bytearray([255])*(w*h)
            for r in root.iter('{http://www.w3.org/2000/svg}rect'):
                if r.attrib.get('fill')=='white': continue
                x,y=round(float(r.attrib['x'])*scale),round(float(r.attrib['y'])*scale)
                rw=round((float(r.attrib['x'])+float(r.attrib['width']))*scale)-x
                rh=round((float(r.attrib['y'])+float(r.attrib['height']))*scale)-y
                for row in range(y,y+rh): pixels[row*w+x:row*w+x+rw]=bytes(rw)
            image=self.root/'render.pgm'; image.write_bytes(f'P5\n{w} {h}\n255\n'.encode()+pixels)
            points=backend(TOOL,'detect',dict(specification=spec,image=str(image)))['points']
            self.assertEqual([p['id'] for p in points],list(range((nx-1)*(ny-1))))
            for p in points:
                x,y=(p['id']%(nx-1)+1)*40,(p['id']//(nx-1)+1)*40
                self.assertEqual((p['x_mm'],p['y_mm']),(x,y))
                self.assertLess(abs(p['x_px']-((420-nx*40)/2+x)*scale),1.5)
                self.assertLess(abs(p['y_px']-((297-ny*40)/2+y)*scale),1.5)

    def test_rotation_and_statistics(self):
        eye=[1,0,0,0,1,0,0,0,1]
        self.assertEqual(rotation_delta(eye,eye)['angle_rad'],0)
        for angle in (1e-10,.31,math.pi):
            r=[math.cos(angle),-math.sin(angle),0,math.sin(angle),math.cos(angle),0,0,0,1]
            self.assertAlmostEqual(rotation_delta(eye,r)['angle_rad'],angle,places=12)
        for r in ([1]*9,[1]*8,[float('nan')]*9,[-1,0,0,0,1,0,0,0,1]):
            with self.assertRaises(ValueError): rotation_delta(eye,r)
        self.assertEqual(stats([1,2,3]),dict(min=1,max=3,mean=2,sample_stddev=1))
        with self.assertRaises(ValueError): stats([1,float('inf')])

    def test_repeatability_order_deltas_and_independence(self):
        sessions=copy.deepcopy(self.sessions)
        sessions[1]['evidence']['left']['payload']['solution']['final_model']['fx']=sessions[0]['evidence']['left']['payload']['solution']['final_model']['fx']+2
        a=repeatability(sessions); b=repeatability(list(reversed(sessions)))
        self.assertEqual(a,b)
        self.assertEqual(a['pairs'][0]['cameras']['left']['fx_delta'],2)
        self.assertAlmostEqual(a['pairs'][0]['cameras']['left']['fx_relative_delta'],.002,places=6)
        sessions[1]['evidence']['raw_captures']=sessions[0]['evidence']['raw_captures']
        with self.assertRaises(ValueError): repeatability(sessions)

    def test_authoritative_rig_frame_contract(self):
        evidence=copy.deepcopy(self.sessions[0]['evidence'])
        evidence['rig']=json.loads((self.root/'rig-canonical.json').read_text())
        validate_x1_rig(evidence)
        canonical=evidence['rig']['payload']['solution']
        for name in ('wrong-id','wrong-name'):
            other=json.loads((self.root/f'rig-{name}.json').read_text())
            # Both are valid immutable M4/M5 artifacts with identical numerical geometry.
            for key in ('T_rig_from_left','T_rig_from_right','T_right_from_left'):
                self.assertEqual(other['payload']['solution']['rig'][key]['matrix'],canonical['rig'][key]['matrix'])
            evidence['rig']=other
            with self.assertRaisesRegex(ValueError,'canonical X1 rig frame'):
                validate_x1_rig(evidence)
        evidence['rig']=json.loads((self.root/'rig-canonical.json').read_text())
        for transform in ('T_rig_from_left','T_rig_from_right'):
            wrong=copy.deepcopy(evidence)
            wrong['rig']['payload']['solution']['rig'][transform]['target']['name']='Wrong target name'
            with self.assertRaisesRegex(ValueError,'canonical X1 rig frame'):
                validate_x1_rig(wrong)

    def test_real_analysis_and_activation_reject_immutable_wrong_frame(self):
        s=copy.deepcopy(self.sessions[0]); s['evidence_mode']='real'
        evidence=copy.deepcopy(s['evidence'])
        evidence['rig']=json.loads((self.root/'rig-wrong-id.json').read_text())
        for mode in ('--analyze','--activation'):
            a=parse([mode,'--project',s['project'],'--build-dir',str(BUILD),'--output-root',str(self.root/'frame-rejection')])
            h=Harness(a)
            with patch.object(h,'internal',return_value=evidence),patch.object(h,'start') as start:
                with self.assertRaisesRegex(ValueError,'canonical X1 rig frame'):
                    if a.analyze: h.analyze([s])
                    else: h.activation(s)
                start.assert_not_called()

    def test_repeatability_content_hashes_across_projects(self):
        sessions=copy.deepcopy(self.sessions)
        self.assertEqual(repeatability(sessions)['session_order'],['a','b','c'])
        a,b=sessions[:2]
        b['project']=str(self.root/'another-project.mantis')
        for index,raw in enumerate(b['evidence']['raw_captures']): raw['id']=f'imported-{index}'
        # Disjoint source hashes still pass with different projects and IDs.
        repeatability(sessions)
        shared=a['evidence']['raw_captures'][0]['hash']
        # Same digest under another algorithm is not the same complete hash identity.
        b['evidence']['raw_captures'][0]['hash']='another-algorithm:'+shared.partition(':')[2]
        repeatability(sessions)
        b['evidence']['raw_captures'][0]['hash']=shared
        with self.assertRaisesRegex(ValueError,'share source RawCapture content hashes'):
            repeatability(sessions)
        for value in (None,'',':digest','fnv1a64:'):
            b['evidence']['raw_captures'][0]['hash']=value
            with self.assertRaisesRegex(ValueError,'RawCapture content hash'):
                repeatability(sessions)

    def test_report_protection_failure_and_binding_parsing(self):
        for mode in ('fixture','real'):
            s=summary(mode); validate_summary(s)
            s['final_m8_acceptance']='PASS'
            with self.assertRaises(ValueError): validate_summary(s)
            fail(s,'calibration_pipeline_execution','invalid lineage'); validate_summary(s)
            self.assertEqual(s['software_harness'],'FAIL')
        s=summary('fixture'); s['acquisition_integrity']='PASS'
        with self.assertRaises(ValueError): validate_summary(s)
        r=dict(schema_version=1,status='PASS',framesets=2,binding=dict(id={'value':'rig'},schema_version=1,revision=3),raw=dict(state='FINALIZED',schema_version=2))
        self.assertEqual(activation_report(r),r)
        r['binding']['revision']=0
        with self.assertRaises(ValueError): activation_report(r)

    def test_noninteractive_analysis(self):
        env=os.environ.copy(); env['MANTIS_X1_FAKE']='normal'
        argv=[sys.executable,ROOT/'tools/validate_x1_calibration.py','--analyze','--evidence-mode','fixture','--build-dir',BUILD,'--output-root',self.root/'reports']
        for n in ('c','a','b'): argv += ['--session-input',self.root/f'session-{n}.json']
        subprocess.run(list(map(str,argv)),env=env,check=True,timeout=120,stdout=subprocess.DEVNULL)
        output=next((self.root/'reports').iterdir())
        s=json.loads((output/'summary.json').read_text()); validate_summary(s)
        self.assertEqual(s['characterization'],'COMPLETE'); self.assertEqual(s['final_m8_acceptance'],'NOT ESTABLISHED')
        matrix=json.loads((output/'cross-validation.json').read_text())['ordered_pairs']
        self.assertEqual([(r['calibration_session'],r['dataset_session']) for r in matrix],[('a','b'),('a','c'),('b','a'),('b','c'),('c','a'),('c','b')])

    def test_fake_backend_is_rejected_even_on_arm(self):
        a=parse(['--session','a','--project',str(self.root/'Q6A.mantis'),'--build-dir',str(BUILD),'--output-root',str(self.root/'fake')])
        h=Harness(a)
        with patch('validate_x1_calibration.platform.machine',return_value='aarch64'),patch.dict(os.environ,{'MANTIS_X1_FAKE':'normal'}):
            self.assertEqual(h.run(),1)
        self.assertEqual(h.report['acquisition_integrity'],'NOT ESTABLISHED')
        self.assertEqual(h.report['final_m8_acceptance'],'NOT ESTABLISHED')
        class Device:
            plugin_id='org.mantis.x1'; parent=''; metadata={'backend':'deterministic fixture'}
        from types import SimpleNamespace
        h.client=SimpleNamespace(devices=SimpleNamespace(list=lambda:[Device()]))
        # Reject observed fake backend independently of the environment guard.
        with patch('google.protobuf.json_format.MessageToDict',return_value={}):
            with self.assertRaises(ValueError): h.discover()

    def test_interrupted_cleanup_owns_only_child(self):
        a=parse(['--analyze','--evidence-mode','fixture','--build-dir',str(BUILD),'--output-root',str(self.root/'interrupt')]); h=Harness(a)
        unrelated=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])
        owned=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])
        h.daemon=owned
        from types import SimpleNamespace
        h.client=SimpleNamespace(shutdown=lambda:owned.terminate())
        try:
            with patch.object(h,'preflight',side_effect=KeyboardInterrupt): self.assertEqual(h.run(),1)
            self.assertIsNotNone(owned.poll()); self.assertIsNone(unrelated.poll())
            self.assertTrue((h.output/'summary.json').is_file())
        finally: unrelated.terminate(); unrelated.wait(timeout=5)

    def test_retained_session_sdk_orchestration_and_determinism(self):
        # Control-path fixture checks the actual runner arguments and report writes.
        from types import SimpleNamespace
        original=copy.deepcopy(self.sessions[0]); evidence=original['evidence']
        metadata=generate(DEFAULT,TOOL,self.root/'session-target.svg','test')
        measurement=dict(active_width_mm=330.4,active_height_mm=243.6,mounting_note='fixture plate')
        path=self.root/'session-measurement.json'; path.write_text(json.dumps(measurement))
        a=parse(['--session','retained','--evidence-mode','fixture','--project',original['project'],
                 '--build-dir',str(BUILD),'--output-root',str(self.root/'sdk'),
                 '--target-id',original['target'],'--target-metadata',str(self.root/'session-target.json'),
                 '--measurement',str(path),'--raw','one','--raw','two','--determinism'])
        h=Harness(a); h.preflight()
        evidence['target']['payload']['target']['measurement']['active_width_mm']=330.4
        evidence['target']['payload']['target']['measurement']['active_height_mm']=243.6
        evidence['target']['payload']['target']['measurement']['provenance']['note']='Mounting: fixture plate'
        calls=[]
        def job(kind): return SimpleNamespace(id=kind,wait=lambda timeout:SimpleNamespace(id=original[kind]))
        class Calibration:
            def build_dataset(self,target,raws,**config): calls.append(('dataset',target,raws,config)); return job('dataset')
            def solve_camera(self,dataset,role,**config): calls.append(('camera',dataset,role,config)); return job(role)
            def solve_rig(self,dataset,left,right,**config): calls.append(('rig',dataset,left,right,config)); return job('rig')
        fake=SimpleNamespace(calibration=Calibration(),artifacts=SimpleNamespace(list=lambda:[SimpleNamespace(id=id,state='FINALIZED',schema_version=2,type='org.mantis.RawCapture') for id in ('one','two')]),replay=SimpleNamespace(verify=lambda raw:dict(raw_integrity='PASS',replay='PASS')))
        def start(name): h.client=fake
        with patch.object(h,'start',side_effect=start),patch.object(h,'stop'),patch.object(h,'internal',return_value=evidence):
            result=h.session('retained')
        self.assertEqual([x[0] for x in calls],['dataset','camera','camera','rig']*2)
        self.assertEqual(calls[0][3],dict(roles=['left','right'],max_selected_per_camera=48))
        self.assertEqual(calls[1][3],dict(heldout_per_camera=4))
        for call in (calls[3],calls[7]):
            self.assertEqual(call[4],dict(heldout_pairs=4,rig_frame_id='org.mantis.x1.rig',rig_frame_name='Mantis X1 rig'))
        self.assertEqual(result['dataset_counts']['left']['analyzed'],26)
        self.assertEqual(result['source_capture_count'],2)
        self.assertEqual(json.loads((h.output/'determinism.json').read_text())['status'],'PASS')
        self.assertEqual(h.report['acquisition_integrity'],'NOT ESTABLISHED')
        self.assertEqual(h.report['final_m8_acceptance'],'NOT ESTABLISHED')

    def test_measurement_and_invalid_controls(self):
        path=self.root/'measurement.json'
        path.write_text(json.dumps(dict(active_width_mm=319.3,active_height_mm=240.5,mounting_note='rigid flat substrate')))
        self.assertNotIn('width_uncertainty_mm',measured(path))
        for value in (float('nan'),0):
            path.write_text(json.dumps(dict(active_width_mm=value,active_height_mm=240,mounting_note='plate')))
            with self.assertRaises(ValueError): measured(path)
        for args in (['--session','../a'],['--analyze','--duration','nan'],['--analyze','--heldout','48']):
            with self.assertRaises(ValueError): parse(args)


if __name__=='__main__': unittest.main()
