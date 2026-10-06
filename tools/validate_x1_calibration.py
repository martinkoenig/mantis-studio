#!/usr/bin/env python3
"""M8a public-SDK orchestration and retained-data characterization; no acceptance thresholds."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import platform
import secrets
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_x1_q6a import validate_profile
from generate_calibration_target import generate, load_metadata, verify_metadata, DEFAULT
from x1_calibration_report import (summary, validate_summary, fail, require, finite,
                                   repeatability, activation_report, COEFFICIENTS,
                                   validate_x1_rig, X1_RIG_FRAME_ID, X1_RIG_FRAME_NAME)


def write(path,value):
    finite(value)
    Path(path).write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n')


def read(path): return json.loads(Path(path).read_text())


def measured(path):
    m=read(path)
    require(set(m)<= {'active_width_mm','active_height_mm','width_uncertainty_mm','height_uncertainty_mm','instrument','note','mounting_note'},'Unknown measurement fields')
    for k in ('active_width_mm','active_height_mm'):
        require(type(m.get(k)) in (int,float) and math.isfinite(m[k]) and m[k]>0,f'{k} required, finite and positive')
    for k in ('width_uncertainty_mm','height_uncertainty_mm'):
        if m.get(k) is not None: require(type(m[k]) in (int,float) and math.isfinite(m[k]) and m[k]>=0,'Invalid uncertainty')
    require(isinstance(m.get('mounting_note'),str) and m['mounting_note'].strip(),'Physical mounting/substrate note required')
    finite(m); return m


class Harness:
    def __init__(self,args):
        self.args=args
        stamp=datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
        self.output=(args.output_root/f'x1-calibration-{stamp}').resolve()
        self.output.mkdir(parents=True,mode=0o700)
        self.report=summary(args.evidence_mode)
        self.report.update(started_utc=stamp,output=str(self.output),cleanup_errors=[])
        self.stage='preflight'; self.daemon=None; self.client=None; self.log=None; self.child=None
        self.env=os.environ.copy(); self.build=args.build_dir.resolve()
        self.tool=self.build/'bin/mantis-x1-calibration-validation'
        self.project=args.project.resolve() if args.project else self.output/'Calibration.mantis'
        self.target_id=args.target_id
        for name in ('repeatability','cross-validation','activation','determinism','session-a','session-b','session-c'):
            write(self.output/(name+'.json'),dict(schema_version=1,status='NOT ESTABLISHED'))
        for name in ('target','environment'):
            write(self.output/(name+'.json'),dict(schema_version=1,status='NOT ESTABLISHED'))
        (self.output/'target.sha256').write_text('NOT ESTABLISHED\n')
        (self.output/'commands.log').write_text('')
        (self.output/'daemon.log').write_text('No daemon started\n')

    def record_command(self,argv):
        with (self.output/'commands.log').open('a') as f: f.write(shlex.join(list(map(str,argv)))+'\n')

    def command(self,argv,timeout=3600):
        self.record_command(argv)
        with (self.output/'commands.log').open('a') as f:
            self.child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env=self.env,stdout=f,stderr=f,start_new_session=True)
            try:
                deadline=time.monotonic()+timeout; update=time.monotonic()+30
                while self.child.poll() is None:
                    require(time.monotonic()<deadline,'Command deadline exceeded')
                    if time.monotonic()>update:
                        print(f'Still running {argv[0]}; see commands.log',flush=True); update=time.monotonic()+30
                    time.sleep(.05)
                require(self.child.returncode==0,f'Command failed: {argv[0]}; see commands.log')
            finally:
                if self.child.poll() is None:
                    os.killpg(self.child.pid,signal.SIGINT)
                    try: self.child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(self.child.pid,signal.SIGKILL); self.child.wait(timeout=5)
                self.child=None

    def internal(self,mode,value):
        def manifest(s): return {k:s[k] for k in ('name','project','target','dataset','left','right','rig') if k in s}
        if mode=='export': value=manifest(value)
        if mode=='evaluate': value={k:manifest(v) for k,v in value.items()}
        self.record_command([self.tool,mode,'<',json.dumps(value,sort_keys=True)])
        self.child=subprocess.Popen([str(self.tool),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
        deadline=time.monotonic()+3600; update=time.monotonic()+30
        payload=json.dumps(value,allow_nan=False)
        try:
            while True:
                try:
                    stdout,stderr=self.child.communicate(input=payload,timeout=.25)
                    break
                except subprocess.TimeoutExpired:
                    payload=None
                    require(time.monotonic()<deadline,'Internal analysis deadline exceeded')
                    if time.monotonic()>update:
                        print(f'Still running internal {mode}; retained projects preserved',flush=True)
                        update=time.monotonic()+30
            require(self.child.returncode==0,stderr.strip() or f'Internal {mode} failed')
            return json.loads(stdout)
        finally:
            if self.child.poll() is None:
                os.killpg(self.child.pid,signal.SIGINT)
                try: self.child.wait(timeout=5)
                except subprocess.TimeoutExpired: self.child.kill(); self.child.wait(timeout=5)
            self.child=None

    def preflight(self):
        if self.args.prepare:
            self.command(['cmake','-S',ROOT,'-B',self.build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DMANTIS_BUILD_STUDIO=OFF',f'-DPython3_EXECUTABLE={sys.executable}'])
            self.command(['cmake','--build',self.build,'--parallel',str(self.args.jobs)])
            self.command(['ctest','--test-dir',self.build,'--output-on-failure'])
        require(self.tool.is_file(),'Run --prepare with the selected --build-dir first')
        profile=self.args.profile.resolve()
        try:
            git_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True,stderr=subprocess.DEVNULL).strip()
            git_status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True,stderr=subprocess.DEVNULL)
        except (OSError,subprocess.CalledProcessError):
            git_head=git_status='UNAVAILABLE'
        environment=dict(schema_version=1,evidence_mode=self.args.evidence_mode,
                         uname_m=platform.machine(),kernel=platform.release(),os_release=Path('/etc/os-release').read_text(),
                         git_head=git_head,git_status=git_status,
                         validation_tool_sha256=hashlib.sha256(self.tool.read_bytes()).hexdigest(),
                         opencv_version=self.internal('markers',DEFAULT)['opencv_version'],
                         profile_path=str(profile),profile_sha256=hashlib.sha256(profile.read_bytes()).hexdigest(),
                         fake_state=os.environ.get('MANTIS_X1_FAKE'),build_dir=str(self.build),project=str(self.project))
        write(self.output/'environment.json',environment)
        self.env['MANTIS_X1_PROFILE']=str(profile)
        if self.args.evidence_mode=='real' and ((self.args.session and not self.args.raw) or self.args.characterize or self.args.activation):
            require(platform.machine() in ('aarch64','arm64'),'Real collection requires native ARM64 Q6A')
            require(not os.environ.get('MANTIS_X1_FAKE'),'Real evidence rejects MANTIS_X1_FAKE')
            self.env.pop('MANTIS_X1_FAKE',None)
            require(git_head!='UNAVAILABLE','Real collection requires observable exact Git HEAD')
            validate_profile(read(profile))
        if self.args.target_metadata:
            self.target=load_metadata(self.args.target_metadata,self.tool)
        elif self.args.prepare:
            self.target=generate(DEFAULT,self.tool,self.output/'target.svg',shlex.join([sys.executable,str(ROOT/'tools/generate_calibration_target.py'),'--tool',str(self.tool),'--output',str(self.output/'target.svg')]))
        else: self.target=None
        if self.target:
            write(self.output/'target.json',self.target)
            (self.output/'target.sha256').write_text(self.target['sha256']+'\n')
        if self.args.session or self.args.characterize:
            require(self.target is not None and self.args.measurement is not None,'Supply --target-metadata and --measurement before collecting')
            measured(self.args.measurement)
        sys.path.insert(0,str(self.build/'python'))
        import mantis
        self.mantis=mantis
        self.env['PYTHONPATH']=str(self.build/'python')

    def start(self,name):
        require(self.daemon is None,'Daemon already owned')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
        self.env['MANTIS_PORT']=str(port); self.env['MANTIS_TOKEN']=secrets.token_hex(24)
        argv=[self.build/'bin/mantisd','--project',self.project,'--port',str(port)]
        self.record_command(argv)
        self.log=(self.output/f'daemon-{name}.log').open('a')
        self.daemon=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env=self.env,stdout=self.log,stderr=self.log,start_new_session=True)
        self.client=self.mantis.connect(port=port,token=self.env['MANTIS_TOKEN'])
        deadline=time.monotonic()+20
        while True:
            require(self.daemon.poll() is None,'Validation daemon exited; inspect log (stop other camera users manually)')
            try: self.client.snapshot(); break
            except (OSError,self.mantis.MantisError):
                require(time.monotonic()<deadline,'Daemon readiness timed out'); time.sleep(.1)

    def stop(self):
        if not self.daemon: return
        process=self.daemon
        try:
            if process.poll() is None:
                try: self.client.shutdown()
                except Exception: process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        finally:
            if self.log: self.log.close()
            self.daemon=self.client=self.log=None

    def discover(self):
        from google.protobuf.json_format import MessageToDict
        devices=self.client.devices.list()
        write(self.output/'devices.json',[MessageToDict(d,preserving_proto_field_name=True) for d in devices])
        parents=[d for d in devices if d.plugin_id=='org.mantis.x1' and not d.parent]
        require(len(parents)==1,'Exactly one X1 parent required')
        parent=parents[0]
        require(parent.metadata.get('backend')=='Linux V4L2','Fake backend cannot supply real evidence')
        validate_profile(json.loads(parent.metadata['profile']))
        children=[d for d in devices if d.parent==parent.id]
        require(len(children)==2 and set(parent.children)=={d.id for d in children},'Wrong discovered children')
        for role,sensor in [('left','ov9281 18-0060'),('right','ov9281 20-0060')]:
            found=[d for d in children if d.metadata.get('role')==role]
            require(len(found)==1 and found[0].metadata.get('identity')=='platform:acb3000.isp/'+sensor,'Wrong stable physical camera identity')
            require((found[0].metadata.get('width'),found[0].metadata.get('height'))==('1280','720'),'Wrong image geometry')
        return parent

    def capture(self,parent,name):
        reserve=math.ceil(1.25*276480000*(self.args.duration+1)+64*1024*1024)
        require(shutil.disk_usage(self.project).free>=reserve,'Insufficient storage reserve for next burst; retained data preserved')
        print('Hold the target stationary until this burst has finished.',flush=True)
        argv=[sys.executable,ROOT/'tools/validate_x1_pairing.py','--duration',str(self.args.duration)]
        self.record_command(argv)
        log=self.output/f'capture-{name}.log'
        with log.open('w') as f:
            self.child=subprocess.Popen(list(map(str,argv)),env=self.env,cwd=ROOT,stdout=f,stderr=f,start_new_session=True)
            try:
                self.child.wait(timeout=self.args.duration+180)
                require(self.child.returncode==0,f'Acquisition integrity failed; inspect {log}')
            finally:
                if self.child.poll() is None:
                    os.killpg(self.child.pid,signal.SIGINT)
                    try: self.child.wait(timeout=5)
                    except subprocess.TimeoutExpired: self.child.kill(); self.child.wait(timeout=5)
                self.child=None
        result=read(log)
        require(result['backend']=='Linux V4L2' and result['short_pairing_check']=='PASS','Invalid real acquisition report')
        return result['capture']['raw_artifact'],result

    def job(self,job):
        self.record_command(['SDK','job.wait',job.id]); return job.wait(timeout=3600).id

    def sdk_target(self):
        require(self.target is not None and self.args.measurement is not None,'--target-metadata and --measurement are required')
        m=measured(self.args.measurement); s=self.target['specification']
        note='; '.join(filter(None,[m.get('note'), 'Mounting: '+m['mounting_note']]))
        provenance=self.mantis.MeasurementProvenance(m.get('width_uncertainty_mm'),m.get('height_uncertainty_mm'),m.get('instrument'),note)
        measurement=self.mantis.PhysicalMeasurement(m['active_width_mm'],m['active_height_mm'],provenance)
        pattern=self.mantis.CharucoPattern(s['dictionary'],s['nominal_marker_size_mm'],s['layout']) if s['target_type']=='charuco' else self.mantis.CheckerboardPattern()
        if not self.target_id:
            self.record_command(['SDK','calibration.create_target',json.dumps(dict(specification=s,measurement=m),sort_keys=True)])
            self.target_id=self.client.calibration.create_target(self.mantis.TargetSpecification(s['squares_x'],s['squares_y'],s['nominal_square_size_mm'],pattern,measurement)).id
        write(self.output/'measurement.json',m)
        write(self.output/'target-artifact.json',dict(schema_version=1,project=str(self.project),target=self.target_id,measurement=m))
        return m

    def solve(self,raws):
        c=self.client.calibration
        self.record_command(['SDK','calibration.build_dataset',self.target_id,*raws])
        dataset=self.job(c.build_dataset(self.target_id,raws,roles=['left','right'],max_selected_per_camera=self.args.max_selected))
        self.record_command(['SDK','calibration.solve_camera',dataset])
        left=self.job(c.solve_camera(dataset,'left',heldout_per_camera=self.args.heldout))
        right=self.job(c.solve_camera(dataset,'right',heldout_per_camera=self.args.heldout))
        rig=None
        if self.target['specification']['target_type']=='charuco':
            self.record_command(['SDK','calibration.solve_rig',dataset,left,right])
            rig=self.job(c.solve_rig(dataset,left,right,heldout_pairs=self.args.heldout,rig_frame_id=X1_RIG_FRAME_ID,rig_frame_name=X1_RIG_FRAME_NAME))
        return dict(project=str(self.project),target=self.target_id,dataset=dataset,left=left,right=right,rig=rig)

    def target_contract(self,evidence,metadata,m):
        target=evidence['target']['payload']['target']; spec=metadata['specification']
        require(target['grid']==dict(squares_x=spec['squares_x'],squares_y=spec['squares_y'],nominal_square_size_mm=spec['nominal_square_size_mm']),'Target metadata/grid mismatch')
        require(target['pattern']['type']==spec['target_type'],'Target type mismatch')
        if spec['target_type']=='charuco':
            require(target['pattern']['definition']==dict(dictionary=spec['dictionary'],nominal_marker_size_mm=spec['nominal_marker_size_mm'],pattern_layout=spec['layout']),'Target pattern mismatch')
        for k in ('active_width_mm','active_height_mm'): require(target['measurement'][k]==m[k],'Physical measurement mismatch')
        expected_provenance={k:m.get(k) for k in ('width_uncertainty_mm','height_uncertainty_mm','instrument')}
        expected_provenance['note']='; '.join(filter(None,[m.get('note'),'Mounting: '+m['mounting_note']]))
        require(target['measurement']['provenance']==expected_provenance,'Measurement provenance differs from target revision')

    def session(self,name):
        self.stage='acquisition_integrity'
        self.start(name)
        raw_reports=[]
        try:
            m=self.sdk_target()
            if self.args.raw:
                raws=sorted(self.args.raw)
                for raw in raws:
                    artifact=next(a for a in self.client.artifacts.list() if a.id==raw)
                    require(artifact.state=='FINALIZED' and artifact.schema_version==2 and artifact.type=='org.mantis.RawCapture','Nonfinalized/wrong RawCapture')
                    first=self.client.replay.verify(raw); second=self.client.replay.verify(raw)
                    require(first==second and first['raw_integrity']==first['replay']=='PASS','Replay failed')
                    raw_reports.append(dict(raw=raw,verification=first,acquisition_integrity='NOT ESTABLISHED'))
            else:
                require(self.args.evidence_mode=='real','Fixture mode cannot collect real hardware evidence')
                parent=self.discover(); raws=[]
                for pose in range(self.args.poses):
                    input(f'{name} pose {pose+1:02d}/{self.args.poses}: reposition now; press Enter ONLY when board is stable: ')
                    raw,evidence=self.capture(parent,f'{name}-{pose+1:02d}')
                    raws.append(raw); raw_reports.append(evidence)
                    write(self.output/f'session-{name}-captures.json',raw_reports)
                    print('Burst finalized; you may reposition.',flush=True)
            self.stage='calibration_pipeline_execution'
            ids=self.solve(raws)
            duplicate=None
            if self.args.determinism: duplicate=self.solve(raws)
        finally: self.stop()
        evidence=self.internal('export',ids)
        if self.args.evidence_mode=='real':
            if ids['rig']: validate_x1_rig(evidence)
            for role,sensor in [('left','ov9281 18-0060'),('right','ov9281 20-0060')]:
                camera=evidence[role]['payload']['solution']['camera']
                require(camera['camera_id']['value']=='platform:acb3000.isp/'+sensor and (camera['image_width'],camera['image_height'])==(1280,720),'Nonreference physical dataset camera')
            for raw in evidence['raw_captures']:
                require(raw['provenance_parameters'].get('backend')=='Linux V4L2','Fixture RawCapture cannot be real evidence')
                validate_profile(json.loads(raw['provenance_parameters']['profile']))
        self.target_contract(evidence,self.target,m)
        session=dict(schema_version=1,name=name,evidence_mode=self.args.evidence_mode,**ids,
                     environment=read(self.output/'environment.json'),target_metadata=self.target,measurement=m,
                     acquisition=raw_reports,evidence=evidence,boundary='validation daemon restarted; rig mechanically unchanged (operator assertion)')
        records=evidence['dataset']['payload']['dataset']['records']
        session['dataset_counts']={role:dict(analyzed=sum(r['key']['camera_role']==role for r in records),detected=sum(r['key']['camera_role']==role and r['observation'] is not None for r in records),selected=sum(r['key']['camera_role']==role and r['selection_rank'] is not None for r in records)) for role in ('left','right')}
        session['source_capture_count']=len(raws)
        write(self.output/f'session-{name}.json',session)
        if duplicate:
            other=self.internal('export',duplicate)
            checks={key:evidence[key]['payload']['dataset' if key=='dataset' else 'solution']==other[key]['payload']['dataset' if key=='dataset' else 'solution'] for key in ('dataset','left','right')}
            if ids['rig']: checks['rig']=evidence['rig']['payload']['solution']==other['rig']['payload']['solution']
            write(self.output/'determinism.json',dict(schema_version=1,status='PASS' if all(checks.values()) else 'FAIL',original=ids,repeat=duplicate,checks=checks,scope='same target/sources/configuration and recorded OpenCV/build only'))
            require(all(checks.values()),'Meaningful dataset/solve determinism mismatch')
        self.report['calibration_pipeline_execution']='PASS'
        self.report['deterministic_replay']='PASS'
        if not self.args.raw and self.args.evidence_mode=='real': self.report['acquisition_integrity']='PASS'
        return session

    def analyze(self,sessions):
        self.stage='characterization'
        sessions=sorted(sessions,key=lambda s:s['name'])
        for s in sessions:
            require(s['schema_version']==1 and s['evidence_mode']==self.args.evidence_mode,'Mixed/invalid evidence modes')
            require(isinstance(s['name'],str) and s['name'].isascii() and s['name'].replace('-','').replace('_','').isalnum(),'Invalid retained session name')
            s['project']=str(Path(s['project']).resolve())
            # Reload immutable evidence rather than trusting edited summary geometry.
            s['evidence']=self.internal('export',s)
            if self.args.evidence_mode=='real': validate_x1_rig(s['evidence'])
            if s.get('target_metadata'):
                verify_metadata(s['target_metadata'],self.tool)
                self.target_contract(s['evidence'],s['target_metadata'],s['measurement'])
                write(self.output/'target.json',s['target_metadata'])
                (self.output/'target.sha256').write_text(s['target_metadata']['sha256']+'\n')
            else: require(self.args.evidence_mode=='fixture','Real session lacks target SVG metadata')
            write(self.output/f'session-{s["name"]}.json',s)
        write(self.output/'repeatability.json',repeatability(sessions))
        matrix=[self.internal('evaluate',dict(calibration_session=a,dataset_session=b)) for a,b in itertools.permutations(sessions,2)]
        write(self.output/'cross-validation.json',dict(schema_version=1,classification='CHARACTERIZATION',ordered_pairs=matrix))
        if self.args.checkerboard_session:
            cb=read(self.args.checkerboard_session); cb['evidence']=self.internal('export',cb)
            require(cb['evidence_mode']==self.args.evidence_mode,'Mixed Checkerboard evidence mode')
            if self.args.evidence_mode=='real':
                verify_metadata(cb['target_metadata'],self.tool)
                self.target_contract(cb['evidence'],cb['target_metadata'],cb['measurement'])
            require(cb['evidence']['rig'] is None and cb['evidence']['target']['payload']['target']['pattern']['type']=='checkerboard','Checkerboard must be mono only')
            comparisons=[]
            for s in sessions:
                for role in ('left','right'):
                    a=s['evidence'][role]['payload']['solution']; b=cb['evidence'][role]['payload']['solution']
                    require(a['camera']==b['camera'],'Checkerboard camera identity/geometry mismatch')
                    comparisons.append(dict(charuco_session=s['name'],checkerboard_session=cb['name'],role=role,deltas={k:b['final_model'][k]-a['final_model'][k] for k in COEFFICIENTS}))
            write(self.output/'checkerboard-comparison.json',dict(schema_version=1,classification='CHARACTERIZATION',comparisons=comparisons))
        if self.args.evidence_mode=='real':
            for s in sessions:
                require(s['environment']['uname_m'] in ('aarch64','arm64') and not s['environment']['fake_state'],'Invalid original real-hardware provenance')
                for role,sensor in [('left','ov9281 18-0060'),('right','ov9281 20-0060')]:
                    camera=s['evidence'][role]['payload']['solution']['camera']
                    require(camera['camera_id']['value']=='platform:acb3000.isp/'+sensor and (camera['image_width'],camera['image_height'])==(1280,720),'Wrong real camera identity/geometry')
                for raw in s['evidence']['raw_captures']:
                    require(raw['provenance_parameters'].get('backend')=='Linux V4L2','Fixture sources cannot become real characterization')
                    validate_profile(json.loads(raw['provenance_parameters']['profile']))
            if all(s['acquisition'] and all(r.get('short_pairing_check')=='PASS' and r.get('backend')=='Linux V4L2' for r in s['acquisition']) for s in sessions):
                self.report['acquisition_integrity']='PASS'
        self.report['characterization']='COMPLETE'
        self.report['calibration_pipeline_execution']='PASS'

    def activation(self,s):
        self.stage='activation_binding'
        require(self.args.evidence_mode=='real' and s['evidence_mode']=='real' and s['rig'],'Activation requires a real rig session')
        require(str(self.project)==s['project'],'Activation must use retained session project')
        validate_x1_rig(self.internal('export',s))
        self.start('activation')
        prior=None; parent=None; active_changed=False
        try:
            parent=self.discover(); c=self.client.calibration
            prior=c.active(parent.id)
            if prior is not None:
                from google.protobuf.json_format import MessageToDict
                write(self.output/'activation-prior.json',MessageToDict(prior,preserving_proto_field_name=True))
            c.activate(parent.id,s['rig'])
            active_changed=True
            current=c.active(parent.id)
            require(current is not None,'Active calibration unavailable')
            from google.protobuf.json_format import MessageToDict
            queried=MessageToDict(current,preserving_proto_field_name=True)
            write(self.output/'active-query.json',queried)
            input('Activation burst: press Enter when the target is stable (camera-only, lasers off): ')
            raw,acquisition=self.capture(parent,'activation')
            before=self.client.replay.verify(raw)
            self.record_command(['SDK','calibration.clear',parent.id])
            c.clear(parent.id)  # deliberately change active state to test historical interpretation
            after=self.client.replay.verify(raw)
            require(before==after,'Historical replay changed after clearing active calibration')
            write(self.output/'activation-acquisition.json',acquisition)
        finally:
            if active_changed:
                try:
                    if prior is not None: self.client.calibration.activate(parent.id,prior.artifact.id)
                    else: self.client.calibration.clear(parent.id)
                    require(self.client.calibration.active(parent.id)==prior,'Prior active state restoration mismatch')
                except Exception as e: self.report['cleanup_errors'].append('Restore active calibration: '+str(e))
            self.stop()
        profile=read(self.args.profile)
        args=dict(project=str(self.project),rig=s['rig'],raw=raw,logical_device_id=queried['logical_device_id'],source_id=profile.get('calibration_id',''),source_revision=str(profile.get('calibration_revision',0)))
        result=activation_report(self.internal('binding',args))
        result.update(evidence_mode='real',active_query=queried,replay_before=before,replay_after=after,historical_active_change='cleared then prior state restored')
        # Exact query must match artifact revision, not only an artifact ID.
        expected=self.internal('export',s)['artifacts']['rig']
        require(queried['artifact']['id']==s['rig'] and queried['artifact']['hash_algorithm']+':'+queried['artifact']['hash']==expected['hash'],'Active query exact artifact/hash mismatch')
        revision=result['binding']; ref=queried['reference']
        require(ref['id']==revision['id']['value'] and int(ref['revision'])==revision['revision'] and ref['schema_version']==revision['schema_version'],'Active query exact revision mismatch')
        write(self.output/'activation.json',result)
        self.report['activation_binding']=self.report['deterministic_replay']=self.report['acquisition_integrity']='PASS'

    def run(self):
        try:
            self.preflight()
            if self.args.characterize:
                require(not self.args.raw,'Characterize needs separately acquired sessions')
                print('Keep rig mechanically unchanged. Cameras only; all projectors/lasers must remain OFF.',flush=True)
                sessions=[]
                for name in ('a','b','c'):
                    if sessions:
                        if self.args.cold_boundary: input('Validation daemon stopped. Perform your chosen cold boundary, keep rig unchanged, then press Enter: ')
                        else: input('Validation daemon stopped. Begin a new independent session; press Enter to continue: ')
                    sessions.append(self.session(name))
                self.analyze(sessions)
                self.activation(sessions[-1])
            elif self.args.session: self.session(self.args.session)
            elif self.args.analyze: self.analyze([read(p) for p in self.args.session_input])
            elif self.args.activation:
                require(len(self.args.session_input)==1,'--activation requires exactly one --session-input')
                self.activation(read(self.args.session_input[0]))
            self.report['software_harness']='PASS'
        except (Exception,KeyboardInterrupt) as e:
            fail(self.report,self.stage,str(e) or 'Interrupted')
        finally:
            try: self.stop()
            except Exception as e: self.report['cleanup_errors'].append(str(e))
            if self.report['cleanup_errors']: fail(self.report,'cleanup','; '.join(self.report['cleanup_errors']))
            validate_summary(self.report)
            write(self.output/'summary.json',self.report)
            (self.output/'summary.txt').write_text('\n'.join(f'{k}: {v}' for k,v in self.report.items())+'\n')
            print(json.dumps(self.report,indent=2),flush=True)
        return 0 if self.report['software_harness']=='PASS' else 1


def parse(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    modes=p.add_mutually_exclusive_group(required=True)
    modes.add_argument('--prepare',action='store_true'); modes.add_argument('--session')
    for mode in ('analyze','activation','characterize'): modes.add_argument('--'+mode,action='store_true')
    p.add_argument('--evidence-mode',choices=['real','fixture'],default='real')
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/release')
    p.add_argument('--profile',type=Path,default=Path(os.environ.get('MANTIS_X1_PROFILE',ROOT/'profiles/x1-q6a.json')))
    p.add_argument('--output-root',type=Path,default=ROOT/'validation-output')
    p.add_argument('--project',type=Path)
    p.add_argument('--target-metadata',type=Path); p.add_argument('--measurement',type=Path); p.add_argument('--target-id')
    p.add_argument('--session-input',type=Path,action='append',default=[])
    p.add_argument('--checkerboard-session',type=Path)
    p.add_argument('--raw',action='append',default=[],help='Existing finalized RawCapture IDs; never establishes acquisition integrity')
    p.add_argument('--poses',type=int,default=24); p.add_argument('--duration',type=float,default=.25)
    p.add_argument('--max-selected',type=int,default=48); p.add_argument('--heldout',type=int,default=4)
    p.add_argument('--jobs',type=int,default=4); p.add_argument('--determinism',action='store_true')
    p.add_argument('--cold-boundary',action='store_true',help='Pause for operator-selected stronger boundary; never reboot automatically')
    a=p.parse_args(argv)
    require(math.isfinite(a.duration) and 0<a.duration<=60,'Burst duration must be in (0,60]')
    require(0<a.poses<=1024 and 0<a.max_selected and 0<a.heldout<a.max_selected and a.jobs>0,'Invalid pose/selection/split/build counts')
    if a.session: require(a.session.isascii() and a.session.replace('-','').replace('_','').isalnum(),'Session name must be a safe ASCII name')
    if a.characterize or (a.session and not a.raw) or a.activation: require(a.project is not None,'Explicit retained --project path required for real captures')
    return a


if __name__=='__main__':
    signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    sys.exit(Harness(parse()).run())
