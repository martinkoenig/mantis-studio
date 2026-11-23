"""Run with -I in an installed venv, never from a source package path."""
import hashlib
import json
from pathlib import Path
import socket
import sys


def no_network(*args, **kwargs):
    raise AssertionError("artifact import/serialization attempted network access")


socket.create_connection = no_network
socket.socket.connect = no_network
import mantis
import mantis.calibration
from mantis import mantis_pb2 as wire
from google.protobuf import descriptor_pb2

prefix = Path(sys.prefix).resolve()
for module in (mantis, mantis.calibration, wire):
    path = Path(module.__file__).resolve()
    assert path.is_relative_to(prefix), (path, prefix)
    assert 'site-packages' in path.parts, path
assert callable(mantis.Client) and callable(mantis.connect)
assert mantis.Calibration is mantis.calibration.Calibration
# Independent golden wire bytes: version=1, correlation='r', snapshot tag=10.
request = wire.Request(protocol_version=1, request_id='r', snapshot=wire.Empty())
assert request.SerializeToString(deterministic=True) == bytes.fromhex('08011201725200')
assert wire.Request.FromString(bytes.fromhex('08011201725200')) == request
response = wire.Response(protocol_version=1, request_id='r', result_id='job')
assert response.SerializeToString(deterministic=True) == bytes.fromhex('08011201725a036a6f62')
assert wire.Response.FromString(bytes.fromhex('08011201725a036a6f62')) == response
assert wire.Request.DESCRIPTOR.fields_by_name['x1_role_apply'].number == 82
measurement = wire.PhysicalMeasurement(active_width_mm=0)
assert measurement.HasField('active_width_mm')
assert not wire.PhysicalMeasurement().HasField('active_width_mm')
if len(sys.argv) > 1:
    expected = descriptor_pb2.FileDescriptorSet.FromString(Path(sys.argv[1]).read_bytes()).file[0]
    actual = descriptor_pb2.FileDescriptorProto.FromString(wire.DESCRIPTOR.serialized_pb)
    # protoc's descriptor set includes json_name; generated descriptors omit it.
    def normalize(message):
        for field in message.field:
            field.ClearField('json_name')
        for nested in message.nested_type:
            normalize(nested)
    for file in (expected, actual):
        file.ClearField('source_code_info')
        for message in file.message_type:
            normalize(message)
    assert actual == expected, 'installed descriptor differs from canonical schema'
print(json.dumps({'python': sys.version.split()[0], 'module': str(Path(mantis.__file__).resolve()),
                  'descriptor_sha256': hashlib.sha256(wire.DESCRIPTOR.serialized_pb).hexdigest()}))
