"""Independently compute the expected source identity before reading a real result."""

import hashlib
from pathlib import Path
import subprocess
import sys


# Deliberately independent of CMake's list, cache and compile definition. If an
# input disappears from the production fingerprint the output must not pass.
INPUTS = (
    'src/alg03/alg03.cpp', 'src/alg03/artifacts.cpp', 'src/alg03/documents.cpp',
    'src/alg03/numeric.cpp', 'src/alg03/private.hpp',
    'src/alg03/include/mantis/l1_ray_plane.hpp',
    'src/alg03/recorded.cpp', 'src/alg03/include/mantis/l1_recorded.hpp',
    'src/artifact-store/artifact-store.cpp',
    'src/artifact-store/include/mantis/artifact_store.hpp', 'src/calibration-artifacts/codecs.hpp',
    'src/calibration/include/mantis/calibration_solve.hpp', 'src/data/include/mantis/semantic_packet.hpp',
    'src/data/include/mantis/projected_light.hpp', 'src/memory/include/mantis/memory.hpp',
)


def source_identity(root):
    digest = hashlib.sha256(b'binary64-outward-enclosure-v1;no-fast-math;no-fp-contract\n')
    for name in INPUTS:
        file_hash = hashlib.sha256((root / name).read_bytes()).hexdigest()
        digest.update(f'{name}:{file_hash}\n'.encode('ascii'))
    return digest.hexdigest()


if __name__ == '__main__':
    expected = source_identity(Path(__file__).resolve().parents[2])
    if sys.argv[1:] == ['--print-digest']:
        print(expected)
    else:
        binary, = sys.argv[1:]
        sys.exit(subprocess.run([binary, '--result-build-identity', expected]).returncode)
