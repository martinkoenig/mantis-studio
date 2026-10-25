"""New terminal policy remains a caller-side, uninstalled Linux test component."""
from pathlib import Path
import ast
import re
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
module = root / 'plugins/first-party/devices/x1/f2-serial'
for path in module.glob('*'):
    code = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(), flags=re.S)
    assert not re.search(r'\b(?:open|openat|close|dup|ioctl|libusb|gpio|FdTransport|Host)\b', code), path
    assert 'tests/' not in code, path
for directory in ('src', 'sdk', 'plugins/first-party/devices/x1/f2',
                  'plugins/first-party/devices/x1/f2-posix'):
    for path in (root / directory).rglob('*'):
        if path.suffix in ('.cpp', '.hpp', '.h'):
            assert 'f2-serial/' not in path.read_text(), path
cmake = (root / 'tests/pty/x1-f2/CMakeLists.txt').read_text()
assert cmake.index('CMAKE_SYSTEM_NAME STREQUAL "Linux"') < cmake.index('add_library(mantis-x1-f2-serial-settings')
assert 'install(' not in cmake
for i, binary in enumerate(sys.argv[2:]):
    symbols = subprocess.check_output(['nm', '-C', binary], text=True)
    if i == 0:
        assert 'x1::f2::posix::configure_serial' in symbols
        assert not re.search(r'\b(?:open|openat|close|dup|libusb_\w+)\b', symbols)
    else:
        assert 'x1::f2::posix::configure_serial' not in symbols
        assert 'x1::f2::posix::SerialProfile' not in symbols
source = (root / 'tools/x1_uart_readiness.py').read_text()
tree = ast.parse(source)
for node in ast.walk(tree):
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        modules = [a.name for a in node.names] if isinstance(node, ast.Import) else [node.module]
        assert not any(m in ('subprocess', 'socket', 'serial', 'ctypes') for m in modules)
assert '/dev/tty' not in source and 'os.system' not in source
print('Borrowed-FD policy, Linux test linkage, product/portable binary boundaries and read-only inspector PASS')
