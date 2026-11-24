"""Strict, bounded offline decoder for internal recorder diagnostics v1.

Not an authenticity or scheduler profiler. A complete trace of a failed capture is
not a successful capture; callback, schema-1 durable acknowledgement and recovered
prefix counts are deliberately separate. Missing evidence always fails closed.
"""
from pathlib import Path
import json
import re

MAX_BYTES = 24 * 1024 * 1024
CAPACITY = 8192

class Incomplete(ValueError):
    pass

def require(value, message):
    if not value:
        raise Incomplete(message)

def load(path):
    path = Path(path)
    require(path.is_file() and path.stat().st_size <= MAX_BYTES, 'missing/oversize trace')
    marker_path = path.with_suffix('.started')
    require(marker_path.is_file() and marker_path.stat().st_size <= 65536, 'missing/oversize start marker')
    try:
        data = json.loads(path.read_bytes())
        marker = json.loads(marker_path.read_bytes())
    except (ValueError, OSError) as exc:
        raise Incomplete('truncated/missing trace or start marker') from exc
    require(type(marker) is dict and marker.get('version') == 1 and marker.get('retired') is False
            and {'capture','sources','pid','generation','opened_ns','clock','build'} <= marker.keys(), 'incomplete start identity')
    for key, value in marker.items():
        if key != 'retired':
            require(data.get(key) == value, 'start/final identity mismatch: ' + key)
    return data

