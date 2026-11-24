"""Opt-in bounded RECORD-ONLY replay; never execute commands from corpus data."""
import argparse
import os
from pathlib import Path
import re
import sys

import ci_shadow as shadow
import ci_shadow_configured as configured
import ci_shadow_inventory as reader
import ci_shadow_policy as policy

MAX_CASES = 8
MAX_TOTAL_BYTES = 16_000_000
MAX_FILES = 8192
KINDS = ('controlled-fixture', 'causally-adjudicated', 'caller-assertion', 'historical-unadjudicated')
CONTEXT = 'event ref identities tested_commit configuration generator configured_manifest_sha256 test_inventory_sha256'


def check(ok, reason):
    if not ok:
        raise ValueError(reason)


def empty_result():
    return {'schema': 'ci110-impact-comparison-v1', 'status': 'rejected', 'errors': [], 'cases': [],
            'execution_authorized': False, 'closure_complete': False, 'full_matrix_would_select': True,
            'historical_causal_recall': 'UNKNOWN', 'product_closure': 'UNQUALIFIED',
            'limits': ['record-only-no-selection-or-execution', 'all-existing-final-gates-remain',
                       'caller-pins-and-adjudication-labels-are-not-authentication',
                       'failed-CI-observations-are-not-causal-ground-truth',
                       'no-historical-recall-safe-omission-speedup-or-product-closure-qualification',
                       'cooperative-input-snapshots-not-OS-isolation']}


class Snapshot:
    """Stable regular files plus a final whole-input recheck; no host isolation claim."""
    def __init__(self):
        self.files = {}
        self.total = 0

    def read(self, path, digest):
        check(configured.anchor(digest), 'corpus-missing-anchor')
        path = Path(path)
        check(path.is_absolute() and path == path.resolve(), 'corpus-unsafe-or-symlink-path')
        if path in self.files:
            check(self.files[path][0] == digest, 'corpus-conflicting-file-pin')
            return self.files[path][2]
        check(len(self.files) < MAX_FILES, 'corpus-file-limit')
        before = reader.identity(os.lstat(path))
        raw = reader.read_regular(path)
        check(before == reader.identity(os.lstat(path)), 'corpus-input-mutated')
        check(policy.digest(raw) == digest, 'corpus-file-digest-mismatch')
        self.total += len(raw)
        check(self.total <= MAX_TOTAL_BYTES, 'corpus-total-byte-limit')
        self.files[path] = (digest, before, raw)
        return raw

    def raw_reference(self, root, ref):
        policy.keys(ref, 'path sha256', 'corpus-reference-fields')
        check(configured.relative(ref['path']), 'corpus-unsafe-reference')
        return self.read(root / ref['path'], ref['sha256'])

    def reference(self, root, ref):
        return policy.strict_json(self.raw_reference(root, ref))

    def verify(self):
        for path, (digest, identity, _) in self.files.items():
            check(reader.identity(os.lstat(path)) == identity and
                  policy.digest(reader.read_regular(path)) == digest and
                  reader.identity(os.lstat(path)) == identity and path == path.resolve(),
                  'corpus-input-mutated')


