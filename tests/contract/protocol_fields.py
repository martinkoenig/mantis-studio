"""Generated descriptor compatibility check for protocol v1 (old and M6 tags)."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]) / 'python'))
from mantis import wire
old = ['snapshot','devices_list','capture_start','capture_stop','pipeline_run','project_open',
       'artifacts_list','export_artifact','job_cancel','plugins_list','events','artifact_data',
       'artifact_recover','plugin_enable','shutdown','capture_status','captures_list','devices_info',
       'preview','preview_release','replay','artifact_recover_async']
new = ['calibration_target_create','calibration_dataset_build','calibration_camera_solve',
       'calibration_rig_solve','calibrations_list','calibration_info','calibration_active',
       'calibration_activate','calibration_clear']
for number, name in enumerate(old + new, 10):
    field = wire.Request.DESCRIPTOR.fields_by_name[name]
    assert field.number == number, (name, field.number, number)
    assert field.containing_oneof.name == 'command'
for number, name in enumerate(['protocol_version','request_id','error','devices','captures','artifacts',
                               'jobs','plugins','events','project_path','result_id','data',
                               'calibrations','calibration_info','active_calibration'], 1):
    assert wire.Response.DESCRIPTOR.fields_by_name[name].number == number
# Compact summaries have no repeated observation or per-point evidence message.
for cls in (wire.CalibrationDatasetInfo, wire.CalibrationCameraInfo, wire.CalibrationRigInfo,
            wire.MonoStageInfo, wire.StereoStageInfo):
    for f in cls.DESCRIPTOR.fields:
        assert f.name not in ('records','observations','views','pairs','residuals_px','image_points','object_points')
assert wire.Transform.DESCRIPTOR.fields_by_name['matrix'].number == 3
print('Protocol v1: Request 10..31 and Response 1..12 frozen; M6 Request 32..40 / Response 13..15 verified')
