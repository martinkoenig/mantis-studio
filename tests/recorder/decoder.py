"""Strict, bounded offline decoder for internal recorder diagnostics v2.

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
    require(type(marker) is dict and marker.get('version') == 2 and marker.get('retired') is False
            and {'capture','sources','pid','generation','opened_ns','clock','build'} <= marker.keys(), 'incomplete start identity')
    for key, value in marker.items():
        if key != 'retired':
            require(data.get(key) == value, 'start/final identity mismatch: ' + key)
    return data

def decode(data, segment_oracle=None):
    require(type(data) is dict, 'invalid trace object')
    require(data.get('version') == 2, 'unsupported version')
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
    produced, admitted, rejected, admission_begins = {}, {}, {}, {}
    source_failures, source_empty = 0, 0
    pstate, candidate, returned = 'next', None, None
    for e in lanes[1][1:-1]:
        s, k = e['stage'], tuple(e['key'])
        if pstate == 'next':
            require(s == 'source.next.begin', 'source next begin missing')
            pstate = 'returned'
        elif pstate == 'returned':
            require(s in ('source.next.end', 'source.next.threw'), 'source next end missing')
            if s == 'source.next.threw':
                source_failures += 1
                pstate = 'terminal'
            else:
                require(e['value'] in (-1, 0, 1), 'source return outcome unavailable')
                returned = k if e['value'] == 1 else None
                source_failures += int(e['value'] == -1)
                source_empty += int(e['value'] == 0)
                pstate = 'frame' if returned is not None else 'next' if e['value'] == 0 else 'terminal'
        elif pstate == 'frame':
            require(s == 'source.frame' and k == returned, 'source return/delivered identity mismatch')
            require(e['source'] in sources and k not in produced and k[3], 'wrong/duplicated source frame/clock')
            produced[k] = e
            candidate, pstate = k, 'admission'
        elif pstate == 'admission':
            require(s == 'admission.begin' and k == candidate and 0 <= e['value'] <= 32, 'admission begin/identity missing')
            admission_begins[k] = e
            pstate = 'outcome'
        elif pstate == 'outcome':
            require(s in ('admission.accepted','admission.failed') and k == candidate and 0 <= e['value'] <= 32, 'admission outcome/identity missing')
            (admitted if s == 'admission.accepted' else rejected)[k] = e
            pstate = 'next'
        else:
            raise Incomplete('source work after terminal next outcome')
    require(pstate in ('next','terminal'), 'incomplete producer stage')
    successful, failures, durable, cycles = [], [], [], []
    raw_artifacts, segment_acks = {}, []
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
        require(admission_begins[k]['ns'] <= pop_end['ns'] and pop_begin['ns'] <= pop_end['ns'], 'admission-entry/pop chronology')
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
                elif s in ('raw.segment.index', 'raw.segment.limit', 'raw.segment.bytes', 'raw.segment.records'):
                    require(e['value'] >= 0, 'invalid segment state')
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
                    first = aid not in raw_artifacts
                    state = raw_artifacts.setdefault(aid, {'index': 0, 'records': 0, 'bytes': 0, 'limit': None})
                    required = ['raw.provenance.begin','raw.provenance.end','raw.init.begin','raw.init.end'] if first else []
                    if state['records'] == 0:
                        required += ['raw.open.begin','raw.open.end','raw.directory.begin','raw.directory.end']
                    required += ['raw.segment.index', 'raw.segment.limit', 'raw.write.begin', 'raw.write.end',
                                 'raw.segment.bytes', 'raw.segment.records']
                    values = {e['stage']: e['value'] for e in store_events}
                    require(all(s in values for s in required), 'missing RawTrace stage')
                    require(values['raw.segment.index'] == state['index'], 'mispaired segment identity')
                    limit, size, records = (values['raw.segment.' + s] for s in ('limit', 'bytes', 'records'))
                    require(4096 <= limit <= 64 * 1024 * 1024 and state['limit'] in (None, limit), 'segment configuration mismatch')
                    require(size > state['bytes'] and records == state['records'] + 1, 'segment record/size discontinuity')
                    sealed = size >= limit or records >= 100000
                    if sealed:
                        required += ['raw.seal.begin', 'store.segment.durable', 'raw.seal.end']
                        require(values.get('store.segment.durable') == records, 'required segment seal/durable evidence missing')
                        segment_acks.append({'artifact': aid, 'index': state['index'], 'records': records,
                                             'bytes': size, 'last_key': list(k)})
                    require(names[3:-1] == required, 'missing/reordered RawTrace or conditional seal stage')
                    state.update(index=state['index'] + int(sealed), records=0 if sealed else records,
                                 bytes=0 if sealed else size, limit=limit)
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
    if raw_artifacts:
        require(type(segment_oracle) is dict, 'segment durability evidence UNAVAILABLE: independent Store oracle required')
        assert isinstance(segment_oracle, dict)
        require(segment_oracle.get('capture') == data['capture'] and segment_oracle.get('schema') == 2
                and set(raw_artifacts) == {segment_oracle.get('artifact')}, 'independent Store/capture identity mismatch')
        require(all(s['limit'] == segment_oracle.get('segment_max_bytes') for s in raw_artifacts.values()), 'independent Store configuration mismatch')
        require(segment_oracle.get('segments') == segment_acks, 'persisted segment/seal evidence mismatch')
    return {'status': 'COMPLETE_CHRONOLOGY', 'capture': data['capture'], 'build': data['build'],
            'pid': data['pid'], 'generation': data['generation'],
            'produced': len(produced), 'admitted': len(admitted), 'rejected': len(rejected),
            'source_failures': source_failures, 'source_empty_returns': source_empty,
            'callback_success': len(successful), 'callback_failure': len(failures),
            'schema1_durable_acknowledgements': len(durable),
            'segment_durable_acknowledgements': segment_acks,
            'segment_evidence': 'QUALIFIED: matched independent indexed segment scan' if raw_artifacts else 'NOT_APPLICABLE: schema-1',
            'recoverable_prefix': 'UNAVAILABLE: verify actual Store/replay separately',
            'residual_wall_time_attribution': 'UNKNOWN', 'cycles': cycles}

def collect(directory):
    """Inspect the union of markers/finals/partials, not just surviving markers.

    Absence of a WHOLE observation is detected separately by the runtime roster.
    """
    paths = list(Path(directory).iterdir())
    require(len(paths) <= 8 * 16 * 3, 'observation inventory bound')
    stems = set()
    for path in paths:
        require(re.fullmatch(r'[1-9][0-9]*-[1-8]\.(started|json|json\.part)', path.name), 'unexpected observer output')
        stems.add(path.name.split('.')[0])
    result = []
    for stem in sorted(stems):
        marker, final, partial = (Path(directory) / (stem + suffix) for suffix in ('.started', '.json', '.json.part'))
        identity = {'pid': int(stem.split('-')[0]), 'generation': int(stem.split('-')[1]),
                    'capture': None, 'marker_present': marker.is_file(), 'final_present': final.is_file()}
        try:
            require(marker.is_file() and marker.stat().st_size <= 65536, 'missing/oversize start marker')
            start = json.loads(marker.read_bytes())
            identity['capture'] = start['capture']
            require((start['pid'], start['generation']) == (identity['pid'], identity['generation']), 'filename/marker identity mismatch')
            require(not partial.exists(), 'partial serialization retained')
            summary = decode(load(final))
            summary.update(marker_present=True, final_present=True)
            result.append(summary)
        except (Incomplete, ValueError, KeyError, OSError, TypeError) as exc:
            result.append(dict(identity, status='INCOMPLETE', reason=str(exc)))
    return result


def require_owned_acceptance(summaries, exits, roster, observer_statuses):
    """Reconcile every actual Session; only its exact owned SIGKILL may lack a final."""
    require(isinstance(summaries, list) and 0 < len(summaries) <= 16, 'acceptance trace bound')
    require(isinstance(exits, list) and 0 < len(exits) <= 8, 'owned daemon bound')
    owned = {p['pid']: p['exit'] for p in exits}
    require(len(owned) == len(exits) and all(type(p) is int and p > 0 and type(rc) is int
            for p, rc in owned.items()), 'owned daemon not retired/identity unavailable')
    require(isinstance(roster, list) and 0 < len(roster) <= 16, 'actual Session roster unavailable')
    expected = {(r['pid'], r['capture']): r for r in roster}
    require(len(expected) == len(roster) and all(pid in owned and isinstance(capture, str) and capture
            and type(r.get('allow_missing_final')) is bool for (pid, capture), r in expected.items()), 'invalid/duplicate actual Session roster')
    actual = {(s.get('pid'), s.get('capture')) for s in summaries}
    require(len(actual) == len(summaries) and actual == set(expected), 'actual Session/observer roster mismatch')
    require(isinstance(observer_statuses, list) and not observer_statuses, 'retained UNAVAILABLE/INCOMPLETE observer outcome')
    for summary in summaries:
        require(summary.get('pid') in owned, 'trace from unowned daemon')
        entry = expected[(summary['pid'], summary['capture'])]
        require(summary.get('marker_present') is True, 'observer start marker unavailable')
        if summary['status'] != 'COMPLETE_CHRONOLOGY':
            require(summary['status'] == 'INCOMPLETE' and summary.get('final_present') is False,
                    'serialized observer evidence invalid/incomplete')
            require(owned[summary['pid']] == -9 and entry['allow_missing_final'], 'missing trace without exact owned capture SIGKILL')
        else:
            require(summary.get('final_present') is True and not entry['allow_missing_final'], 'killed Session falsely complete')
    require(any(s['status'] == 'COMPLETE_CHRONOLOGY' and s['callback_success'] > 0 for s in summaries),
            'no real completed Session/Store trace')
    require(any(s['status'] == 'INCOMPLETE' for s in summaries), 'hard-killed observer falsely complete')
    require(sum(r['allow_missing_final'] for r in roster) == 1, 'exact owned killed capture unavailable')

if __name__ == '__main__':
    import sys
    try:
        oracle = None
        if len(sys.argv) == 3:
            path = Path(sys.argv[2])
            require(path.is_file() and path.stat().st_size <= MAX_BYTES, 'missing/oversize Store oracle')
            oracle = json.loads(path.read_bytes())
        print(json.dumps(decode(load(sys.argv[1]), oracle), indent=2))
    except (Incomplete, OSError, KeyError, TypeError, ValueError) as exc:
        print('INCOMPLETE: ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