def identities(acquisition):
    policy.keys(acquisition, 'event ref git_version identities tested_commit diff errors privileged_access '
                'fork_origin pr_trust_policy', 'corpus-acquisition-fields')
    check(acquisition['privileged_access'] is False and acquisition['fork_origin'] == 'UNKNOWN' and
          acquisition['pr_trust_policy'] == 'all-PR-inputs-untrusted' and
          configured.label(acquisition['git_version']), 'corpus-acquisition-trust')
    check(acquisition['errors'] == [] and acquisition['diff']['complete'] is True,
          'corpus-incomplete-acquisition')
    event, ids = acquisition['event'], acquisition['identities']
    check(event in ('push', 'pull_request') and isinstance(ids, dict) and
          set(ids) == ({'base', 'head', 'merge'} if event == 'pull_request' else {'base', 'head'}),
          'corpus-event-identities')
    for identity in ids.values():
        policy.keys(identity, 'commit tree parents', 'corpus-identity-fields')
        policy.identity({k: identity[k] for k in ('commit', 'tree')})
        check(isinstance(identity['parents'], list) and len(identity['parents']) <= 32 and
              all(policy.sha(p) for p in identity['parents']) and
              len(identity['parents']) == len(set(identity['parents'])), 'corpus-parent-identities')
    tested = ids['merge' if event == 'pull_request' else 'head']
    check(acquisition['tested_commit'] == tested['commit'], 'corpus-tested-identity')
    if event == 'pull_request':
        check(ids['merge']['parents'] == [ids['base']['commit'], ids['head']['commit']] and
              isinstance(acquisition['ref'], str) and
              re.fullmatch(r'refs/pull/[1-9][0-9]*/merge', acquisition['ref']), 'corpus-merge-context')
    else:
        check(isinstance(acquisition['ref'], str) and
              re.fullmatch(r'refs/heads/[A-Za-z0-9._/-]+', acquisition['ref']), 'corpus-push-context')
    diff = acquisition['diff']
    policy.keys(diff, 'complete encoding records sha256 bytes record_count base head', 'corpus-diff-fields')
    check(diff['encoding'] == 'git-raw-nul-v1' and diff['base'] == ids['base']['commit'] and
          diff['head'] == tested['commit'] and isinstance(diff['records'], list) and
          type(diff['record_count']) is int and diff['record_count'] == len(diff['records']) and
          len(diff['records']) <= shadow.MAX_CHANGES, 'corpus-diff-context')
    raw = bytearray()
    for row in diff['records']:
        policy.keys(row, 'status path old_path old_mode new_mode old_object new_object', 'corpus-change-fields')
        check(configured.relative(row['path']) and
              (row['old_path'] is None or configured.relative(row['old_path'])), 'corpus-change-path')
        check(all(isinstance(row[k], str) for k in ('status', 'old_mode', 'new_mode', 'old_object', 'new_object')),
              'corpus-change-type')
        raw.extend((':{} {} {} {} {}\0'.format(row['old_mode'], row['new_mode'], row['old_object'],
                                            row['new_object'], row['status'])).encode())
        if row['old_path'] is not None:
            raw.extend(row['old_path'].encode() + b'\0')
        raw.extend(row['path'].encode() + b'\0')
        check(len(raw) <= shadow.MAX_DIFF_BYTES, 'corpus-diff-size')
    check(shadow.parse_diff(bytes(raw)) == diff['records'] and type(diff['bytes']) is int and
          diff['bytes'] == len(raw) and policy.digest(raw) == diff['sha256'], 'corpus-diff-integrity')
    return {k: tested[k] for k in ('commit', 'tree')}


def validate_saved_decision(saved, source, inventory_bytes=None, inventory_digest=None):
    """Rebuild the supported collector request, not a parallel classifier.

    Legacy replay has no pinned static input; v2 supplies original bytes and an
    independent pin, never a serialized legacy projection. The configured graph
    is independently replayed below. Canonical comparison checks every nested
    field and distinguishes JSON booleans from integers (unlike dict equality).
    """
    check(saved['limits'] == ['all-required-native-local-and-CI-gates-remain',
          'local-ref-validation-is-not-provider-event-authentication',
          'historical-failure-recall-unknown', 'dependency-closure-unqualified'], 'corpus-shadow-limits')
    receipt = saved['inventory_acquisition']
    policy.keys(receipt, 'status error cleanup_errors', 'corpus-inventory-receipt-fields')
    check(receipt['status'] == ('regular-file' if inventory_bytes is not None else 'not-requested'),
          'corpus-static-inventory-evidence-unsupported')
    check(receipt['error'] is None and receipt['cleanup_errors'] == [], 'corpus-inventory-receipt-conflict')
    acquisition = saved['acquisition']
    base = {k: acquisition['identities']['base'][k] for k in ('commit', 'tree')}
    tested = acquisition['tested_commit']
    request = {'schema': 'ci110-request-v1', 'mode': 'shadow-feedback',
               'base': base, 'head': source, 'labels': [],
               'diff': {'base_commit': base['commit'], 'head_commit': tested, 'complete': True,
                        'shallow': False, 'truncated': False,
                        'reported_count': acquisition['diff']['record_count'],
                        'changes': [{k: r[k] for k in ('status', 'path', 'old_path')}
                                    for r in acquisition['diff']['records']],
                        'event': 'pr-merge' if acquisition['event'] == 'pull_request' else 'protected-push',
                        'ref': acquisition['ref'], 'expected_head': tested,
                        'fork': acquisition['event'] == 'pull_request', 'privilege': 'none'}}
    # classify also preserves the legitimate empty-diff/missing-inventory errors.
    # Do not require an optional inventory, authenticated source or full closure.
    expected = policy.classify(policy.canonical(request), inventory_bytes, inventory_digest)
    check(policy.canonical(saved['decision']) == policy.canonical(expected),
          'corpus-decision-acquisition-mismatch')


