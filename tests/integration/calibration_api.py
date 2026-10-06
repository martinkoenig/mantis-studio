"""Real daemon + generated Protobuf + C++/Python/CLI calibration workflows, no hardware."""
from pathlib import Path
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
build = Path(sys.argv[1]).resolve()
sys.path.insert(0,str(build / 'python'))
import mantis
with tempfile.TemporaryDirectory(prefix='mantis-m6-') as directory:
    root = Path(directory); project = root / 'Project.mantis'
    seed = json.loads(subprocess.check_output([str(build / 'bin/mantis-calibration-service-tests'),'seed',str(project)],text=True))
    env = os.environ.copy()
    env.pop('MANTIS_X1_PROFILE',None); env.pop('MANTIS_X1_FAKE',None)
    with socket.socket() as s: s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    env.update(MANTIS_PORT=str(port),MANTIS_TOKEN='calibration-test-'+os.urandom(16).hex())
    client=mantis.connect(port=port,token=env['MANTIS_TOKEN'])
    log=open(root / 'daemon.log','w+')
    daemon=subprocess.Popen([str(build / 'bin/mantisd'),'--project',str(project)],env=env,stdout=log,stderr=log)
    def cli(*args): return json.loads(subprocess.check_output([str(build / 'bin/mantis-cli'),'calibration',*args],env=env,text=True))
    def error(fn, code):
        try: fn()
        except mantis.MantisError as e: assert e.code == code, (e.code,str(e))
        else: raise AssertionError('Expected structured daemon error')
    try:
        for _ in range(200):
            if daemon.poll() is not None: log.seek(0); raise AssertionError(log.read())
            try: client.calibration.list(); break
            except (OSError,mantis.MantisError): time.sleep(.025)
        else: raise AssertionError('Daemon startup timeout')
        assert len(client.calibration.list()) == 4
        assert len(cli('list')['calibrations']) == 4
        # Target helpers, anisotropic extents and both provenance presence states across real wire.
        checker = client.calibration.create_checkerboard_target(squares_x=8,squares_y=6,square_mm=40)
        assert not client.calibration.info(checker).target_info.target.measurement.HasField('provenance')
        target = client.calibration.create_charuco_target(squares_x=8,squares_y=6,square_mm=40,marker_mm=25,
            dictionary='DICT_6X6_250',layout='black_square_at_origin',
            measurement=mantis.PhysicalMeasurement(330.4,243.6,mantis.MeasurementProvenance()))
        ti = client.calibration.info(target)
        assert ti.target_info.target.measurement.HasField('provenance')
        rev2 = client.calibration.create_charuco_target(squares_x=8,squares_y=6,square_mm=40,marker_mm=25,
            dictionary='DICT_6X6_250',layout='black_square_at_origin',series_id=ti.entry.reference.id)
        assert client.calibration.info(rev2).entry.reference.revision == 2
        assert client.calibration.info(target).entry.reference.revision == 1
        built = client.calibration.build_dataset(checker,seed['raw'],roles=['right','left'],max_selected_per_camera=3)
        assert isinstance(built,mantis.Job)
        dataset = built.wait(timeout=90)
        di=client.calibration.info(dataset).dataset_info
        assert di.total_records == 4 and di.raw_capture_count == 2
        assert sum(c.selected for c in di.cameras) == 2
        # Synthetic selected population isolates orchestration from expensive image generation.
        dataset = mantis.Artifact(seed['dataset'])
        left=client.calibration.solve_camera(dataset,'left',heldout_per_camera=3).wait(timeout=90)
        right=client.calibration.solve_camera(seed['dataset'],'right',heldout_per_camera=3).wait(timeout=90)
        ci=client.calibration.info(left).camera_info
        assert ci.camera.role == 'left' and ci.final_model.model == 'pinhole-brown5'
        assert ci.implementation.opencv_version and ci.implementation.mantis_version == '0.2.0'
        rig=client.calibration.solve_rig(dataset,left,right,heldout_pairs=3,rig_frame_id='org.mantis.x1.rig',rig_frame_name='Mantis X1 rig').wait(timeout=90)
        ri=client.calibration.info(rig).rig_info
        assert ri.left_camera.id == left.id and ri.right_camera.id == right.id and ri.dataset.id == dataset.id
        assert len(ri.t_rig_from_left.matrix) == 16 and len(ri.final_model.r_right_from_left) == 9
        assert len(ri.SerializeToString()) < 8192
        assert client.calibration.active('offline') is None
        client.calibration.activate('offline',rig)
        active=client.calibration.active('offline')
        assert active.artifact.id == rig.id and active.reference.revision == 1
        assert active.reference.id == client.calibration.info(rig).entry.reference.id
        client.calibration.clear('offline'); assert client.calibration.active('offline') is None
        # Checkerboard mono is public; stereo limitation is explicit, synchronous and structured.
        cl=client.calibration.solve_camera(seed['checker_dataset'],'left',heldout_per_camera=3).wait(timeout=90)
        cr=client.calibration.solve_camera(seed['checker_dataset'],'right',heldout_per_camera=3).wait(timeout=90)
        error(lambda: client.calibration.solve_rig(seed['checker_dataset'],cl,cr,heldout_pairs=3,rig_frame_id='rig',rig_frame_name='Rig'),3)
        error(lambda: client.calibration.info('nonexistent'),2)
        error(lambda: client.calibration.solve_camera(dataset,'missing',heldout_per_camera=3),1)
        error(lambda: client.calibration.build_dataset(checker,[checker],roles=['left'],max_selected_per_camera=3),3)
        # CLI target/info/current binding; jobs print an ID without an implicit wait.
        options=['--squares-x','8','--squares-y','6','--square-mm','40']
        ct=cli('target','create','checkerboard',*options,'--measured-width-mm','330.4','--measured-height-mm','243.6','--measurement-provenance')
        assert ct['calibration_info']['target_info']['target']['measurement']['provenance'] == {}
        assert cli('info',ct['result_id'])['calibration_info']['entry']['artifact']['id'] == ct['result_id']
        assert 'binding' not in cli('active','cli.offline')['active_calibration']
        cli('activate','cli.offline',rig.id)
        assert cli('active','cli.offline')['active_calibration']['binding']['artifact']['id'] == rig.id
        cli('clear','cli.offline'); assert 'binding' not in cli('active','cli.offline')['active_calibration']
        job=cli('dataset','build',checker.id,*seed['raw'],'--role','left','--role','right','--max-samples','3')['result_id']
        assert mantis.Job(client,job).wait(timeout=90).id
        job=cli('camera','solve',dataset.id,'left','--heldout','3')['result_id']; mantis.Job(client,job).wait(timeout=90)
        job=cli('rig','solve',dataset.id,left.id,right.id,'--heldout','3','--rig-frame-id','cli.rig','--rig-frame-name','CLI rig')['result_id']; mantis.Job(client,job).wait(timeout=90)
        subprocess.check_call([str(build / 'bin/mantis-calibration-client-tests'),seed['target'],*seed['raw']],env=env)
        events={e.kind for e in client.events()}
        assert {'calibration.target.created','calibration.dataset.created','calibration.camera.created','calibration.rig.created','calibration.activated','calibration.cleared'} <= events
        client.shutdown(); assert daemon.wait(timeout=10)==0
        print('Real daemon Python/C++/CLI calibration APIs and compositional camera/rig workflow passed')
    finally:
        if daemon.poll() is None: daemon.kill(); daemon.wait(timeout=5)
        log.close()
# Parser errors must be local, including without daemon credentials.
badenv=os.environ.copy(); badenv.pop('MANTIS_TOKEN',None)
base=['target','create','checkerboard','--squares-x','8','--squares-y','6','--square-mm','40']
malformed=[base+['--unknown','1'],base+['--squares-x','8'],base+['--note'],base+['--measured-width-mm','10'],
           base+['--revision','7'],base+['--square-mm','1oops'],base+['--instrument','scope'],
           ['camera','solve','dataset','left','--heldout','0'],['camera','solve','dataset','left','--heldout','2x'],
           ['camera','solve','dataset','left','--heldout','-1'],['dataset','build','target','raw','--role','left','--max-samples','0'],
           ['dataset','build','target','raw','raw','--role','left','--max-samples','3'],
           ['dataset','build','target','raw','--role','left','--role','left','--max-samples','3'],
           base+['--measured-width-mm','nan','--measured-height-mm','3']]
for args in malformed:
    result=subprocess.run([str(build/'bin/mantis-cli'),'calibration',*args],env=badenv,text=True,capture_output=True)
    assert result.returncode != 0 and 'Set MANTIS_TOKEN' not in result.stderr,(args,result.stderr)
print('Strict CLI malformed-option rejection passed without contacting a daemon')
