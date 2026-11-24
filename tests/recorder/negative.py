"""Disposable native cycle controls with separate BUILD and exclusive TEST phases.

Original source and all outputs remain retained; working files are never mutated.
"""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import sys
from decoder import decode, load, Incomplete

source, build, root = (Path(p).resolve() for p in sys.argv[1:4])
phase = sys.argv[4]
actual = source/'src/device-runtime/device-runtime.cpp'
before = actual.read_bytes()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def invoke(command, log, timeout):
    with log.open('xb') as output:
        result = subprocess.run(command, cwd=build, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    return result.returncode

if phase == 'build':
    root.mkdir()
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    baseline = '348e98b38740e9e7e2f65121b0a14478c94b6d87'
    subprocess.run(['git','merge-base','--is-ancestor',baseline,head],cwd=source,check=True)
    original = subprocess.check_output(['git','show',baseline+':src/device-runtime/device-runtime.cpp'],cwd=source)
    assert subprocess.check_output(['git','hash-object','--stdin'],input=original,cwd=source).decode().strip() == '5c196d83c551d1823e8b0f6aa169544ec70741a6'
    entries = json.loads((build/'compile_commands.json').read_bytes())
    entry = next(c for c in entries if c['file']==str(actual) and 'mantis-recorder-session.dir' in c['command'])
    compile_tokens = shlex.split(entry['command'])
    commands = subprocess.check_output(['ninja','-t','commands','mantis-recorder-tests'],cwd=build,text=True).splitlines()
    link = shlex.split(commands[-1]); assert link[:2] == [':','&&'] and link[-2:] == ['&&',':']
    link = link[2:-2]
    old_object = next(t for t in link if 'mantis-recorder-session.dir' in t and t.endswith('.o'))
    needle = b'MANTIS_RECORDER_EVENT("cycle.end");'
    assert before.count(needle)==1
    receipt = {'source_head':head,'source_sha256':digest(actual), 'restored_binary_sha256':digest(build/'bin/mantis-recorder-tests'),
               'original_blob':'5c196d83c551d1823e8b0f6aa169544ec70741a6','cases':[], 'test':'NOT_RUN'}
    for name, content in [('original348',original),
        ('removed-cycle-end',before.replace(needle,b'/* deliberately omitted cycle completion */')),
        ('mispaired-cycle',before.replace(needle,b'MANTIS_RECORDER_EVENT("cycle.end", first_.get());'))]:
        case = root/name; case.mkdir()
        cpp, obj, binary = case/'session.cpp', case/'session.o', case/'fixture'
        cpp.write_bytes(content)
        args = []; skip = False
        for token in compile_tokens:
            if skip: skip=False; continue
            if token in ('-MF','-MT'): skip=True; continue
            if token=='-MD': continue
            args.append(token)
        args[args.index('-o')+1] = str(obj)
        args[args.index(str(actual))] = str(cpp)
        args += ['-I'+str(actual.parent)]
        rc = invoke(args, case/'compile.log', 120); assert rc==0, (name,'compile',rc)
        linking = [str(obj) if t==old_object else t for t in link]
        linking[linking.index('-o')+1] = str(binary)
        rc = invoke(linking, case/'link.log', 120); assert rc==0, (name,'link',rc)
        receipt['cases'].append({'name':name,'source_sha256':hashlib.sha256(content).hexdigest(),
            'binary_sha256':digest(binary), 'compile_command':args,'compile_exit':0,'link_command':linking,'link_exit':0})
        (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    assert actual.read_bytes()==before
    print('BUILD complete; TEST not entered',root/'receipt.json')
elif phase == 'test':
    receipt = json.loads((root/'receipt.json').read_bytes())
    assert digest(actual) == receipt['source_sha256']
    assert digest(build/'bin/mantis-recorder-tests') == receipt['restored_binary_sha256']
    for row in receipt['cases']:
        name = row['name']; case = root/name; trace = case/'trace'; trace.mkdir()
        binary = case/'fixture'; assert digest(binary)==row['binary_sha256']
        env = os.environ.copy(); env['MANTIS_RECORDER_OBSERVE_DIR']=str(trace)
        command = [str(binary), 'boundary' if name=='original348' else 'positive', str(case/'project')]

        with (case/'execution.log').open('xb') as output:
            result = subprocess.run(command,cwd=build,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=15)
        row.update(run_command=command, run_exit=result.returncode)
        if name == 'original348':
            assert result.returncode==1 and b'future.wait_for(5s)' in (case/'execution.log').read_bytes(), 'original gap did not fail gate oracle'
            assert not list(trace.iterdir())
            row['intended_assertion']='actual post-callback accounting gate not reached by original348'
        else:
            assert result.returncode==0, 'mutation broke unrelated fixture behavior'
            try:
                decode(load(next(trace.glob('*.json'))))
            except Incomplete as exc:
                expected = 'complete writer cycle/accounting missing' if name=='removed-cycle-end' else 'mispaired writer cycle'
                assert str(exc)==expected, ('wrong intended assertion',str(exc))
                row.update(decoder_exit=2,intended_assertion=str(exc))
            else:
                raise AssertionError('native mutation accepted: '+name)
        (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    restored = root/'restored'; restored.mkdir(); (restored/'trace').mkdir()
    env = os.environ.copy(); env['MANTIS_RECORDER_OBSERVE_DIR']=str(restored/'trace')
    command = [str(build/'bin/mantis-recorder-tests'),'boundary',str(restored/'project')]
    with (restored/'execution.log').open('xb') as output:
        result = subprocess.run(command,cwd=build,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=15)
    assert result.returncode==0
    assert decode(load(next((restored/'trace').glob('*.json'))))['callback_success']==8
    assert actual.read_bytes()==before
    receipt.update(test='PASS', restoration={'run_command':command,'exit':0,'decoder':'GREEN','source_unchanged':True})
    (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS: original348 and removed/mispaired cycle RED; exact restoration GREEN',root/'receipt.json')
else:
    raise SystemExit('select build or test')