def compare_case(snapshot, root, case, version='ci110-impact-corpus-v1'):
    extended = version == 'ci110-impact-corpus-v2'
    policy.keys(case, 'id context shadow configured_inputs observation' +
                (' static_inventory' if extended else ''), 'corpus-case-fields')
    check(configured.label(case['id']), 'corpus-case-id')
    policy.keys(case['context'], CONTEXT, 'corpus-context-fields')
    saved = snapshot.reference(root, case['shadow'])
    policy.keys(saved, 'schema shadow_only execution_authorized acquisition decision limits '
                'inventory_acquisition configured_diagnostic', 'corpus-shadow-fields')
    check(saved['schema'] == 'mantis-git-shadow-v2' and saved['shadow_only'] is True and
          saved['execution_authorized'] is False and saved['decision']['full_matrix_would_select'] is True and
          saved['decision']['schema'] == policy.SCHEMA and saved['decision']['policy_version'] == 'shadow-1' and
          saved['decision']['shadow_only'] is True and saved['decision']['activation_qualified'] is False and
          saved['decision']['execution_authorized'] is False, 'corpus-shadow-version-or-authority')
    acquisition = saved['acquisition']
    source = identities(acquisition)
    inventory_bytes, inventory_digest = None, None
    if extended and case['static_inventory'] is not None:
        inventory_bytes = snapshot.raw_reference(root, case['static_inventory'])
        inventory_digest = case['static_inventory']['sha256']
    validate_saved_decision(saved, source, inventory_bytes, inventory_digest)
    inputs = snapshot.reference(root, case['configured_inputs'])
    # Freeze the entire pinned producer closure before replay, then independently
    # re-read it via the unchanged importer. No command/URI in that data is run.
    policy.keys(inputs, 'schema source roots configuration generator index ctest files', 'corpus-configured-fields')
    policy.keys(inputs['roots'], 'source build', 'corpus-configured-roots')
    check(isinstance(inputs['files'], list) and 0 < len(inputs['files']) <= configured.MAX_FILES,
          'corpus-configured-file-limit')
    for row in inputs['files']:
        policy.keys(row, 'root path sha256', 'corpus-configured-pin')
        check(row['root'] in ('source', 'build') and configured.relative(row['path']), 'corpus-configured-path')
        snapshot.read(Path(inputs['roots'][row['root']]) / row['path'], row['sha256'])
    diagnostic = configured.diagnose(root / case['configured_inputs']['path'],
                                    case['configured_inputs']['sha256'], source, acquisition['diff']['records'])
    check(diagnostic['status'] == 'ingested', 'corpus-configured-evidence-rejected')
    check(policy.canonical(saved['configured_diagnostic']) == policy.canonical(diagnostic),
          'corpus-stale-or-mismatched-shadow')
    context = {k: acquisition[k] for k in ('event', 'ref', 'identities', 'tested_commit')}
    context.update(configuration=diagnostic['configuration'], generator=diagnostic['generator'],
                   configured_manifest_sha256=diagnostic['manifest_sha256'],
                   test_inventory_sha256=policy.digest(policy.canonical(diagnostic['tests'])))
    check(case['context'] == context, 'corpus-evidence-context-mismatch')
    observation = snapshot.reference(root, case['observation'])
    policy.keys(observation, 'schema context kind origin trust test_ids basis', 'corpus-observation-fields')
    check(observation['schema'] == 'ci110-impact-observation-v1', 'corpus-observation-version')
    check(observation['context'] == context, 'corpus-observation-context-mismatch')
    check(observation['kind'] in KINDS and configured.label(observation['origin']) and
          configured.label(observation['basis']) and observation['trust'] == 'caller-asserted-unverified',
          'corpus-observation-origin-or-trust')
    all_tests = {t['id'] for t in diagnostic['tests']}
    observed = observation['test_ids']
    check(observed is None or (isinstance(observed, list) and len(observed) <= configured.MAX_RECORDS and
          all(configured.label(t) for t in observed) and len(observed) == len(set(observed)) and
          set(observed) <= all_tests), 'corpus-unknown-or-duplicate-test')
    impacted = set(diagnostic['impacted_test_ids'])
    expected = set(observed or [])
    hits, misses = sorted(expected & impacted), sorted(expected - impacted)
    historical = observation['kind'] == 'historical-unadjudicated'
    comparison = {'hits': hits, 'misses': misses, 'denominator': len(expected) if expected else None,
                  'state': 'UNKNOWN' if not expected else 'MISS' if misses else 'HIT'}
    result = {'id': case['id'], 'context': context, 'kind': observation['kind'],
            'origin': observation['origin'], 'basis': observation['basis'], 'trust': observation['trust'],
            'evidence_sha256': {name: case[name]['sha256'] for name in ('shadow', 'configured_inputs', 'observation')},
            'diagnostic': comparison, 'diagnostic_impacted_test_ids': sorted(impacted),
            'full_would_select_test_ids': sorted(all_tests),
            'full_observation_coverage': {'hits': sorted(expected), 'misses': [],
                                        'state': 'COUNTERFACTUAL_FULL' if expected else 'UNKNOWN'},
            'causal_denominator': len(expected) if expected and observation['kind'] in
                                  ('controlled-fixture', 'causally-adjudicated') else None,
            'reason': 'unadjudicated-observation-overlap-not-causal-recall' if historical else
                      'no-known-nonempty-expectation' if not expected else
                      'diagnostic-edge-miss-despite-unconditional-FULL' if misses else 'diagnostic-expectation-hit',
            'unknowns': diagnostic['unknowns']}
    if extended:
        result['evidence_sha256']['static_inventory'] = inventory_digest
        # Compared means consistent replay, not policy success. Bind the entire
        # canonical decision and retain its fixed errors, without re-exporting
        # caller-authored inventory provenance/origin strings from the policy.
        result['static_inventory_replay'] = {
            'decision_sha256': policy.digest(policy.canonical(saved['decision'])),
            'decision_errors': saved['decision']['errors']
        } if inventory_bytes is not None else None
    return result


