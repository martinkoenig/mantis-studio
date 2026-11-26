"""Native pipeline, actual observer failure controls, independent decoder mutations."""
from pathlib import Path
import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from decoder import collect, decode, load, Incomplete, require_owned_acceptance
from oracle_controls import causal_controls, segment_controls

def rejected(data, reason):
    try:
        decode(data)
    except Incomplete as exc:
        return {'control': reason, 'status': 'REJECTED', 'diagnostic': str(exc)}
    raise AssertionError('invalid evidence accepted: ' + reason)

def main():
    binary = Path(sys.argv[1]).resolve()
    parent = Path(sys.argv[2]).resolve()
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='campaign-', dir=parent))
    receipt = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'decoder_sha256': hashlib.sha256(Path(__file__).with_name('decoder.py').read_bytes()).hexdigest(),
               'cases': [], 'negative_controls': []}
    golden = None
    for mode in ('positive','boundary','segmented','segmented-unsealed','queue','storage','writer','source-observer','source-error','source-throw','init-sibling','serialization','overflow','crash','disabled','product-disabled','unavailable','collision'):
        case = root / mode
        trace = case / 'trace'
        trace.mkdir(parents=True)
        env = os.environ.copy()
        env.pop('MANTIS_RECORDER_OBSERVE_DIR', None)
        if mode != 'disabled':
            env['MANTIS_RECORDER_OBSERVE_DIR'] = str(trace if mode != 'unavailable' else trace/'missing')
        actual_mode = 'positive' if mode in ('disabled','product-disabled','unavailable') else mode
        start = time.monotonic_ns()
        command = [str(Path(sys.argv[4]).resolve() if mode == 'product-disabled' else binary), actual_mode, str(case/'project')]
        result = subprocess.run(command, env=env, capture_output=True, timeout=15)
        elapsed = time.monotonic_ns() - start
        (case/'stdout.log').write_bytes(result.stdout)
        (case/'stderr.log').write_bytes(result.stderr)
        entry = {'mode': mode, 'exit': result.returncode, 'elapsed_ns': elapsed,
                 'command': command, 'trace_bytes': sum(p.stat().st_size for p in trace.iterdir() if p.is_file())}
        receipt['cases'].append(entry)
        (root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        assert result.returncode == (27 if mode == 'crash' else 0), (mode, result.stderr.decode(errors='replace'))
        outputs, markers = list(trace.glob('*.json')), list(trace.glob('*.started'))
        if mode == 'init-sibling':
            native = [json.loads(line) for line in result.stdout.splitlines()]
            assert len(native) == 2 and {n['capture'] for n in native} == {'controlled-positive', 'controlled-init-failed'}
            assert len(outputs) == len(markers) == 1
            assert all(n['produced'] == n['admitted'] == n['callbacks'] == n['recovered'] == 8
                       and not n['primary'] and n['two_replays'] for n in native)
            assert b'UNAVAILABLE initialization' in result.stderr
            roster = [{'pid': n['pid'], 'capture': n['capture'], 'allow_missing_final': False} for n in native]
            try:
                require_owned_acceptance(collect(trace), [{'pid': native[0]['pid'], 'exit': 0}], roster,
                                         ['recorder-observer: UNAVAILABLE initialization; capture error unchanged'])
            except Incomplete as exc:
                assert str(exc) == 'actual Session/observer roster mismatch'
                receipt['negative_controls'].append({'control': 'one actual Session initialization fails; both captures healthy',
                    'status': 'REJECTED', 'diagnostic': str(exc), 'roster': roster, 'native': native})
            else:
                raise AssertionError('healthy sibling concealed unavailable observer Session')
            entry['evidence_status'] = 'INCOMPLETE: one Session observer unavailable; both real replays healthy'
            continue
        if mode in ('crash','serialization'):
            assert len(markers) == 1 and not outputs, (mode, 'missing incomplete evidence marker')
            if mode == 'serialization':
                assert b'INCOMPLETE serialization' in result.stderr
                assert json.loads(result.stdout)['callbacks'] == 8
            entry['evidence_status'] = 'INCOMPLETE: start marker without retired trace'
            continue
        if mode in ('disabled','product-disabled','unavailable','collision'):
            assert not outputs and len(markers) == (1 if mode == 'collision' else 0)
            if mode == 'collision':
                assert markers[0].read_text() == 'retained prior observation\n'
            if mode in ('unavailable','collision'):
                assert b'UNAVAILABLE initialization' in result.stderr
            assert json.loads(result.stdout)['callbacks'] == 8
            continue
        assert len(outputs) == len(markers) == 1
        data = load(outputs[0])
        if mode in ('overflow','storage','writer','source-observer'):
            receipt['negative_controls'].append(rejected(data, mode+' actual observer fault'))
            if mode == 'overflow':
                assert data['dropped'] > 0 and len(data['events']) <= 3*8192
                continue
            native = json.loads(result.stdout)
            if mode == 'source-observer':
                assert data['invalid_identity'] is True
                assert native['produced'] == native['admitted'] == native['callbacks'] == native['recovered'] == 8
                assert native['primary'] == '' and native['two_replays'] is True
                entry['evidence_status'] = 'INCOMPLETE: throwing source identity query; capture healthy'
                continue
            assert native['callbacks'] == 3 and native['recovered'] == (4 if mode == 'storage' else 3)
            # Observer failure is explicitly flagged, but inspect chronology with
            # that one fault flag cleared only as a verifier control, never PASS evidence.
            hypothetical = copy.deepcopy(data)
            hypothetical['invalid_identity'] = False
            summary = decode(hypothetical)
            assert summary['callback_success'] == 3 and summary['callback_failure'] == 1
            assert summary['schema1_durable_acknowledgements'] == 3
            entry['evidence_status'] = 'INCOMPLETE: observer boundary callback failed'
            continue
        native = json.loads(result.stdout)
        summary = decode(data, native['segment_oracle'])
        if mode == 'segmented':
            assert summary['segment_durable_acknowledgements'], 'fixture did not exercise a real seal'
            receipt['negative_controls'].extend(segment_controls(data, native['segment_oracle']))
        if mode == 'segmented-unsealed':
            assert summary['segment_durable_acknowledgements'] == native['segment_oracle']['segments'] == []
            assert summary['segment_evidence'].startswith('QUALIFIED:') and native['callbacks'] == 4
        if mode in ('source-error', 'source-throw'):
            assert summary['source_failures'] == 1 and native['primary'] == 'source primary'
        assert summary['produced'] == native['produced']
        assert summary['admitted'] == native['admitted']
        assert summary['callback_success'] == native['callbacks']
        assert native['schema1_acknowledged'] == summary['schema1_durable_acknowledgements']
        entry['summary'] = summary
        if mode == 'positive':
            golden = data
        if mode == 'boundary':
            cycle = next(c for c in summary['cycles'] if c['key'][0] == 1)
            # Event order, not a sleep or an invented duration threshold, proves
            # that old callback-only append timing excludes the held real mutex.
            assert cycle['callback_end_ns'] <= native['gate_entered_ns'] <= native['gate_release_ns'] <= cycle['account_lock_acquired_ns'] <= cycle['cycle_end_ns']
            assert cycle['post_callback_ns'] >= native['gate_release_ns'] - native['gate_entered_ns']
            assert cycle['service_ns'] > cycle['append_ns']
            entry['boundary_proof'] = native
    probe_dir = root/'microprobe'
    probe_dir.mkdir()
    probe_env = os.environ.copy()
    probe_env['MANTIS_RECORDER_OBSERVE_DIR'] = str(probe_dir)
    probe = subprocess.run([str(Path(sys.argv[3]).resolve())], env=probe_env, capture_output=True, timeout=15)
    (probe_dir/'stdout.log').write_bytes(probe.stdout)
    (probe_dir/'stderr.log').write_bytes(probe.stderr)
    assert probe.returncode == 0, probe.stderr.decode(errors='replace')
    receipt['microprobe'] = json.loads(probe.stdout)
    assert receipt['microprobe']['allocations_per_record'] == 0
    observed = [load(p) for p in sorted(probe_dir.glob('*.json'))]
    assert len(observed) == len(list(probe_dir.glob('*.started'))) == 8
    assert sum(len(d['events']) for d in observed) == 4096+4+7*2
    assert all(d['dropped']==0 and len(d['events'])<=3*8192 for d in observed)
    receipt['microprobe']['reserved_record_bytes_per_capture'] = observed[0]['retained_record_bytes_per_capture']
    receipt['microprobe']['record_bytes'] = observed[0]['record_bytes']
    receipt['microprobe']['eight_capture_output_bound_enforced'] = True
    assert golden is not None
    # A valid sibling must never hide a serialized invalid observation. This is
    # an ownership-oracle negative control, not a replacement daemon workload.
    good = dict(decode(golden), marker_present=True, final_present=True)
    # Synthetic collection controls only. Actual killed-daemon/whole-Session
    # absence checks run independently in the real acceptance workload.
    killed = {'status': 'INCOMPLETE', 'pid': good['pid'] + 1, 'capture': 'oracle-killed',
              'marker_present': True, 'final_present': False}
    roster = [{'pid': good['pid'], 'capture': good['capture'], 'allow_missing_final': False},
              {'pid': killed['pid'], 'capture': killed['capture'], 'allow_missing_final': True}]
    exits = [{'pid': good['pid'], 'exit': 0}, {'pid': killed['pid'], 'exit': -9}]
    require_owned_acceptance([good, killed], exits, roster, [])
    for name, sibling in (
        ('serialized incomplete sibling', dict(good, status='INCOMPLETE')),
        ('unowned sibling', dict(good, pid=good['pid']+2)),
        ('missing final without owned kill', dict(good, status='INCOMPLETE', final_present=False)),
        ('missing marker with surviving final', dict(good, marker_present=False))):
        try:
            require_owned_acceptance([sibling, killed], exits, roster, [])
        except Incomplete as exc:
            receipt['negative_controls'].append({'control':name,'status':'REJECTED','diagnostic':str(exc)})
        else:
            raise AssertionError('invalid acceptance collection accepted: '+name)
    for name, rows, expected_roster, statuses in (
        ('entire complete sibling absent', [killed], roster, []),
        ('wrong capture given owned kill exception', [good, killed], [dict(r, allow_missing_final=not r['allow_missing_final']) for r in roster], []),
        ('retained unavailable outcome despite complete sibling', [good, killed], roster, ['UNAVAILABLE initialization'])):
        try:
            require_owned_acceptance(rows, exits, expected_roster, statuses)
        except Incomplete as exc:
            receipt['negative_controls'].append({'control':name,'status':'REJECTED','diagnostic':str(exc)})
        else:
            raise AssertionError('invalid acceptance roster accepted: '+name)
    receipt['negative_controls'].extend(causal_controls(golden))
    mutations = []
    for stage in ('cycle.end','account.lock.acquired','store.packet.end','store.packet.begin','source.frame','admission.accepted'):
        bad = copy.deepcopy(golden)
        index = next(i for i,e in enumerate(bad['events']) if e['stage'] == stage)
        del bad['events'][index]
        mutations.append((stage+' absent', bad))
        # Renumber to ensure matching-stage validation, not just ordinal checks,
        # rejects a plausible buggy producer that never emitted the stage.
        ordinals = [0,0,0]
        for e in bad['events']:
            e['ordinal'] = ordinals[e['lane']]; ordinals[e['lane']] += 1
        receipt['negative_controls'].append(rejected(bad, stage+' never emitted'))
    bad = copy.deepcopy(golden)
    first = next(i for i,e in enumerate(bad['events']) if e['stage']=='callback.end')
    bad['events'][first], bad['events'][first+1] = bad['events'][first+1], bad['events'][first]
    mutations.append(('reordered callback/accounting',bad))
    bad = copy.deepcopy(golden); bad['events'].insert(first, copy.deepcopy(bad['events'][first]))
    mutations.append(('duplicated callback',bad))
    bad = copy.deepcopy(golden); next(e for e in bad['events'] if e['stage']=='source.frame')['source']='foreign-source'
    mutations.append(('wrong source',bad))
    bad = copy.deepcopy(golden); next(e for e in bad['events'] if e['stage']=='cycle.end')['key'][0] += 1
    mutations.append(('mispaired end-of-cycle',bad))
    bad = copy.deepcopy(golden); next(e for e in bad['events'] if e['stage']=='store.lock.acquired')['key'][3]='foreign-clock'
    mutations.append(('wrong frame clock',bad))
    bad = copy.deepcopy(golden); bad['clock']='foreign-clock'
    mutations.append(('wrong observer clock',bad))
    bad = copy.deepcopy(golden); bad['retired']=False
    mutations.append(('observer not retired',bad))
    bad = copy.deepcopy(golden); bad['events']=bad['events'][:-1]
    mutations.append(('truncated cleanup',bad))
    for name,bad in mutations:
        receipt['negative_controls'].append(rejected(bad,name))
    broken = root/'truncated.json'
    broken.write_bytes(json.dumps(golden).encode()[:-19])
    broken.with_suffix('.started').write_text('{}')
    try:
        load(broken)
    except Incomplete:
        receipt['negative_controls'].append({'control':'truncated serialization','status':'REJECTED'})
    else:
        raise AssertionError('truncated serialization accepted')
    receipt['status']='PASS: functional qualification, not historical causal closure or T4'
    (root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'status':receipt['status'],'receipt':str(root/'receipt.json'),
                      'cases':len(receipt['cases']),'negative_controls':len(receipt['negative_controls'])}))

if __name__ == '__main__':
    main()
