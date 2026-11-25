"""Linux-only, isolated CMake/Ninja regression. No product inputs or shared cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import traceback

from shadow_configured_fixture import run  # bounded output, owned process group + reap

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'tools'))  # also used by the shared command runner
from ci_ccache import CONFIG

FIXTURE = HERE / 'fixtures' / 'incremental'
OBJECT = 'CMakeFiles/probe.dir/main.cpp.o'
EXPECTED = {'cold': b'7 11 3\n', 'noop': b'7 11 3\n', 'warm': b'7 11 3\n',
            'generated': b'7 29 3\n', 'header': b'17 29 3\n', 'option': b'17 29 5\n'}


class PredicateError(AssertionError):
    """Only fixed, public-safe predicate names belong here."""


class StaleGenerated(PredicateError):
    pass


def require(condition, reason):
    if not condition:
        raise PredicateError(reason)


def check_value(stage, output):
    if stage == 'generated' and output == EXPECTED['cold']:
        raise StaleGenerated('generated: expected 7 29 3, got stale 7 11 3')
    require(output == EXPECTED[stage], stage + ': unexpected binary output')


def tool(name):
    path = shutil.which(name)
    require(path is not None, 'missing executable: ' + name)
    return os.path.abspath(path)


def counters(raw):
    stats = dict(row.split() for row in raw.decode('ascii').splitlines())
    return (int(stats['cache_miss']), int(stats['direct_cache_hit']) + int(stats['preprocessed_cache_hit']))


def changed(path, old, new):
    value = path.read_bytes()
    require(value.count(old) == 1, 'mutation site differs')
    path.write_bytes(value.replace(old, new))


def build_case(root, broken):
    root.mkdir(mode=0o700)
    source, build = root / 'source', root / 'build'
    shutil.copytree(FIXTURE, source)
    rule = source / 'CMakeLists.txt'
    edge = b'    DEPENDS "${input}"\n'
    if broken:
        changed(rule, edge, b'')
    else:
        original = (FIXTURE / rule.name).read_bytes()
        mutant = (root.parent / 'broken' / 'source' / rule.name).read_bytes()
        require(rule.read_bytes() == original and mutant == original.replace(edge, b'') and mutant != original,
                'paired CMake rules must differ by exactly the missing edge')
    cmake, ninja, cache, compiler = map(tool, ('cmake', 'ninja', 'ccache', 'c++'))
    env = {'PATH': os.environ['PATH'], 'LC_ALL': 'C', 'HOME': str(root),
           'TMPDIR': str(root / 'tmp'), 'CCACHE_DIR': str(root / 'cache'),
           'CCACHE_CONFIGPATH': str(root / 'ccache.conf'), 'CMAKE_BUILD_PARALLEL_LEVEL': '1'}
    (root / 'tmp').mkdir()
    settings = CONFIG | {'max_size': '8Mi', 'namespace': 'incremental-probe'}
    (root / 'ccache.conf').write_text(''.join(f'{k} = {v}\n' for k, v in settings.items()))

    def cmd(*args):
        return run(list(map(str, args)), root, env=env)

    def configure(mode):
        cmd(cmake, '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug',
            '-DCMAKE_MAKE_PROGRAM=' + ninja, '-DCMAKE_CXX_COMPILER=' + compiler,
            '-DCMAKE_CXX_COMPILER_LAUNCHER=' + cache, '-DPROBE_MODE=' + str(mode))

    def step(name, misses, hits):
        before = counters(cmd(cache, '--print-stats'))
        log = build / '.ninja_log'
        previous = log.read_bytes() if log.exists() else b''
        output = cmd(ninja, '-C', build, '-j1', '-v', 'probe')
        after = counters(cmd(cache, '--print-stats'))
        delta = tuple(b - a for a, b in zip(before, after))
        require(delta == (misses, hits), name + ': wrong cache miss/hit delta')
        if (misses, hits) == (0, 0):
            require(output.splitlines()[-1:] == [b'ninja: no work to do.'] and
                    log.read_bytes() == previous, name + ': expected no Ninja commands')
        else:
            require(any(os.fsencode(cache) in line and b' -c ' in line and os.fsencode(OBJECT) in line
                        for line in output.splitlines()), name + ': missing launched compile')
        shutil.copy2(build / 'probe', root / name)
        print(f'{root.name}/{name}: misses={delta[0]} hits={delta[1]}', flush=True)

    configure(3)
    step('cold', 1, 0)
    lines = cmd(ninja, '-C', build, '-t', 'deps', OBJECT).decode().splitlines()
    deps = {(build / line.strip()).resolve() for line in lines if line.startswith('    ')}
    require({source / 'leaf.hpp', source / 'public.hpp', build / 'generated.hpp'} <= deps,
            'missing compiler-discovered header dependencies')
    if not broken:
        step('noop', 0, 0)
        (build / OBJECT).rename(root / 'cold.o')  # force a real compile, keep cache AND evidence
        step('warm', 0, 1)
    changed(source / 'generated.in', b'= 11;', b'= 29;')
    step('generated', 0 if broken else 1, 0)
    if not broken:
        changed(source / 'leaf.hpp', b'= 7;', b'= 17;')
        step('header', 1, 0)
        configure(5)
        step('option', 1, 0)


def stages(case):
    return ('cold', 'generated') if case == 'broken' else tuple(EXPECTED)


def manifest(root):
    return {f'{case}/{stage}': hashlib.sha256((root / case / stage).read_bytes()).hexdigest()
            for case in ('broken', 'restored') for stage in stages(case)}


def produce(root):
    require(not any(k.startswith('CCACHE_') for k in os.environ), 'unexpected ccache override')
    build_case(root / 'broken', True)
    build_case(root / 'restored', False)
    (root / 'ready.json').write_text(json.dumps(manifest(root), sort_keys=True))


def consume(root, case):
    require(manifest(root) == json.loads((root / 'ready.json').read_text()), 'staged snapshot differs')
    for stage in stages(case):
        output = run([str(root / case / stage)], root / case, env={'LC_ALL': 'C'})
        check_value(stage, output)
        print(case + '/' + stage + ': expected binary output', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('build', 'test-broken', 'test-restored'))
    parser.add_argument('root', type=Path, help='trusted private campaign outside the checkout')
    args = parser.parse_args()
    root = args.root.absolute()
    owns_root = False
    try:
        require(root == root.resolve() and not root.is_relative_to(REPO) and not REPO.is_relative_to(root),
                'campaign must be disjoint from checkout with no symlink components')
        if args.phase == 'build':
            root.mkdir(mode=0o700)  # fail on any existing entry; never remove/overwrite another run
            owns_root = True
            produce(root)
        else:
            require(root.is_dir() and root.stat().st_uid == os.getuid() and not root.stat().st_mode & 0o077,
                    'expected owned private campaign')
            owns_root = True
            consume(root, args.phase.removeprefix('test-'))
    except (Exception, KeyboardInterrupt) as error:
        # Failure to write diagnostics cannot erase the primary or qualify an expected RED.
        reason = str(error) if isinstance(error, PredicateError) else type(error).__name__
        print(f'{args.phase}: {reason}', file=sys.stderr)
        for note in getattr(error, '__notes__', ()):
            print('additional command failure (private receipt)', file=sys.stderr)
        if owns_root:
            try:
                with (root / 'failures.log').open('a') as stream:
                    traceback.print_exc(file=stream)
            except OSError as secondary:
                print('diagnostic write failed: ' + type(secondary).__name__, file=sys.stderr)
                return 1
        if args.phase == 'test-broken' and isinstance(error, StaleGenerated):
            return 3
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