def decode(data):
    require(type(data) is dict, 'invalid trace object')
    require(data.get('version') == 1, 'unsupported version')
    require(data.get('clock') == 'process-steady-ns', 'wrong clock domain')
    require(data.get('retired') is True, 'observer did not retire')
    require(type(data.get('dropped')) is int and data['dropped'] == 0, 'observer overflow/dropped records')
    require(data.get('invalid_identity') is False, 'observer failed/identity unavailable')
    require(type(data.get('generation')) is int and 1 <= data['generation'] <= 8, 'bad generation')
    require(type(data.get('pid')) is int and data['pid'] > 0, 'bad process identity')
    require(type(data.get('opened_ns')) is int and data['opened_ns'] >= 0, 'missing observation clock epoch')
    require(isinstance(data.get('build'), str) and re.fullmatch('[0-9a-f]{64}', data['build']), 'missing build identity')
    require(isinstance(data.get('capture'), str) and 0 < len(data['capture']) < 96, 'missing capture identity')
    sources = data.get('sources')
    require(isinstance(sources, list) and 0 < len(sources) <= 32 and all(isinstance(s,str) and 0 < len(s) < 96 for s in sources) and len(set(sources)) == len(sources), 'invalid sources')
    require(data.get('capacity_per_lane') == CAPACITY, 'unknown capacity')
    require((data.get('queue_capacity'), data.get('push_ms'), data.get('startup_s'), data.get('unpaced_ms')) == (32, 50, 5, 33), 'workload contract changed')
    events = data.get('events')
    require(isinstance(events, list) and 0 < len(events) <= 3 * CAPACITY, 'event bound')
    lanes = [[], [], []]
    previous_lane = 0
    for e in events:
        require(type(e) is dict and set(e) == {'lane','ordinal','stage','ns','value','key','source','artifact'}, 'event shape')
        lane = e['lane']
        require(type(lane) is int and previous_lane <= lane <= 2, 'reordered lanes')
        previous_lane = lane
        require(e['ordinal'] == len(lanes[lane]), 'absent/reordered/duplicated ordinal')
        require(type(e['ns']) is int and e['ns'] >= data['opened_ns'] and (not lanes[lane] or e['ns'] >= lanes[lane][-1]['ns']), 'reordered/foreign clock')
        require(type(e['key']) is list and len(e['key']) == 4 and all(type(n) is int for n in e['key'][:3]) and isinstance(e['key'][3], str) and len(e['key'][3]) < 96, 'invalid frame/clock identity')
        require(all(isinstance(e[k], str) and len(e[k]) < 96 for k in ('stage','source','artifact')), 'unbounded text')
        require(type(e['value']) is int, 'invalid scalar')
        require(e['source'] == '' or (lane == 1 and e['stage'] == 'source.frame'), 'unexpected source identity')
        lanes[lane].append(e)
    for lane in lanes[:2]:
        require(len(lane) >= 2 and lane[0]['stage'] == 'worker.begin' and lane[-1]['stage'] == 'worker.end', 'worker retirement missing')
    require([e['stage'] for e in lanes[2]] == ['cleanup.begin','cleanup.end'], 'cleanup boundaries missing')
    require(all(e['ns'] <= lanes[2][-1]['ns'] for lane in lanes[:2] for e in lane), 'cleanup before worker retirement')
    produced, admitted, rejected = {}, {}, {}
    pstate, candidate = 'next', None
    for e in lanes[1][1:-1]:
        s, k = e['stage'], tuple(e['key'])
        if pstate == 'next':
            require(s == 'source.next.begin', 'source next begin missing')
            pstate = 'returned'
        elif pstate == 'returned':
            require(s == 'source.next.end', 'source next end missing')
            pstate = 'frame-or-next'
        elif pstate == 'frame-or-next' and s == 'source.next.begin':
            pstate = 'returned'
        elif pstate == 'frame-or-next':
            require(s == 'source.frame' and e['source'] in sources and k not in produced and k[3], 'wrong/duplicated source frame/clock')
            produced[k] = e
            candidate, pstate = k, 'admission'
        elif pstate == 'admission':
            require(s == 'admission.begin' and k == candidate and 0 <= e['value'] <= 32, 'admission begin/identity missing')
            pstate = 'outcome'
        else:
            require(s in ('admission.accepted','admission.failed') and k == candidate and 0 <= e['value'] <= 32, 'admission outcome/identity missing')
            (admitted if s == 'admission.accepted' else rejected)[k] = e
            pstate = 'next'
    require(pstate in ('next','frame-or-next'), 'incomplete producer stage')
    successful, failures, durable, cycles = [], [], [], []
    raw_artifacts = set()
    w = lanes[0][1:-1]
    i = 0
    while i < len(w):
        require(w[i]['stage'] == 'pop.begin', 'next-pop boundary missing')
        pop_begin = w[i]; i += 1
        require(i < len(w), 'pop outcome missing')
        if w[i]['stage'] == 'pop.empty':
            i += 1
            require(i < len(w) and w[i]['stage'] in ('pop.woke','pop.closed'), 'queue empty/wait distinction missing')
        else:
            require(w[i]['stage'] == 'pop.ready', 'queue availability distinction missing')
        pop_end = w[i]; i += 1
        if pop_end['stage'] == 'pop.closed':
            require(i == len(w), 'work after closed queue')
            break
        k = tuple(pop_end['key'])
        require(k in admitted and k not in successful and k not in failures, 'unmatched/duplicate writer identity')
        require(produced[k]['ns'] <= pop_end['ns'] and pop_begin['ns'] <= pop_end['ns'], 'producer/pop chronology')
        require(i < len(w) and w[i]['stage'] == 'callback.begin' and tuple(w[i]['key']) == k, 'callback begin missing/mispaired')
        callback_begin = w[i]; i += 1
        store_events = []
        while i < len(w) and w[i]['stage'] not in ('callback.end','callback.failed'):
            require(tuple(w[i]['key']) == k, 'mispaired Store/frame span')
            store_events.append(w[i]); i += 1
        require(i < len(w), 'callback end missing')
        callback_end = w[i]; i += 1
        require(tuple(callback_end['key']) == k, 'callback identity mismatch')
        if store_events:
            require(store_events[0]['stage'] == 'store.begin' and store_events[-1]['stage'] in ('store.end','store.failed'), 'Store dispatch boundaries missing')
            aid = store_events[0]['artifact']
            require(aid and all(e['artifact'] == aid for e in store_events), 'wrong Store identity')
            require([e['stage'] for e in store_events[1:3]] == ['store.lock.begin','store.lock.acquired'], 'Store lock boundaries missing')
            stack = []
            for e in store_events[3:-1]:
                s = e['stage']
                if s.endswith('.begin'):
                    stack.append(s[:-6])
                elif s.endswith('.end'):
                    require(stack and stack.pop() == s[:-4], 'mispaired Store I/O span')
                elif s == 'store.schema1.durable':
                    require(not stack and store_events[-1]['stage'] == 'store.end', 'durability without successful operation')
                    durable.append(k)
                elif s == 'store.segment.durable':
                    require(e['value'] > 0, 'invalid segment acknowledgement')
                else:
                    raise Incomplete('unknown Store stage: ' + s)
            if store_events[-1]['stage'] == 'store.end':
                require(not stack, 'Store stage end missing')
                names = [e['stage'] for e in store_events]
                require('store.schema1.durable' in names or 'raw.write.end' in names, 'required Store stage missing')
                require(len(names) == len(set(names)), 'duplicate Store stage')
                if 'store.schema1.durable' in names:
                    require(names[3:-1] == ['store.packet.begin','store.packet.end',
                        'store.journal.begin','store.journal.end','store.commit.begin','store.commit.end',
                        'store.schema1.durable'], 'missing/reordered schema-1 I/O/commit stage')
                else:
                    required = ['raw.write.begin','raw.write.end']
                    if aid not in raw_artifacts:
                        required = ['raw.provenance.begin','raw.provenance.end','raw.init.begin','raw.init.end',
                            'raw.open.begin','raw.open.end','raw.directory.begin','raw.directory.end'] + required
                    require(all(s in names for s in required), 'missing RawTrace stage')
                    require([names.index(s) for s in required] == sorted(names.index(s) for s in required), 'reordered RawTrace stages')
                    raw_artifacts.add(aid)
        if callback_end['stage'] == 'callback.failed':
            failures.append(k)
            require(i == len(w), 'writer continued after failure')
            break
        require(store_events and store_events[-1]['stage'] == 'store.end', 'callback success without Store evidence')
        successful.append(k)
        tail = w[i:i+4]
        require([e['stage'] for e in tail] == ['account.lock.begin','account.lock.acquired','account.end','cycle.end'], 'complete writer cycle/accounting missing')
        require(all(tuple(e['key']) == k for e in tail), 'mispaired writer cycle')
        cycles.append({'key': list(k), 'source': produced[k]['source'],
                       'append_ns': callback_end['ns'] - callback_begin['ns'],
                       'service_ns': tail[-1]['ns'] - pop_end['ns'],
                       'post_callback_ns': tail[-1]['ns'] - callback_end['ns'],
                       'callback_end_ns': callback_end['ns'], 'cycle_end_ns': tail[-1]['ns'],
                       'account_lock_begin_ns': tail[0]['ns'], 'account_lock_acquired_ns': tail[1]['ns'],
                       'queue_path': pop_end['stage'], 'pop_ns': pop_end['ns'] - pop_begin['ns']})
        i += 4
    require(w and w[-1]['stage'] in ('pop.closed','callback.failed'), 'final next-pop/service boundary missing')
    if not failures:
        require(len(admitted) == len(successful), 'admitted callback evidence missing')
    require(successful or failures, 'no actual callback evidence')
    return {'status': 'COMPLETE_CHRONOLOGY', 'capture': data['capture'], 'build': data['build'],
            'pid': data['pid'], 'generation': data['generation'],
            'produced': len(produced), 'admitted': len(admitted), 'rejected': len(rejected),
            'callback_success': len(successful), 'callback_failure': len(failures),
            'schema1_durable_acknowledgements': len(durable),
            'recoverable_prefix': 'UNAVAILABLE: verify actual Store/replay separately',
            'residual_wall_time_attribution': 'UNKNOWN', 'cycles': cycles}

