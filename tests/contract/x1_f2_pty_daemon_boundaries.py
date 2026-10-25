"""PTY daemon fixture composition stays Linux-only/test-only; product binaries are unchanged."""
from pathlib import Path
import re
import subprocess
import sys
root = Path(sys.argv[1]).resolve()
module = root/'tests/fixtures/x1-f2-pty'
for path in module.glob('*'):
    code = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(), flags=re.S)
    assert not re.search(r'\b(?:open|openat|ioctl|gpio|termios|tcsetattr|tcgetattr|dup)\s*\(', code), path
    assert '/dev/tty' not in code and 'getenv' not in code, path
    assert not re.search(r'\bController\b', code), path # Only accepted Link owns MCU semantics.
source = (root/'tests/fixtures/x1-f2/plugin.cpp').read_text()
assert 'MANTIS_X1_F2_PTY_FIXTURE' in source and 'using FixtureLink = sim::pty::Backend;' in source
assert 'std::string_view(pty) != "1"' in source
cmake = (root/'tests/pty/x1-f2/CMakeLists.txt').read_text()
assert cmake.index('CMAKE_SYSTEM_NAME STREQUAL "Linux"') < cmake.index('add_library(mantis-x1-f2-pty-backend')
assert 'install(' not in cmake
assert 'LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/x1-f2-pty-plugins' in cmake
# The only allocator opens its own freshly allocated slave, with no caller path.
allocator = (root/'tests/pty/x1-f2/common.hpp').read_text()
assert '::posix_openpt(' in allocator and '::ptsname_r(master, name.data(), name.size())' in allocator
assert '::open(name.data(),' in allocator and 'getenv' not in allocator
for i, binary in enumerate(sys.argv[2:]):
    symbols = subprocess.check_output(['nm','-C',binary],text=True)
    if i == 0:
        for name in ('posix::FdTransport::run', 'posix::configure_serial', 'simulation::pty::Backend::Peer::run'):
            assert name in symbols, name
        assert 'x1::linux_backend' not in symbols and not re.search(r'\b(?:ioctl|libusb_\w+)\b',symbols)
    else:
        for name in ('posix::FdTransport', 'posix::configure_serial', 'simulation::pty::Backend'):
            assert name not in symbols, (binary,name)
print('Dedicated internal PTY allocation, isolated DSO, no device selector/product linkage PASS')
