"""External firmware is reachable only from opted-in conformance tests."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
module = root / 'tests/interop/x1-f2'
pin = '92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8'
for name in ('common.hpp', 'wire-fixtures.hpp', 'verify-firmware.cmake'):
    assert pin in (module / name).read_text(), name
for directory in ('plugins', 'src', 'sdk'):
    for path in (root / directory).rglob('*'):
        if path.suffix in ('.cpp', '.hpp', '.h'):
            assert not re.search(r'#include\s*[<"][^>"\n]*(?:mantis_x1|interop/x1-f2)', path.read_text()), path
cmake = (module / 'CMakeLists.txt').read_text()
assert 'option(MANTIS_F2_LIVE_INTEROP' in cmake and 'OFF)' in cmake
assert cmake.index('if(MANTIS_F2_LIVE_INTEROP)') < cmake.index('/shared/protocol/cpp/src/transport.cpp')
assert 'add_library' not in cmake and 'install(' not in cmake and 'add_subdirectory' not in cmake
assert 'add_dependencies(mantis-x1-f2-live-tests mantis-x1-f2-verify-firmware)' in cmake
main = (root / 'CMakeLists.txt').read_text()
assert main.index('if(BUILD_TESTING)') < main.index('add_subdirectory(tests/interop/x1-f2)')
assert not list(module.rglob('controller.cpp')) and not list(module.rglob('messages.cpp'))
assert not list(module.rglob('transport.cpp'))
for name in ('firmware-transport.cpp', 'firmware-transport.hpp'):
    s = (module / name).read_text()
    assert not re.search(r'\b(?:tcsetattr|tcgetattr|ioctl|libusb|termios|gpio)\b', s)
assert 'controller_.process_batch' in (module / 'firmware-transport.cpp').read_text()
assert 'peer_.feed_byte' in (module / 'firmware-transport.cpp').read_text()
assert 'fw::encode_frame' in (module / 'firmware-transport.cpp').read_text()
for index, binary in enumerate(sys.argv[2:]):
    symbols = subprocess.check_output(['nm', '-C', binary], text=True)
    assert not re.search(r'\b(?:tcsetattr|tcgetattr|cfsetispeed|cfsetospeed|ioctl|libusb_\w+)\b', symbols)
    assert 'x1::f2::simulation::Controller' not in symbols
    if index == 0:
        assert 'mantis_x1::' not in symbols
    else:
        assert 'mantis_x1::controller::v1::Controller::process_batch' in symbols
        assert 'mantis_x1::uart::v1::StreamParser::feed_byte' in symbols
print('F2 live opt-in/source pin, test-only firmware linkage, capture independence and binary boundaries PASS')