def require_owned_acceptance(summaries, exits):
    """Every serialized trace must validate; only an owned SIGKILL may lack a final."""
    require(isinstance(summaries, list) and 0 < len(summaries) <= 16, 'acceptance trace bound')
    require(isinstance(exits, list) and 0 < len(exits) <= 8, 'owned daemon bound')
    owned = {p['pid']: p['exit'] for p in exits}
    require(len(owned) == len(exits) and all(type(p) is int and p > 0 and type(rc) is int
            for p, rc in owned.items()), 'owned daemon not retired/identity unavailable')
    for summary in summaries:
        require(summary.get('pid') in owned, 'trace from unowned daemon')
        if summary['status'] != 'COMPLETE_CHRONOLOGY':
            require(summary['status'] == 'INCOMPLETE' and summary.get('final_present') is False,
                    'serialized observer evidence invalid/incomplete')
            require(owned[summary['pid']] == -9, 'missing trace without owned SIGKILL')
    require(any(s['status'] == 'COMPLETE_CHRONOLOGY' and s['callback_success'] > 0 for s in summaries),
            'no real completed Session/Store trace')
    require(any(s['status'] == 'INCOMPLETE' for s in summaries), 'hard-killed observer falsely complete')

if __name__ == '__main__':
    import sys
    try:
        print(json.dumps(decode(load(sys.argv[1])), indent=2))
    except (Incomplete, OSError, KeyError, TypeError, ValueError) as exc:
        print('INCOMPLETE: ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
