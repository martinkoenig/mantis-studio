"""Qualification controls are absent from product archives and executables."""
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
cmake = (root/'CMakeLists.txt').read_text()
assert 'if(BUILD_TESTING)\n add_subdirectory(tests/storage)\nendif()' in cmake
separate = (root/'tests/storage/CMakeLists.txt').read_text()
assert 'CMAKE_SYSTEM_NAME STREQUAL "Linux"' in separate
assert 'PRIVATE MANTIS_STORAGE_FAULT_TESTING' in separate
assert 'install(' not in separate
seam = (root/'src/artifact-store/persistence-test.hpp').read_text()
assert '#define MANTIS_STORAGE_POINT(point, root, id) ((void)0)' in seam
assert '#define MANTIS_STORAGE_COMMIT(point, root, id, db) ((void)0)' in seam
assert 'using CaptureOutput = std::ofstream;' in seam
for binary in sys.argv[2:-1]:
    symbols = subprocess.check_output(['nm', '-C', binary], text=True)
    assert 'artifact::storage_test::' not in symbols, binary
    strings = subprocess.check_output(['strings', binary], text=True)
    assert 'Storage checkpoint pipe deadline' not in strings, binary
symbols = subprocess.check_output(['nm', '-C', sys.argv[-1]], text=True)
assert 'artifact::storage_test::CommitGuard' in symbols
assert 'artifact::storage_test::Output' in symbols
assert '__wrap_fsync' in symbols
print('Storage seams compile out of product; isolated fault Store/daemon only PASS')