def compare(path, expected_digest):
    result = empty_result()
    try:
        snapshot = Snapshot()
        path = Path(path).absolute()
        corpus = policy.strict_json(snapshot.read(path, expected_digest))
        policy.keys(corpus, 'schema cases', 'corpus-fields')
        check(corpus['schema'] in ('ci110-impact-corpus-v1', 'ci110-impact-corpus-v2'), 'corpus-version')
        check(isinstance(corpus['cases'], list) and len(corpus['cases']) <= MAX_CASES, 'corpus-case-limit')
        cases, seen = [], set()
        for case in corpus['cases']:
            current = compare_case(snapshot, path.parent, case, corpus['schema'])
            check(current['id'] not in seen, 'corpus-duplicate-case')
            seen.add(current['id'])
            cases.append(current)
        snapshot.verify()
        result.update(status='compared', corpus_sha256=expected_digest, cases=sorted(cases, key=lambda c: c['id']))
        if corpus['schema'] == 'ci110-impact-corpus-v2':
            result['schema'] = 'ci110-impact-comparison-v2'
        # Never pool fixture/assertion denominators with historical CI observations.
        result['groups'] = {kind: {'cases': sum(c['kind'] == kind for c in cases),
            'hits': sum(len(c['diagnostic']['hits']) for c in cases if c['kind'] == kind),
            'misses': sum(len(c['diagnostic']['misses']) for c in cases if c['kind'] == kind),
            'causal_recall': 'UNKNOWN', 'coverage_claim': 'NONE'} for kind in KINDS}
        check(len(policy.canonical(result)) <= shadow.MAX_INVENTORY_BYTES, 'corpus-output-limit')
    except ValueError as exc:
        result = empty_result()
        # ValueError strings from strict parsers are locally defined codes only.
        result['errors'] = [str(exc) if re.fullmatch(r'[a-z][a-z0-9-]{0,100}', str(exc)) else 'corpus-invalid-input']
    except (reader.ReadError, OSError, KeyError, TypeError, AttributeError, IndexError,
            RecursionError, shadow.AcquisitionError):
        result = empty_result()
        result['errors'] = ['corpus-invalid-missing-or-mutated-evidence']
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-sha256', required=True)
    args = parser.parse_args(argv)
    worker = ('import sys; sys.path.insert(0, sys.argv.pop(1)); import ci_shadow_corpus as c; '
              'sys.stdout.buffer.write(c.policy.canonical(c.compare(sys.argv[1], sys.argv[2])))')
    command = [sys.executable, '-I', '-B', '-c', worker, str(Path(__file__).resolve().parent),
               str(args.corpus), args.corpus_sha256]
    try:
        raw = shadow._run_owned(command, dict(os.environ), limit=shadow.MAX_INVENTORY_BYTES,
                                seconds=shadow.COMMAND_SECONDS, kind='corpus')
    except shadow.AcquisitionError as exc:
        result = empty_result()
        result['errors'] = [str(exc), *exc.cleanup_errors]
        raw = policy.canonical(result)
    sys.stdout.buffer.write(raw)
    return 0  # Emitted evidence, never qualification/admission.


if __name__ == '__main__':
    raise SystemExit(main())
