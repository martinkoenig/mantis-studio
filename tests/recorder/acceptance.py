"""Run the existing real registered acceptance workload, not a replacement fixture."""
from pathlib import Path
import contextlib
import hashlib
import json
import os
import runpy
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import traceback
from decoder import collect, Incomplete, require, require_owned_acceptance

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
receipt.update(session_roster=[], observer_statuses=[], store_capture_roster=[])


def checkpoint(phase, project_root, pid, state, killed_capture, log):
    # Independent runtime Session roster AND existing Store capture provenance;
    # neither is discovered from observer files. Retain outcomes before the
    # workload's TemporaryDirectory removes its otherwise successful daemon log.
    try:
        if state is not None:
            captures = list(state.captures)
            require(len(captures) <= 16, 'runtime roster bound')
            for capture in captures:
                receipt['session_roster'].append({'pid': pid, 'capture': capture.id,
                    'raw_artifact': capture.raw_artifact, 'allow_missing_final': capture.id == killed_capture})
            if killed_capture is not None:
                require(sum(c.id == killed_capture and c.active for c in captures) == 1, 'owned killed capture identity unavailable')
        with sqlite3.connect((project_root/'Project.mantis/project.sqlite').as_uri() + '?mode=ro', uri=True) as db:
            rows = db.execute("SELECT id,provenance FROM artifacts WHERE type='org.mantis.RawCapture'").fetchall()
        require(len(rows) <= 16, 'Store roster bound')
        stored = [{'raw_artifact': raw, 'capture': json.loads(provenance)['parameters']['capture_id']} for raw, provenance in rows]
        receipt['store_capture_roster'] = stored
        require({(r['capture'], r['raw_artifact']) for r in stored} ==
                {(r['capture'], r['raw_artifact']) for r in receipt['session_roster']}, 'runtime/Store capture roster mismatch')
        log.flush()
        with Path(log.name).open('rb') as stream:
            text = stream.read(65537)
        require(len(text) <= 65536, 'observer outcome log bound exceeded')
        receipt['observer_statuses'] = sorted(set(receipt['observer_statuses'] +
            [line for line in text.decode(errors='replace').splitlines() if line.startswith('recorder-observer:')]))
        require(len(receipt['observer_statuses']) <= 128, 'observer outcome count exceeded')
    except BaseException as exc:
        # Never replace a workload/cleanup primary with an observer failure.
        receipt['observer_statuses'].append('INCOMPLETE checkpoint ' + phase + ': ' + type(exc).__name__)
    try:
        (root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    except OSError:
        receipt['observer_statuses'].append('INCOMPLETE checkpoint serialization')

(root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
original = os.environ.get('MANTIS_RECORDER_OBSERVE_DIR')
os.environ['MANTIS_RECORDER_OBSERVE_DIR'] = str(trace)
sys.path.insert(0,str(script.parent))
sys.argv = receipt['argv']
try:
    with (root/'complete-output.log').open('w') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
        try:
            completed = runpy.run_path(str(script), init_globals={'_recorder_checkpoint': checkpoint}, run_name='__main__')
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
    summaries = collect(trace)
    receipt['traces']=summaries
    (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Actual acceptance exit:',receipt['exit'],'; retained receipt:',root/'receipt.json')
# The workload intentionally SIGKILLs its final active daemon. That observation
# MUST remain incomplete. Earlier finalized sessions must have complete matching
# production writer/Store chronology; later passing acceptance cannot repair it.
require_owned_acceptance(summaries, receipt['daemon_exits'], receipt['session_roster'], receipt['observer_statuses'])
# Remove BOTH marker and final from an actual complete sibling. The independent
# runtime/Store roster must still reject the missing whole observation.
absent = next(s for s in summaries if s['status'] == 'COMPLETE_CHRONOLOGY')
absent_stem = str(absent['pid']) + '-' + str(absent['generation'])
missing_dir = root/'whole-session-absent'
missing_dir.mkdir()
for path in trace.iterdir():
    if path.stem != absent_stem:
        shutil.copyfile(path, missing_dir/path.name)
missing = collect(missing_dir)
try:
    require_owned_acceptance(missing, receipt['daemon_exits'], receipt['session_roster'], [])
except Incomplete as exc:
    require(str(exc) == 'actual Session/observer roster mismatch', 'wrong whole-Session absence assertion')
    receipt['whole_session_absence_control'] = {'status': 'REJECTED', 'diagnostic': str(exc)}
else:
    raise AssertionError('whole actual Session observation absent but accepted')
(root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
assert receipt['exit']==0
print('PASS: actual daemon/virtual-scanner characterization; killed observer remains INCOMPLETE')
