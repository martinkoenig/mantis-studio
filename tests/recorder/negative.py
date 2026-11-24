"""Disposable exact-input native negative sensitivity, no working-source edits.

Invoke after the focused target is built. Compiler/link commands come from this
configured build. Sequential compilation; output/logs/original inputs retained.
"""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile
from decoder import decode, load, Incomplete

source, build, parent = (Path(p).resolve() for p in sys.argv[1:4])
parent.mkdir(parents=True,exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='negative-',dir=parent))
actual = source/'src/device-runtime/device-runtime.cpp'
before = actual.read_bytes()
head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
baseline = '348e98b38740e9e7e2f65121b0a14478c94b6d87'
subprocess.run(['git','merge-base','--is-ancestor',baseline,head],cwd=source,check=True)
original = subprocess.check_output(['git','show',baseline+':src/device-runtime/device-runtime.cpp'],cwd=source)
assert subprocess.check_output(['git','hash-object','--stdin'],input=original,cwd=source,text=False).decode().strip() == '5c196d83c551d1823e8b0f6aa169544ec70741a6'
compilations = json.loads((build/'compile_commands.json').read_bytes())
entry = next(c for c in compilations if c['file']==str(actual) and 'mantis-recorder-session.dir' in c['command'])
compile_tokens = shlex.split(entry['command'])
commands = subprocess.check_output(['ninja','-t','commands','mantis-recorder-tests'],cwd=build,text=True).splitlines()
link = shlex.split(commands[-1])
assert link[:2] == [':','&&'] and link[-2:] == ['&&',':']
link = link[2:-2]
old_object = next(t for t in link if 'mantis-recorder-session.dir' in t and t.endswith('.o'))
needle = b'MANTIS_RECORDER_EVENT("cycle.end");'
assert before.count(needle)==1
receipt={'source_head':head,'source_sha256':hashlib.sha256(before).hexdigest(),
         'original_blob':'5c196d83c551d1823e8b0f6aa169544ec70741a6','cases':[]}

def invoke(command, directory, log):
    with log.open('wb') as output:
        result=subprocess.run(command,cwd=directory,stdout=output,stderr=subprocess.STDOUT,timeout=120)
    return result.returncode

for name,content in [('original348',original),
    ('removed-cycle-end',before.replace(needle,b'/* deliberately omitted cycle completion */')),
    ('mispaired-cycle',before.replace(needle,b'MANTIS_RECORDER_EVENT("cycle.end", first_.get());'))]:
    case=root/name; case.mkdir(); trace=case/'trace'; trace.mkdir()
    cpp=case/'session.cpp'; cpp.write_bytes(content)
    obj=case/'session.o'; binary=case/'fixture'
    args=[]; skip=False
    for token in compile_tokens:
        if skip: skip=False; continue
        if token in ('-MF','-MT'): skip=True; continue
        if token=='-MD': continue
        args.append(token)
    args[args.index('-o')+1]=str(obj)
    args[args.index(str(actual))]=str(cpp)
    args+=['-I'+str(actual.parent)]
    rc=invoke(args,build,case/'compile.log')
    assert rc==0, (name,'compile',rc)
    linking=[str(obj) if t==old_object else t for t in link]
    linking[linking.index('-o')+1]=str(binary)
    rc=invoke(linking,build,case/'link.log')
    assert rc==0,(name,'link',rc)
    env=os.environ.copy(); env['MANTIS_RECORDER_OBSERVE_DIR']=str(trace)
    mode='boundary' if name=='original348' else 'positive'
    command=[str(binary),mode,str(case/'project')]
    with (case/'execution.log').open('wb') as output:
        run=subprocess.run(command,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=15)
    row={'name':name,'source_sha256':hashlib.sha256(content).hexdigest(),
         'compile_command':args,'compile_exit':0,'link_command':linking,'link_exit':0,
         'run_command':command,'run_exit':run.returncode}
    if name=='original348':
        assert run.returncode==1 and b'future.wait_for(5s)' in (case/'execution.log').read_bytes(), 'original gap did not fail gate oracle'
        assert not list(trace.iterdir()),'original unexpectedly emitted diagnostic evidence'
        row['intended_assertion']='actual post-callback accounting gate not reached by original348'
    else:
        assert run.returncode==0,'mutation unexpectedly broke unrelated fixture behavior'
        outputs=list(trace.glob('*.json')); assert len(outputs)==1
        try:
            decode(load(outputs[0]))
        except Incomplete as exc:
            expected='complete writer cycle/accounting missing' if name=='removed-cycle-end' else 'mispaired writer cycle'
            assert str(exc)==expected,('wrong intended assertion',str(exc))
            row['decoder_exit']=2; row['intended_assertion']=str(exc)
        else: raise AssertionError('native mutation accepted: '+name)
    receipt['cases'].append(row)
    (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
assert actual.read_bytes()==before, 'working source changed during disposable campaign'
# Exact original instrumented binary and gate restore GREEN without editing or
# rebuilding any working input. No retry to replace natural acceptance evidence.
restored=root/'restored'; restored.mkdir(); (restored/'trace').mkdir()
env=os.environ.copy(); env['MANTIS_RECORDER_OBSERVE_DIR']=str(restored/'trace')
command=[str(build/'bin/mantis-recorder-tests'),'boundary',str(restored/'project')]
with (restored/'execution.log').open('wb') as output:
    result=subprocess.run(command,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=15)
assert result.returncode==0
summary=decode(load(next((restored/'trace').glob('*.json'))))
assert summary['callback_success']==8
receipt['restoration']={'run_command':command,'exit':0,'decoder':'GREEN','source_unchanged':True}
(root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS: original348 RED; removed/mispaired native cycle spans RED; exact instrumented restoration GREEN;',root/'receipt.json')
