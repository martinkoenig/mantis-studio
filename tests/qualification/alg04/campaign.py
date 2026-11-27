"""Run the bounded ALG04 CTest in fresh Linux processes and print its provenance."""

import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


def git(root, *args):
    result = subprocess.run(['git', '-C', root, *args], capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    exe, plugins, host, config, studio, san, compiler, version, cmake_cache = sys.argv[1:]
    root = Path(__file__).resolve().parents[3]
    with gzip.open(Path(__file__).with_name('calibration.json.gz'), 'rb') as f:
        calibration = f.read((512 << 10) + 1)
    assert 0 < len(calibration) <= 512 << 10, 'frozen calibration fixture exceeds its byte budget'
    arch = platform.machine()
    with open(exe, 'rb') as f:
        elf = f.read(20)
        assert elf[:6] == b'\x7fELF\x02\x01', 'expected native ELF64 little-endian test executable'
        machine = int.from_bytes(elf[18:20], 'little')
        f.seek(0)
        digest = hashlib.sha256()
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    head, tree = git(root, 'rev-parse', 'HEAD'), git(root, 'rev-parse', 'HEAD^{tree}')
    worktree_clean = git(root, 'status', '--porcelain') == ''
    event = {}
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
        runner_arch = os.environ['RUNNER_ARCH']
        assert (runner_arch, arch, machine) in {('X64', 'x86_64', 62), ('ARM64', 'aarch64', 183)}
        assert head == os.environ['GITHUB_SHA'] and tree and worktree_clean
    print('ALG04_CONTEXT ' + json.dumps({
        'arch': arch, 'elf_machine': machine, 'runner_arch': os.getenv('RUNNER_ARCH'),
        'runner_image': os.getenv('ImageOS'), 'runner_image_version': os.getenv('ImageVersion'),
        'run_id': os.getenv('GITHUB_RUN_ID'), 'run_attempt': os.getenv('GITHUB_RUN_ATTEMPT'),
        'checkout': head, 'tree': tree, 'worktree_clean': worktree_clean,
        'pr_head': event.get('pull_request', {}).get('head', {}).get('sha'),
        'config': config, 'studio': studio, 'sanitizers': san, 'compiler': f'{compiler} {version}',
        'exe_sha256': digest.hexdigest(), 'test_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'cmake_cache_sha256': hashlib.sha256(Path(cmake_cache).read_bytes()).hexdigest(),
        'calibration_fixture_sha256': hashlib.sha256(calibration).hexdigest(),
        'condition': 'serial; per-child new project on build filesystem; warm page cache, no cache dropping',
    }, sort_keys=True), flush=True)
    with tempfile.TemporaryDirectory(prefix='alg04-', dir=Path.cwd()) as scratch:
        snapshot = Path(scratch) / 'calibration.json'
        snapshot.write_bytes(calibration)
        for mode in ('baseline', 'fixture', 'campaign'):
            # No pip packages, detached workers, timing feedback, retries, or runner changes.
            print(f'ALG04_CHILD {mode}', flush=True)
            subprocess.run([exe, plugins, host, mode, snapshot], check=True, env={**os.environ, 'TMPDIR': scratch})


if __name__ == '__main__':
    main()
