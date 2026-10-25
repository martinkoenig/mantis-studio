"""High-level Python encoding/presence, artifact arguments and daemon error propagation."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]) / 'python'))
import mantis
from mantis import wire
class Recorder:
    def __init__(self): self.command = None
    def _call(self, **command):
        self.command = command
        return wire.Response(result_id='created', active_calibration=wire.ActiveCalibration())
r = Recorder(); api = mantis.Calibration(r)
t = api.create_checkerboard_target(squares_x=8, squares_y=6, square_mm=40)
assert isinstance(t, mantis.Artifact) and t.id == 'created'
assert r.command['calibration_target_create'].target.WhichOneof('pattern') == 'checkerboard'
assert not r.command['calibration_target_create'].target.measurement.HasField('provenance')
api.create_charuco_target(squares_x=8,squares_y=6,square_mm=40,marker_mm=25,
                         dictionary='DICT_6X6_250',layout='white_square_at_origin_even_rows')
assert r.command['calibration_target_create'].target.charuco.pattern_layout == 'white_square_at_origin_even_rows'
for provenance in (None, mantis.MeasurementProvenance(), mantis.MeasurementProvenance(0, .1, '', 'measured')):
    m = mantis.PhysicalMeasurement(330.4, 243.6, provenance)
    api.create_checkerboard_target(squares_x=8,squares_y=6,square_mm=40,measurement=m,series_id='series')
    encoded = r.command['calibration_target_create']
    assert encoded.series_id == 'series'
    assert encoded.target.measurement.active_width_mm == 330.4
    assert encoded.target.measurement.HasField('provenance') == (provenance is not None)
    if provenance and provenance.instrument is not None:
        assert encoded.target.measurement.provenance.HasField('instrument')
        assert encoded.target.measurement.provenance.HasField('width_uncertainty_mm')
entry = wire.CalibrationEntry(artifact=wire.Artifact(id='artifact'))
j = api.build_dataset(entry, [mantis.Artifact('raw'), 'raw2'], roles=['left','right'], max_selected_per_camera=40)
assert isinstance(j, mantis.Job) and j.id == 'created'
assert list(r.command['calibration_dataset_build'].raw_capture_artifact_ids) == ['raw','raw2']
assert r.command['calibration_dataset_build'].target_artifact_id == 'artifact'
assert isinstance(api.solve_camera(mantis.Artifact('dataset'),'left',heldout_per_camera=5), mantis.Job)
assert isinstance(api.solve_rig('dataset',entry,'right',heldout_pairs=5,rig_frame_id='rig',rig_frame_name='Rig'),mantis.Job)
assert r.command['calibration_rig_solve'].left_camera_artifact_id == 'artifact'
assert api.active('offline') is None
class Active:
    def _call(self, **command):
        return wire.Response(active_calibration=wire.ActiveCalibration(binding=wire.ActiveCalibrationBinding(logical_device_id='offline')))
assert mantis.Calibration(Active()).active('offline').logical_device_id == 'offline'
class Error:
    def _call(self, **command): raise mantis.MantisError('not found', 2, 'calibration')
try: mantis.Calibration(Error()).info('missing')
except mantis.MantisError as e: assert e.code == 2 and e.component == 'calibration'
else: raise AssertionError('Structured error swallowed')
for raws, roles in [(['raw','raw'], ['left']), (['raw'], ['left','left']), ((str(i) for i in range(1025)),['left']), (['raw'],[str(i) for i in range(65)])]:
    try: api.build_dataset('target',raws,roles=roles,max_selected_per_camera=40)
    except ValueError: pass
    else: raise AssertionError('Invalid collection accepted')
print('Python calibration encoding, presence, bounds, artifact arguments, Job and active semantics passed')
