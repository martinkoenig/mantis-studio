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
projected = ['projected_devices_list','projected_validate','projected_start','projected_status',
             'projected_captures_list','projected_stop','projected_bundle']
for number, name in enumerate(old + new + projected, 10):
    field = wire.Request.DESCRIPTOR.fields_by_name[name]
    assert field.number == number, (name, field.number, number)
    assert field.containing_oneof.name == 'command'
for number, name in enumerate(['protocol_version','request_id','error','devices','captures','artifacts',
                               'jobs','plugins','events','project_path','result_id','data',
                               'calibrations','calibration_info','active_calibration',
                               'projected_devices','projected_validation','projected_captures'], 1):
    assert wire.Response.DESCRIPTOR.fields_by_name[name].number == number
# Compact summaries have no repeated observation or per-point evidence message.
for cls in (wire.CalibrationDatasetInfo, wire.CalibrationCameraInfo, wire.CalibrationRigInfo,
            wire.MonoStageInfo, wire.StereoStageInfo):
    for f in cls.DESCRIPTOR.fields:
        assert f.name not in ('records','observations','views','pairs','residuals_px','image_points','object_points')
assert wire.Transform.DESCRIPTOR.fields_by_name['matrix'].number == 3
print('Protocol v1: Request 10..31 and Response 1..12 frozen; M6 Request 32..40 / Response 13..15 verified')

# Explicit projected enum mappings are protocol contracts, independent of C++ layouts.
enums = {
'ProjectedPresence': ['PROJECTED_UNKNOWN','PROJECTED_UNAVAILABLE','PROJECTED_ESTABLISHED'],
'ProjectedRunState': ['PROJECTED_VALIDATING','PROJECTED_READY','PROJECTED_RUNNING','PROJECTED_STOPPING','PROJECTED_COMPLETED','PROJECTED_CANCELLED','PROJECTED_FAILED'],
'ProjectedEmitterState': ['PROJECTED_OFF','PROJECTED_ON'],
'ProjectedCaptureMode': ['PROJECTED_CAPTURE_NONE','PROJECTED_FREE_RUNNING','PROJECTED_HARDWARE_TRIGGER'],
'ProjectedEvidenceRequirement': ['PROJECTED_COMMANDED_ONLY','PROJECTED_CONTROLLER_ACKNOWLEDGED','PROJECTED_EXPOSURE_EFFECTIVE'],
'ProjectedEvidenceScope': ['PROJECTED_CONTROLLER_REGISTER','PROJECTED_ELECTRICAL_ENABLE','PROJECTED_OPTICAL_EMISSION'],
'ProjectedEvidenceMethod': ['PROJECTED_SOFTWARE_DISPATCH','PROJECTED_CONTROLLER_REPORT','PROJECTED_REGISTER_READBACK','PROJECTED_ELECTRICAL_READBACK','PROJECTED_OPTICAL_SENSOR','PROJECTED_VALIDATED_EXECUTOR','PROJECTED_CAMERA_METADATA','PROJECTED_SOFTWARE_ASSOCIATION','PROJECTED_IMPORTED'],
'ProjectedStopMode': ['PROJECTED_NORMAL_STOP','PROJECTED_CANCEL'],
'ProjectedReason': ['PROJECTED_REASON_NONE','PROJECTED_TIMEOUT','PROJECTED_REJECTED','PROJECTED_DEVICE_FAILURE','PROJECTED_TRANSPORT_FAILURE','PROJECTED_EVIDENCE_MISSING','PROJECTED_CONTRADICTORY_EVIDENCE','PROJECTED_RESOURCE_LIMIT','PROJECTED_USER_STOP','PROJECTED_USER_CANCEL','PROJECTED_CLEANUP_FAILURE'],
'ProjectedParticipantKind': ['PROJECTED_PARENT','PROJECTED_IMAGE','PROJECTED_EMITTER','PROJECTED_CONTROLLER'],
'ProjectedTriggerMode': ['PROJECTED_TRIGGER_NONE','PROJECTED_TRIGGER_FREE_RUNNING','PROJECTED_HARDWARE_TRIGGER_MODE'],
'ProjectedStorageState': ['PROJECTED_STORAGE_OPEN','PROJECTED_STORAGE_FINALIZING','PROJECTED_STORAGE_FINALIZED','PROJECTED_STORAGE_RECOVERABLE'],
'ProjectedAcknowledgementStage': ['PROJECTED_ACCEPTANCE','PROJECTED_ACK_COMPLETION'],
'ProjectedAcknowledgementResult': ['PROJECTED_ACK_SUCCESS','PROJECTED_ACK_REJECTED','PROJECTED_ACK_FAILED'],
}
for name, names in enums.items():
    assert [(v.name, v.number) for v in wire.DESCRIPTOR.enum_types_by_name[name].values] == list(zip(names, range(len(names))))
