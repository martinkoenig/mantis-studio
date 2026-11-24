"""Disposable omitted-seal observation mutation, split BUILD and exclusive TEST.

Only observation emission is removed; actual Store flush/sync/index remains.
"""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import sys
from decoder import decode, load, Incomplete

phase, source, build, root = sys.argv[1], *(Path(p).resolve() for p in sys.argv[2:5])
actual = source/'src/device-runtime/recorder-diagnostics.cpp'

def invoke(command, log, timeout):
    with log.open('xb') as output:
        result = subprocess.run(command, cwd=build, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    return result.returncode

if phase == 'build':
    root.mkdir()
    before = actual.read_bytes()
    content = before.replace(b'    if (!current)\n        return;\n    auto &l = current->lanes[lane];',
        b'    if (!current || std::strcmp(stage, "store.segment.durable") == 0)\n        return;\n    auto &l = current->lanes[lane];')
    assert content != before
    needle = b'void raw_stage(size_t index, bool end) noexcept {'
    assert content.count(needle) == 1
    content = content.replace(needle, needle + b'\n    if (index == 5) return; // omitted whole seal observation group')
    cpp, obj, binary = root/'observer.cpp', root/'observer.o', root/'fixture'
    cpp.write_bytes(content)
    entries = json.loads((build/'compile_commands.json').read_bytes())
    entry = next(e for e in entries if e['file'] == str(actual) and 'mantis-recorder-observer.dir' in e['command'])
    tokens = shlex.split(entry['command'])
    args = []; skip = False
    for token in tokens:
        if skip: skip = False; continue
        if token in ('-MF', '-MT'): skip = True; continue
        if token == '-MD': continue
        args.append(token)
    args[args.index('-o') + 1] = str(obj)
    args[args.index(str(actual))] = str(cpp)
    args += ['-I' + str(actual.parent)]
    rc = invoke(args, root/'compile.log', 120)
    assert rc == 0, ('compile', rc)
    commands = subprocess.check_output(['ninja', '-t', 'commands', 'mantis-recorder-tests'], cwd=build, text=True).splitlines()
    link = shlex.split(commands[-1]); assert link[:2] == [':','&&'] and link[-2:] == ['&&',':']
    link = link[2:-2]
    archive = next(t for t in link if t.endswith('libmantis-recorder-observer.a'))
    link = [str(obj) if t == archive else t for t in link]
    link[link.index('-o') + 1] = str(binary)
    rc = invoke(link, root/'link.log', 120)
    assert rc == 0, ('link', rc)
    receipt = {'source_sha256': hashlib.sha256(before).hexdigest(), 'mutation_sha256': hashlib.sha256(content).hexdigest(),
               'compile_command': args, 'compile_exit': 0, 'link_command': link, 'link_exit': 0,
               'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'test': 'NOT_RUN'}
    (root/'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    assert actual.read_bytes() == before
    print('BUILD complete, native TEST not entered:', root/'receipt.json')
elif phase == 'test':
    receipt = json.loads((root/'receipt.json').read_bytes())
    assert hashlib.sha256(actual.read_bytes()).hexdigest() == receipt['source_sha256']
    assert hashlib.sha256((root/'fixture').read_bytes()).hexdigest() == receipt['binary_sha256']
    for name, binary in [('omitted-whole-seal', root/'fixture'), ('exact-restoration', build/'bin/mantis-recorder-tests')]:
        case = root/name; case.mkdir(); (case/'trace').mkdir()
        env = os.environ.copy(); env['MANTIS_RECORDER_OBSERVE_DIR'] = str(case/'trace')
        command = [str(binary), 'segmented', str(case/'project')]
        result = subprocess.run(command, cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        (case/'stdout.log').write_bytes(result.stdout); (case/'stderr.log').write_bytes(result.stderr)
        assert result.returncode == 0, (name, result.returncode)
        native = json.loads(result.stdout)
        assert native['callbacks'] == native['recovered'] == 24 and native['two_replays']
        data = load(next((case/'trace').glob('*.json')))
        if name == 'omitted-whole-seal':
            assert not any(e['stage'] in ('raw.seal.begin','raw.seal.end','store.segment.durable') for e in data['events'])
            try:
                decode(data, native['segment_oracle'])
            except Incomplete as exc:
                assert str(exc) == 'required segment seal/durable evidence missing', str(exc)
                receipt['negative'] = {'command':command, 'run_exit':0, 'decoder_exit':2, 'intended_assertion':str(exc),
                    'persisted_segments':len(native['segment_oracle']['segments'])}
            else:
                raise AssertionError('native omitted seal accepted')
        else:
            summary = decode(data, native['segment_oracle'])
            assert len(summary['segment_durable_acknowledgements']) == len(native['segment_oracle']['segments']) > 0
            receipt['restoration'] = {'command':command, 'exit':0, 'decoder':'GREEN', 'summary':summary}
    receipt['test'] = 'PASS: omitted whole seal RED, exact restoration GREEN; real Store unchanged'
    (root/'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(receipt['test'], root/'receipt.json')
else:
    raise SystemExit('select build or test')
