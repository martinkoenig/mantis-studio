"""Linux FD foundation exists only in uninstalled test targets; portable core stays frozen."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
module = root / 'plugins/first-party/devices/x1/f2-posix'
for path in module.glob('*'):
    source = path.read_text()
    code = re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S)
    assert not re.search(r'\b(?:open|openat|tcsetattr|tcgetattr|ioctl|libusb|gpio|termios)\b', code), path
    assert 'tests/' not in source and 'Controller' not in code and 'Link' not in code, path
for directory in ('src', 'sdk', 'plugins/first-party/devices/x1/f2'):
    for path in (root / directory).rglob('*'):
        if path.suffix in ('.cpp', '.hpp', '.h'):
            assert 'f2-posix/' not in path.read_text(), path
cmake = (root / 'tests/pty/x1-f2/CMakeLists.txt').read_text()
assert 'CMAKE_SYSTEM_NAME STREQUAL "Linux"' in cmake
assert 'install(' not in cmake and 'add_subdirectory' not in cmake
main = (root / 'CMakeLists.txt').read_text()
assert main.index('if(BUILD_TESTING)') < main.index('add_subdirectory(tests/pty/x1-f2)')
for i, binary in enumerate(sys.argv[2:]):
    symbols = subprocess.check_output(['nm', '-C', binary], text=True)
    if i == 0:
        assert 'x1::f2::posix::FdTransport::run' in symbols
        assert not re.search(r'\b(?:open|openat|tcsetattr|tcgetattr|ioctl|libusb_\w+)\b', symbols)
    else:
        assert 'x1::f2::posix::FdTransport' not in symbols
print('Linux FD module/test linkage, no opener/terminal policy, portable core and product binary boundaries PASS')
