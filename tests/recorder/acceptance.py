"""Run the existing real registered acceptance workload, not a replacement fixture."""
from pathlib import Path
import contextlib
import hashlib
import json
import os
import runpy
import subprocess
import sys
import tempfile
import traceback
from decoder import decode, load, Incomplete, require_owned_acceptance

build, source, parent = map(lambda p: Path(p).resolve(), sys.argv[1:4])
parent.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='actual-', dir=parent))
trace = root/'trace'
trace.mkdir()
script = source/'tests/integration/acceptance.py'
daemon = build/'bin/mantis-recorder-mantisd'
inputs = [script, source/'tests/integration/capture_lifecycle.py', Path(__file__),
          Path(__file__).with_name('decoder.py'), build/'CMakeCache.txt', daemon,
          build/'bin/mantis-cli', build/'bin/mantis-plugin-host']
inputs += sorted(p for p in (build/'plugins').iterdir() if p.is_file())
inputs += sorted(p for p in (build/'recipes').iterdir() if p.is_file())
receipt = {'workload':'existing tests/integration/acceptance.py; assertions unchanged',
           'inputs': {str(p.relative_to(source)) if p.is_relative_to(source) else str(p):
                      hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
           'source_head':subprocess.check_output(['git','rev-parse','HEAD'], cwd=source, text=True).strip(),
           'diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','HEAD'], cwd=source)).hexdigest(),
           'source_status':subprocess.check_output(['git','status','--short'], cwd=source, text=True),
           'argv':[str(script),str(build),'--daemon',str(daemon)], 'exit':None,
           'qualification':'functional characterization under observer perturbation, not T4 or historical cause'}
(root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
original = os.environ.get('MANTIS_RECORDER_OBSERVE_DIR')
os.environ['MANTIS_RECORDER_OBSERVE_DIR'] = str(trace)
sys.path.insert(0,str(script.parent))
sys.argv = receipt['argv']
try:
    with (root/'complete-output.log').open('w') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
        try:
            completed = runpy.run_path(str(script), run_name='__main__')
            receipt['daemon_exits'] = [{'pid': p.pid, 'exit': p.returncode} for p in completed['daemons']]
            receipt['exit']=0
        except BaseException:
            receipt['exit']=1
            traceback.print_exc()
            raise
finally:
    if original is None:
        os.environ.pop('MANTIS_RECORDER_OBSERVE_DIR',None)
    else:
        os.environ['MANTIS_RECORDER_OBSERVE_DIR']=original
    summaries = []
    for marker in sorted(trace.glob('*.started')):
        final = marker.with_suffix('.json')
        try:
            summaries.append(decode(load(final)))
        except (Incomplete,ValueError,KeyError,OSError,TypeError) as exc:
            summaries.append({'status':'INCOMPLETE','reason':str(exc),
                              'marker':marker.name,'final_present':final.exists(),
                              'pid':int(marker.stem.split('-')[0])})
    receipt['traces']=summaries
    (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Actual acceptance exit:',receipt['exit'],'; retained receipt:',root/'receipt.json')
# The workload intentionally SIGKILLs its final active daemon. That observation
# MUST remain incomplete. Earlier finalized sessions must have complete matching
# production writer/Store chronology; later passing acceptance cannot repair it.
require_owned_acceptance(summaries, receipt['daemon_exits'])
assert receipt['exit']==0
print('PASS: actual daemon/virtual-scanner characterization; killed observer remains INCOMPLETE')