for name, message in wire.DESCRIPTOR.message_types_by_name.items():
    if name.startswith('Projected'):
        for field in message.fields:
            assert field.name not in ('frameset','frames','pixels','samples','acquisition_evidence','trigger_events_history','bundles') or name == 'ProjectedEvidenceSummary' and field.name == 'bundles'
            assert field.type != field.TYPE_BYTES
            assert not field.message_type or not field.message_type.GetOptions().map_entry
assert not any(x in wire.DESCRIPTOR.message_types_by_name for x in ['AcquisitionBundle','AcquisitionEvidence','FrameSet','TriggerEvent','LaserObservation'])
print('L5 Request 41..47, Response 16..18 and projected enums frozen; bulk evidence excluded')

projected_fields = {
    'ProjectedHash': {'algorithm': 1, 'hex': 2},
    'ProjectedHashEvidence': {'presence': 1, 'value': 2},
    'ProjectedContentReference': {'id': 1, 'type': 2, 'schema_version': 3, 'hash': 4, 'revision': 5},
    'ProjectedContentEvidence': {'presence': 1, 'value': 2},
    'ProjectedProgramReference': {'id': 1, 'hash': 2, 'content': 3},
    'ProjectedBoolEvidence': {'presence': 1, 'value': 2},
    'ProjectedStringEvidence': {'presence': 1, 'value': 2},
    'ProjectedUIntEvidence': {'presence': 1, 'value': 2},
    'ProjectedCameraParticipant': {'component': 1, 'stream': 2, 'role': 3},
    'ProjectedParticipants': {'cameras': 1, 'emitters': 2, 'controllers': 3},
    'ProjectedEmitterIntent': {'emitter': 1, 'state': 2},
    'ProjectedTriggerIntent': {'controller': 1, 'request': 2, 'endpoints': 3},
    'ProjectedCaptureIntent': {'mode': 1, 'cameras': 2, 'trigger': 3},
    'ProjectedAcquisitionStep': {'index': 1, 'label': 2, 'emitters': 3, 'capture': 4, 'evidence_requirement': 5, 'required_scope': 6, 'settle_ns': 7, 'max_duration_ns': 8},
    'ProjectedRunBounds': {'max_duration_ns': 1, 'max_on_duration_ns': 2, 'max_step_instances': 3, 'max_commands': 4, 'max_events': 5, 'max_bytes': 6, 'max_in_flight_captures': 7},
    'ProjectedAcquisitionProgram': {'type': 1, 'schema_version': 2, 'identity': 3, 'participants': 4, 'steps': 5, 'repetitions': 6, 'bounds': 7},
    'ProjectedProgramSource': {'inline_program': 1, 'raw_capture_artifact_id': 2},
    'ProjectedRuntimeConfig': {'queue_capacity': 1, 'operation_timeout_ms': 2, 'abort_timeout_ms': 3, 'cleanup_timeout_ms': 4, 'publication_timeout_ms': 5, 'max_correlation_entries': 6},
    'ProjectedCaptureRequest': {'plugin_id': 1, 'parent_id': 2, 'program': 3, 'config': 4},
    'ProjectedStopRequest': {'capture_id': 1, 'expected_run_id': 2, 'expected_generation_id': 3, 'mode': 4},
    'ProjectedImageSource': {'stream_id': 1, 'physical_identity': 2, 'role': 3, 'width': 4, 'height': 5},
    'ProjectedComponent': {'id': 1, 'parent_id': 2, 'name': 3, 'role': 4, 'kind': 5, 'capabilities': 6, 'controls': 7, 'participants': 8, 'trigger_endpoints': 9, 'image_source': 10, 'emitter_states': 11, 'capture_modes': 12, 'trigger_modes': 13, 'evidence_methods': 14, 'evidence_scopes': 15, 'pattern': 16, 'pattern_revision': 17},
    'ProjectedLimits': {'max_components': 1, 'max_steps': 2, 'max_bundle_members': 3, 'max_cameras': 4, 'run': 5, 'max_step_duration_ns': 6, 'max_pending_bundles': 7, 'max_call_timeout_ms': 8, 'watchdog': 9, 'interlock': 10, 'fail_off': 11},
    'ProjectedDevice': {'plugin_id': 1, 'parent_id': 2, 'components': 3, 'limits': 4, 'frameset_stream': 5},
    'ProjectedContractError': {'category': 1, 'code': 2},
    'ProjectedExecutorValidation': {'accepted': 1, 'error': 2, 'diagnostic': 3},
    'ProjectedValidation': {'accepted': 1, 'host_error': 2, 'executor_error': 3, 'close_error': 4, 'executor_validation': 5, 'limits': 6},
    'ProjectedStepInstance': {'run_id': 1, 'repetition_index': 2, 'step_index': 3},
    'ProjectedQueue': {'produced': 1, 'consumed': 2, 'occupancy': 3, 'capacity': 4, 'high_water': 5, 'saturation_failures': 6},
    'ProjectedEvidenceSummary': {'bundles': 1, 'trigger_events': 2, 'evidence_only': 3, 'captured': 4, 'unresolved_request_records': 5, 'loss_records': 6, 'commanded_established': 7, 'commanded_unknown': 8, 'commanded_unavailable': 9, 'acknowledgement_success': 10, 'acknowledgement_rejected': 11, 'acknowledgement_failed': 12, 'acknowledgement_unknown': 13, 'acknowledgement_unavailable': 14, 'effective_established': 15, 'effective_unknown': 16, 'effective_unavailable': 17, 'late_evidence': 18},
    'ProjectedEmitterSummary': {'emitter_id': 1, 'commanded_presence': 2, 'commanded_state': 3, 'acknowledgement_presence': 4, 'acknowledgement_stage': 5, 'acknowledgement_result': 6, 'observed_presence': 7, 'observed_state': 8, 'observed_scope': 9, 'effective_established': 10, 'effective_unknown': 11, 'effective_unavailable': 12},
    'ProjectedAbortSummary': {'run': 1, 'fenced_generation': 2, 'inhibited': 3, 'stale_work_fenced': 4, 'off_requested': 5, 'emitters': 6, 'executor_error': 7},
    'ProjectedCapture': {'id': 1, 'plugin_id': 2, 'parent_id': 3, 'run_id': 4, 'generation_id': 5, 'raw_artifact_id': 6, 'program': 7, 'state': 8, 'cleanup_resolved': 9, 'active': 10, 'storage_state': 11, 'last_evidence_step': 12, 'latest_bundle_sequence': 13, 'queue': 14, 'committed_bundles': 15, 'raw_capture_bytes': 16, 'finalization_job_id': 17, 'recording_error': 18, 'reason': 19, 'initiating_error': 20, 'abort_error': 21, 'stop_error': 22, 'close_error': 23, 'abort_outcome': 24, 'evidence': 25},
}
for name, expected in projected_fields.items():
    assert {f.name: f.number for f in wire.DESCRIPTOR.message_types_by_name[name].fields} == expected
