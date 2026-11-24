"""Independent causal and conditional-stage controls on real retained inputs."""
import copy
from decoder import decode, Incomplete


def rejected(data, reason, oracle=None):
    try:
        if oracle is None:
            decode(data)
        else:
            decode(data, oracle)
    except Incomplete as exc:
        return {'control': reason, 'status': 'REJECTED', 'diagnostic': str(exc)}
    raise AssertionError('invalid evidence accepted: ' + reason)


def causal_controls(golden):
    results = []
    bad = copy.deepcopy(golden)
    next(e for e in bad['events'] if e['stage'] == 'source.next.end')['key'][0] += 1
    results.append(rejected(bad, 'returned frame disagrees with delivered source frame'))
    bad = copy.deepcopy(golden)
    key = tuple([e for e in bad['events'] if e['stage'] == 'source.frame'][-1]['key'])
    end = next(e['ns'] for e in bad['events'] if e['stage'] == 'cycle.end' and tuple(e['key']) == key)
    producer = [e for e in bad['events'] if e['lane'] == 1]
    begin = next(i for i, e in enumerate(producer) if e['stage'] == 'admission.begin' and tuple(e['key']) == key)
    for i, e in enumerate(producer[begin:]):
        e['ns'] = end + 1 + i
    cleanup = next(e for e in bad['events'] if e['stage'] == 'cleanup.end')
    cleanup['ns'] = max(cleanup['ns'], producer[-1]['ns'] + 1)
    results.append(rejected(bad, 'admission begins after completed writer cycle'))
    # Enqueue linearizes inside push_for; the producer's OUTCOME clock read may
    # happen after the writer already popped/processed it. Do not reject that.
    valid = copy.deepcopy(golden)
    producer = [e for e in valid['events'] if e['lane'] == 1]
    begin = next(i for i, e in enumerate(producer) if e['stage'] == 'admission.accepted' and tuple(e['key']) == key)
    for i, e in enumerate(producer[begin:]):
        e['ns'] = max(e['ns'], end + 1 + i)
    cleanup = next(e for e in valid['events'] if e['stage'] == 'cleanup.end')
    cleanup['ns'] = max(cleanup['ns'], producer[-1]['ns'] + 1)
    assert decode(valid)['status'] == 'COMPLETE_CHRONOLOGY'
    results.append({'control': 'admission outcome after consumption is legal', 'status': 'ACCEPTED'})
    return results


def segment_controls(golden, oracle):
    results = []
    bad = copy.deepcopy(golden)
    bad['events'] = [e for e in bad['events'] if e['stage'] not in ('raw.seal.begin', 'raw.seal.end', 'store.segment.durable')]
    ordinals = [0, 0, 0]
    for e in bad['events']:
        e['ordinal'] = ordinals[e['lane']]
        ordinals[e['lane']] += 1
    results.append(rejected(bad, 'all actual segment seal groups never emitted', oracle))
    bad = copy.deepcopy(golden)
    next(e for e in bad['events'] if e['stage'] == 'raw.segment.index')['value'] += 1
    results.append(rejected(bad, 'mispaired segment index', oracle))
    bad_oracle = copy.deepcopy(oracle)
    bad_oracle['segments'][0]['last_key'][0] += 1
    results.append(rejected(golden, 'wrong independent persisted segment identity', bad_oracle))
    results.append(rejected(golden, 'persisted segment oracle unavailable'))
    return results


if __name__ == '__main__':
    import sys
    from decoder import load
    print(causal_controls(load(sys.argv[1])))
