"""Owned tiny CMake producer used by the existing shadow collection entrypoint.

Only these repository-owned setup commands run. Imported CTest commands never do.
No product configuration, target, test binary or borrowed output is accessed.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


FIXTURE = Path(__file__).parent / 'fixtures' / 'shadow-configured'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def run(command, root):
    completed = subprocess.run(command, capture_output=True, timeout=30)
    logs = root / 'producer-commands.jsonl'
    with logs.open('a') as stream:
        stream.write(json.dumps({'argv': list(map(str, command)), 'exit': completed.returncode,
                                 'stdout': completed.stdout.decode(), 'stderr': completed.stderr.decode()}) + '\n')
    if completed.returncode:
        raise AssertionError('tiny fixture setup failed: ' + completed.stderr.decode())
    return completed.stdout


def produce(root, generator='Ninja'):
    source, build = root / 'source', root / 'build'
    shutil.copytree(FIXTURE, source)
    env = dict(os.environ, GIT_AUTHOR_NAME='Fixture', GIT_AUTHOR_EMAIL='fixture@example.invalid',
               GIT_COMMITTER_NAME='Fixture', GIT_COMMITTER_EMAIL='fixture@example.invalid',
               GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1')
    for args in (['init', '--template=', '-b', 'develop'], ['add', '.'], ['commit', '-m', 'tiny configured source']):
        p = subprocess.run(['git', '-C', str(source), '-c', 'core.hooksPath=' + os.devnull, *args],
                           env=env, capture_output=True, timeout=10)
        if p.returncode:
            raise AssertionError(p.stderr.decode())
    base = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], root).decode().strip()
    # Independent real committed diff for the full CLI composition.
    with (source / 'public.hpp').open('a') as stream:
        stream.write('// changed shared interface\n')
    for args in (['add', '.'], ['commit', '-m', 'shared contract change']):
        p = subprocess.run(['git', '-C', str(source), *args], env=env, capture_output=True, timeout=10)
        if p.returncode:
            raise AssertionError(p.stderr.decode())
    commit = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], root).decode().strip()
    tree = run(['git', '-C', str(source), 'rev-parse', 'HEAD^{tree}'], root).decode().strip()
    query = build / '.cmake/api/v1/query'
    query.mkdir(parents=True)
    for kind in ('codemodel-v2', 'cmakeFiles-v1', 'cache-v2'):
        (query / kind).touch()
    run(['cmake', '-S', str(source), '-B', str(build), '-G', generator,
         '-DCMAKE_BUILD_TYPE=Debug'], root)
    # Tiny executable artifacts are needed for CTest's real command expansion.
    # One compiler only; never run these test executables.
    run(['cmake', '--build', str(build), '--config', 'Debug', '--parallel', '1'], root)
    ctest = run(['ctest', '--test-dir', str(build), '-C', 'Debug', '--show-only=json-v1'], root)
    (build / 'ctest.json').write_bytes(ctest)
    indexes = list((build / '.cmake/api/v1/reply').glob('index-*.json'))
    if len(indexes) != 1:
        raise AssertionError('tiny producer did not create exactly one File API index')
    manifest = {'schema': 'ci110-configured-inputs-v1', 'source': {'commit': commit, 'tree': tree},
                'roots': {'source': str(source), 'build': str(build)}, 'configuration': 'Debug',
                'generator': generator, 'index': indexes[0].relative_to(build).as_posix(),
                'ctest': 'ctest.json', 'files': []}
    for path in sorted(source.iterdir()):
        if path.is_file():
            manifest['files'].append({'root': 'source', 'path': path.name, 'sha256': digest(path.read_bytes())})
    for path in sorted((build / '.cmake/api/v1/reply').glob('*.json')) + [build / 'ctest.json', build / 'CMakeCache.txt']:
        manifest['files'].append({'root': 'build', 'path': path.relative_to(build).as_posix(),
                                  'sha256': digest(path.read_bytes())})
    path = root / 'inputs.json'
    path.write_bytes(encode(manifest))
    return manifest, path, base


def repin(manifest, root, path, value):
    """Mutate only an owned test input; explicitly refresh its independent pin."""
    raw = encode(value) if not isinstance(value, bytes) else value
    (Path(manifest['roots'][root]) / path).write_bytes(raw)
    row = next(r for r in manifest['files'] if r['root'] == root and r['path'] == path)
    row['sha256'] = digest(raw)


def target_document(manifest, name):
    reply = Path(manifest['index']).parent
    for row in manifest['files']:
        if row['root'] == 'build' and row['path'].startswith(str(reply / ('target-' + name + '-'))):
            path = row['path']
            return path, json.loads((Path(manifest['roots']['build']) / path).read_bytes())
    raise AssertionError('missing owned target ' + name)
