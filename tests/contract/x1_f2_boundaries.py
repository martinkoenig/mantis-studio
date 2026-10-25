"""Private host dependency and isolated-backend source/binary checks."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
module = root / "plugins/first-party/devices/x1/f2"
standard = {
    "algorithm", "array", "bit", "chrono", "condition_variable", "cstdint", "functional",
    "limits", "mutex", "optional", "random", "span", "stdexcept", "string", "thread",
}
for path in module.iterdir():
    if path.suffix not in (".cpp", ".hpp"):
        continue
    source = path.read_text()
    for delimiter, include in re.findall(r'#include\s*([<"])([^>"\n]+)', source):
        if delimiter == '<':
            assert include in standard, (path, include)
        else:
            assert (module / include).resolve().parent == module, (path, include)
            assert (module / include).is_file(), (path, include)
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    code = re.sub(r'"(?:\\.|[^"\\])*"', '', code)
    assert not re.search(r'\b(?:Controller|Link|Injection|BenchConfig|PublicationTestHook)\b', code), path
    assert not re.search(r'\b(?:tcsetattr|tcgetattr|cfsetispeed|cfsetospeed|ioctl|gpio|libusb)\b', code), path

cmake = (root / 'CMakeLists.txt').read_text()
for target in ('codec', 'host'):
    marker = f'add_library(mantis-x1-f2-{target} STATIC '
    assert cmake.index(marker) < cmake.index('if(BUILD_TESTING)'), target
    assert 'tests/' not in cmake.split(marker, 1)[1].split(')', 1)[0], target
assert not (root / 'tests/fixtures/x1-f2/host.cpp').exists()
assert not (root / 'tests/fixtures/x1-f2/host.hpp').exists()
if len(sys.argv) > 2:
    symbols = subprocess.check_output(['nm', '-C', sys.argv[2]], text=True)
    assert not re.search(r'\b(?:tcsetattr|tcgetattr|cfsetispeed|cfsetospeed|ioctl|libusb_\w+)\b', symbols)
    assert 'x1::linux_backend()' not in symbols and 'LinuxBackend' not in symbols
print('F2 reusable codec/host standard-only boundary and isolated fixture binary passed')
