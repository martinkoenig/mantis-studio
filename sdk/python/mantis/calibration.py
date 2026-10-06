"""Typed calibration control API. Full immutable evidence remains in artifacts."""
from dataclasses import dataclass
from itertools import islice
from typing import Optional, Union
from . import mantis_pb2 as wire
from . import Artifact, Job

STRING_LIMIT = 4096
SOURCE_LIMIT = 1024
ROLE_LIMIT = 64

@dataclass(frozen=True)
class MeasurementProvenance:
    width_uncertainty_mm: Optional[float] = None
    height_uncertainty_mm: Optional[float] = None
    instrument: Optional[str] = None
    note: Optional[str] = None

@dataclass(frozen=True)
class PhysicalMeasurement:
    active_width_mm: Optional[float] = None
    active_height_mm: Optional[float] = None
    provenance: Optional[MeasurementProvenance] = None

@dataclass(frozen=True)
class CheckerboardPattern:
    pass

@dataclass(frozen=True)
class CharucoPattern:
    dictionary: str
    nominal_marker_size_mm: float
    pattern_layout: str

@dataclass(frozen=True)
class TargetSpecification:
    squares_x: int
    squares_y: int
    nominal_square_size_mm: float
    pattern: Union[CheckerboardPattern, CharucoPattern]
    measurement: Optional[PhysicalMeasurement] = None

def _text(value, name, required=True):
    if not isinstance(value, str) or (required and not value) or len(value.encode('utf-8')) > STRING_LIMIT or '\0' in value:
        raise ValueError(f'{name}: expected text of at most 4096 UTF-8 bytes, without NUL')
    return value

def _id(value):
    if isinstance(value, str): return _text(value, 'id')
    if hasattr(value, 'artifact'): return _id(value.artifact)
    return _text(value.id, 'id')

def _collection(values, limit, convert):
    result = [convert(v) for v in islice(iter(values), limit + 1)]
    if len(result) > limit: raise ValueError(f'Calibration collection exceeds {limit} entries')
    if len(set(result)) != len(result): raise ValueError('Duplicate calibration inputs')
    return result

class Calibration:
    def __init__(self, client): self.client = client
    def create_target(self, target: TargetSpecification, *, series_id=None):
        spec = wire.CalibrationTargetSpecification(squares_x=target.squares_x, squares_y=target.squares_y,
                                                   nominal_square_size_mm=target.nominal_square_size_mm)
        if isinstance(target.pattern, CheckerboardPattern): spec.checkerboard.SetInParent()
        elif isinstance(target.pattern, CharucoPattern):
            pattern = target.pattern
            if pattern.pattern_layout not in ('black_square_at_origin', 'white_square_at_origin_even_rows'):
                raise ValueError('Unknown ChArUco physical pattern_layout')
            spec.charuco.CopyFrom(wire.CharucoPattern(dictionary=_text(pattern.dictionary, 'dictionary'),
                nominal_marker_size_mm=pattern.nominal_marker_size_mm, pattern_layout=pattern.pattern_layout))
        else: raise TypeError('Expected CheckerboardPattern or CharucoPattern')
        if target.measurement is not None:
            m = target.measurement
            spec.measurement.SetInParent()
            for field in ('active_width_mm', 'active_height_mm'):
                value = getattr(m, field)
                if value is not None: setattr(spec.measurement, field, value)
            if m.provenance is not None:
                spec.measurement.provenance.SetInParent()  # Keep present empty distinct from absence.
                for field in ('width_uncertainty_mm', 'height_uncertainty_mm', 'instrument', 'note'):
                    value = getattr(m.provenance, field)
                    if value is not None:
                        if isinstance(value, str): _text(value, field, False)
                        setattr(spec.measurement.provenance, field, value)
        result = self.client._call(calibration_target_create=wire.CalibrationTargetCreate(
            target=spec, series_id=_text(series_id or '', 'series_id', False)))
        return Artifact(result.result_id)
    def create_checkerboard_target(self, *, squares_x, squares_y, square_mm, measurement=None, series_id=None):
        return self.create_target(TargetSpecification(squares_x, squares_y, square_mm, CheckerboardPattern(), measurement), series_id=series_id)
    def create_charuco_target(self, *, squares_x, squares_y, square_mm, marker_mm, dictionary, layout,
                             measurement=None, series_id=None):
        return self.create_target(TargetSpecification(squares_x, squares_y, square_mm,
            CharucoPattern(dictionary, marker_mm, layout), measurement), series_id=series_id)
    def build_dataset(self, target, raw_captures, *, roles, max_selected_per_camera, series_id=None):
        result = self.client._call(calibration_dataset_build=wire.CalibrationDatasetBuild(
            target_artifact_id=_id(target), raw_capture_artifact_ids=_collection(raw_captures, SOURCE_LIMIT, _id),
            camera_roles=_collection(roles, ROLE_LIMIT, lambda v: _text(v, 'role')),
            max_selected_per_camera=max_selected_per_camera, series_id=_text(series_id or '', 'series_id', False)))
        return Job(self.client, result.result_id)
    def solve_camera(self, dataset, role, *, heldout_per_camera, series_id=None):
        result = self.client._call(calibration_camera_solve=wire.CalibrationCameraSolve(
            dataset_artifact_id=_id(dataset), camera_role=_text(role, 'role'), heldout_per_camera=heldout_per_camera,
            series_id=_text(series_id or '', 'series_id', False)))
        return Job(self.client, result.result_id)
    def solve_rig(self, dataset, left, right, *, heldout_pairs, rig_frame_id, rig_frame_name, series_id=None):
        result = self.client._call(calibration_rig_solve=wire.CalibrationRigSolve(
            dataset_artifact_id=_id(dataset), left_camera_artifact_id=_id(left), right_camera_artifact_id=_id(right),
            heldout_pairs=heldout_pairs, rig_frame_id=_text(rig_frame_id, 'rig_frame_id'),
            rig_frame_name=_text(rig_frame_name, 'rig_frame_name'), series_id=_text(series_id or '', 'series_id', False)))
        return Job(self.client, result.result_id)
    def list(self): return list(self.client._call(calibrations_list=wire.Empty()).calibrations)
    def info(self, artifact): return self.client._call(calibration_info=wire.Id(id=_id(artifact))).calibration_info
    def active(self, device):
        active = self.client._call(calibration_active=wire.Id(id=_id(device))).active_calibration
        return active.binding if active.HasField('binding') else None
    def activate(self, device, rig):
        self.client._call(calibration_activate=wire.CalibrationActivate(logical_device_id=_id(device), rig_artifact_id=_id(rig)))
    def clear(self, device): self.client._call(calibration_clear=wire.Id(id=_id(device)))
