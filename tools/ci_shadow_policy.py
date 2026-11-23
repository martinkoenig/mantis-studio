"""Full-matrix shadow policy and known-edge impact reporting (Issue110).

Adapted from the accepted inert prototype, SHA256
72274f2134acfacb9bca47f8abc51e2c461c949a64544974ea0977aff505a70a.
Known edges are diagnostic only: configured closure and corpus recall remain
unqualified. This module cannot admit a reduced test selection.
"""
import hashlib
import json
import re

SCHEMA = 'ci110-decision-v1'
PROFILES = [
    {'id': 'x86_64-on', 'architecture': 'x86_64', 'job': 'linux (ubuntu-24.04, ON)', 'mode': 'on'},
    {'id': 'x86_64-off', 'architecture': 'x86_64', 'job': 'linux (ubuntu-24.04, OFF)', 'mode': 'off'},
    {'id': 'aarch64-on', 'architecture': 'aarch64', 'job': 'linux (ubuntu-24.04-arm, ON)', 'mode': 'on'},
    {'id': 'aarch64-off', 'architecture': 'aarch64', 'job': 'linux (ubuntu-24.04-arm, OFF)', 'mode': 'off'},
    {'id': 'x86_64-san', 'architecture': 'x86_64', 'job': 'sanitizers', 'mode': 'san'},
]
JOB_IDS = sorted(p['id'] for p in PROFILES)
KINDS = {'docs', 'implementation', 'qml', 'daemon', 'shared-header', 'protobuf',
         'qt-model', 'abi', 'storage', 'policy', 'build', 'security', 'unknown'}
EXPAND = {'shared-header', 'protobuf', 'qt-model', 'abi', 'storage', 'policy',
          'build', 'security', 'unknown'}
FULL_EXTRAS = ['existing-agent-efficiency-workflow-when-applicable',
               'compiler-cache-policy', 'headless-lsan-detector-controls',
               'UI-repeats-both-native-ON', 'applicable-local-final-suites']


def canonical(value):
    return (json.dumps(value, sort_keys=True, ensure_ascii=True, indent=2) + '\n').encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def keys(obj, expected, reason):
    require(isinstance(obj, dict) and set(obj) == set(expected.split()), reason)


def unique_strings(seq):
    return (isinstance(seq, list) and all(isinstance(s, str) and
            re.fullmatch(r'[A-Za-z0-9_.:/+ -]{1,180}', s) for s in seq)
            and len(seq) == len(set(seq)))


def path_ok(path):
    return (isinstance(path, str) and 0 < len(path) <= 300 and
            not path.startswith('/') and '\\' not in path and
            all(part not in ('', '.', '..') for part in path.split('/')) and
            all(32 <= ord(c) < 127 for c in path))


def sha(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{40}', value) is not None


def strict_json(raw):
    require(isinstance(raw, bytes) and len(raw) <= 2_000_000, 'input-size-or-type')
    def pairs(items):
        result = {}
        for k, v in items:
            require(k not in result, 'duplicate-json-key')
            result[k] = v
        return result
    def constant(_):
        raise ValueError('nonfinite-json')
    try:
        return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError):
        raise ValueError('invalid-json') from None


def identity(obj):
    keys(obj, 'commit tree', 'identity-fields')
    require(sha(obj['commit']) and sha(obj['tree']), 'invalid-identity')


def validate_inventory(inv):
    keys(inv, 'schema scope source closure files targets tests profiles provenance', 'inventory-fields')
    require(inv['schema'] == 'ci110-inventory-v1', 'inventory-version')
    require(inv['scope'] == 'reused107', 'inventory-scope')
    identity(inv['source'])
    require(inv['profiles'] == PROFILES, 'required-profile-mismatch')
    keys(inv['closure'], 'complete unknowns', 'closure-fields')
    require(type(inv['closure']['complete']) is bool and
            unique_strings(inv['closure']['unknowns']), 'closure-types')
    require(not inv['closure']['complete'] or not inv['closure']['unknowns'], 'conflicting-closure')
    require(unique_strings(inv['provenance']) and inv['provenance'], 'inventory-provenance')
    targets, tests, files = inv['targets'], inv['tests'], inv['files']
    require(isinstance(targets, dict) and targets and unique_strings(list(targets)), 'missing-target-inventory')
    require(isinstance(tests, list) and tests and isinstance(files, dict) and files, 'missing-test-or-file-inventory')
    ids = []
    for t in tests:
        keys(t, 'id targets domains risk tier profiles origin', 'test-fields')
        require(unique_strings([t['id']]) and unique_strings(t['targets']) and
                set(t['targets']) <= set(targets), 'test-target-reference')
        require(unique_strings(t['domains']) and t['domains'] and t['risk'] in ('R1','R2','R3')
                and t['tier'] == 'exclusive' and unique_strings(t['profiles']) and
                t['profiles'] and set(t['profiles']) <= set(JOB_IDS) and
                isinstance(t['origin'], str) and 0 < len(t['origin']) < 300, 'test-metadata')
        ids.append(t['id'])
    require(len(ids) == len(set(ids)), 'duplicate-test-id')
    for target, info in targets.items():
        keys(info, 'dependencies sources origin', 'target-fields')
        require(unique_strings(info['dependencies']) and set(info['dependencies']) <= set(targets)
                and isinstance(info['sources'], list) and all(path_ok(p) for p in info['sources'])
                and set(info['sources']) <= set(files), 'target-edge-reference')
        require(isinstance(info['origin'], str) and 0 < len(info['origin']) < 300, 'target-provenance')
    for path, info in files.items():
        require(path_ok(path), 'unsafe-path')
        keys(info, 'targets tests domains risk kind origin', 'file-fields')
        require(unique_strings(info['targets']) and set(info['targets']) <= set(targets)
                and unique_strings(info['tests']) and set(info['tests']) <= set(ids), 'file-edge-reference')
        require(unique_strings(info['domains']) and info['domains'] and info['risk'] in ('R1','R2','R3')
                and info['kind'] in KINDS and isinstance(info['origin'], str)
                and 0 < len(info['origin']) < 300, 'file-metadata')
    # Reciprocal authored source edges catch one-sided corrupt imports.
    for target, info in targets.items():
        require(all(target in files[p]['targets'] for p in info['sources']), 'source-edge-asymmetry')
    return inv


def validate_request(req):
    keys(req, 'schema mode base head diff labels', 'request-fields')
    require(req['schema'] == 'ci110-request-v1' and req['mode'] in
            ('shadow-final', 'shadow-feedback'), 'request-version-or-mode')
    identity(req['base']); identity(req['head'])
    require(unique_strings(req['labels']), 'invalid-labels')
    d = req['diff']
    keys(d, 'base_commit head_commit complete shallow truncated reported_count changes event ref expected_head fork privilege', 'diff-fields')
    require(d['base_commit'] == req['base']['commit'] and d['head_commit'] == req['head']['commit']
            and d['expected_head'] == req['head']['commit'], 'diff-identity-mismatch')
    require(all(type(d[k]) is bool for k in ('complete','shallow','truncated','fork')), 'diff-boolean-type')
    require(d['complete'] and not d['shallow'] and not d['truncated'], 'incomplete-diff')
    require(d['privilege'] == 'none', 'privileged-input-rejected')
    require((d['event'] == 'pr-merge' and isinstance(d['ref'],str) and
             re.fullmatch(r'refs/pull/[1-9][0-9]*/merge', d['ref'])) or
            (d['event'] == 'protected-push' and isinstance(d['ref'],str) and re.fullmatch(r'refs/heads/[A-Za-z0-9._/-]+',d['ref']) and not d['fork']),
            'event-ref-mismatch')
    require(isinstance(d['changes'],list) and type(d['reported_count']) is int and
            0 < len(d['changes']) == d['reported_count'] <= 10000, 'diff-count-or-empty')
    seen = set()
    for c in d['changes']:
        keys(c, 'status path old_path', 'change-fields')
        require(path_ok(c['path']) and c['path'] not in seen, 'invalid-or-duplicate-path')
        seen.add(c['path'])
        require(isinstance(c['status'],str) and re.fullmatch(r'M|A|D|T|U|R[0-9]{1,3}|C[0-9]{1,3}',c['status'])
                and (c['old_path'] is None or path_ok(c['old_path'])), 'invalid-change-kind')
    require(not {'docs-only','runtime'} <= set(req['labels']), 'conflicting-labels')
    return req


def forced_kind(path, declared):
    # Broad patterns only EXPAND; no prefix alone permits a reduction.
    if (path in {'AGENTS.md','BUILDING.md','CONTRIBUTING.md','ARCHITECTURE.md',
                 'MANTIS_STUDIO_ARCHITECTURE.md','.gitmodules','CMakePresets.json'} or
            path.startswith(('docs/adr/','docs/engineering/','docs/architecture/','docs/agents/'))):
        return 'policy'
    if (path.endswith('CMakeLists.txt') or path.endswith('.cmake') or
            path.startswith(('.github/','cmake/','tools/','scripts/'))):
        return 'build'
    if path.endswith(('.h','.hpp','.hh','.hxx','.inl')):
        return 'shared-header'
    if path.endswith('.proto'):
        return 'protobuf'
    return declared


def closure(inv, changes):
    """Traverse dependency -> consumer, then every affected target -> test."""
    reasons = {}
    direct_tests = set()
    for c in changes:
        f = inv['files'].get(c['path'])
        if f:
            direct_tests.update(f['tests'])
            for target in f['targets']:
                reasons.setdefault(target, []).append('source:' + c['path'])
    # Index each edge once; discover each consumer once, independent of names.
    consumers = {target: [] for target in inv['targets']}
    for target, info in inv['targets'].items():
        for dependency in info['dependencies']:
            consumers[dependency].append(target)
    reached = set(reasons)
    pending = list(reasons)
    while pending:
        for target in consumers[pending.pop()]:
            if target not in reached:
                reached.add(target)
                pending.append(target)
    # Report all reached immediate dependencies, not the order-dependent subset
    # visible on the first scan. Direct source reasons retain their precedence.
    for target in reached:
        if target not in reasons:
            reasons[target] = ['depends-on:' + dependency for dependency in
                               sorted(d for d in inv['targets'][target]['dependencies'] if d in reached)]
    impacted = direct_tests | {t['id'] for t in inv['tests'] if any(target in reached for target in t['targets'])}
    return reasons, impacted


def classify(request_bytes, inventory_bytes, expected_inventory_sha256):
    """Total fail-closed decision boundary for bounded JSON inputs.

    Exit status zero means a manifest was emitted, never that validation passed.
    Unknown inventory means full profile identities with NO fabricated test IDs.
    """
    errors, req, inv = [], None, None
    try:
        require(isinstance(expected_inventory_sha256, str) and
                re.fullmatch('[0-9a-f]{64}', expected_inventory_sha256), 'missing-inventory-anchor')
        require(isinstance(inventory_bytes, bytes) and digest(inventory_bytes) == expected_inventory_sha256,
                'inventory-digest-mismatch')
        inv = validate_inventory(strict_json(inventory_bytes))
    except (ValueError, TypeError, KeyError, RecursionError) as exc:
        # Never retain partially validated input or echo untrusted input content.
        inv = None
        errors.append('inventory-' + str(exc) if isinstance(exc,ValueError) else 'malformed-inventory')
    try:
        req = validate_request(strict_json(request_bytes))
    except (ValueError, TypeError, KeyError, RecursionError) as exc:
        req = None
        errors.append(str(exc) if isinstance(exc, ValueError) else 'malformed-request')
    target_reasons, impacted, domains, risks = {}, set(), set(), set()
    expansions = []
    if req and any(c['status'] != 'M' or c['old_path'] is not None for c in req['diff']['changes']):
        errors.append('unsupported-change-kind')
    if inv and req:
        if inv['source'] != req['base']:
            errors.append('inventory-base-identity-mismatch')
        errors.append('real-input-qualification-unavailable')
        if not inv['closure']['complete']:
            errors.append('dependency-closure-incomplete')
        target_reasons, impacted = closure(inv, req['diff']['changes'])
        for c in req['diff']['changes']:
            f = inv['files'].get(c['path'])
            if not f:
                errors.append('unknown-changed-path')
                continue
            domains.update(f['domains']); risks.add(f['risk'])
            kind = forced_kind(c['path'], f['kind'])
            if kind == 'unknown':
                errors.append('unknown-change-kind')
            if kind in EXPAND:
                expansions.append(kind)
            if kind != 'docs' and not f['targets'] and not f['tests']:
                errors.append('unmapped-runtime-change')
        for t in inv['tests']:
            if t['id'] in impacted:
                domains.update(t['domains']); risks.add(t['risk'])
    # All collected decisions retain all gates, including malformed/empty input.
    full = True
    tests = inv['tests'] if inv else []
    selected_tests = {t['id'] for t in tests} if full else impacted
    selected_profiles = set(JOB_IDS) if full else {p for t in tests if t['id'] in impacted for p in t['profiles']}
    docs = bool(req and inv and any(inv['files'].get(c['path'],{}).get('kind') == 'docs'
                                   for c in req['diff']['changes']))
    selection_reason = ('fail-closed-full-matrix' if errors else 'risk-expansion-full-matrix'
                        if expansions else 'final-mode-full-matrix' if full else 'synthetic-dependency-union')
    return {
        'schema': SCHEMA, 'policy_version': 'shadow-1', 'shadow_only': True,
        'execution_authorized': False, 'activation_qualified': False,
        'source': {'base': req['base'], 'head': req['head']} if req else None,
        'mode': req['mode'] if req else 'untrusted',
        'input_sha256': {'request': digest(request_bytes) if isinstance(request_bytes,bytes) else None,
                         'inventory': digest(inventory_bytes) if isinstance(inventory_bytes,bytes) else None,
                         'expected_inventory': expected_inventory_sha256 if isinstance(expected_inventory_sha256,str)
                         and re.fullmatch('[0-9a-f]{64}',expected_inventory_sha256) else None},
        'diff': req['diff'] if req else None,
        'inventory': {'scope': inv['scope'], 'source': inv['source'], 'closure': inv['closure'],
                      'provenance': inv['provenance']} if inv else None,
        'unknown_or_error': bool(errors), 'errors': sorted(set(errors)),
        'expansion_reasons': sorted(set(expansions)), 'selection_reason': selection_reason,
        'full_matrix_would_select': full, 'domains': sorted(domains),
        'risk': 'R1' if full else 'R1' if 'R1' in risks else 'R2' if 'R2' in risks else 'R3',
        'resource_tier': 'exclusive', 'target_reasons': dict(sorted(target_reasons.items())),
        'impacted_test_ids': sorted(impacted), 'would_select_test_ids': sorted(selected_tests),
        'test_decisions': [{'id':t['id'], 'selected': t['id'] in selected_tests,
                            'reason': selection_reason if full else 'dependency-impact' if t['id'] in impacted
                            else 'synthetic-complete-closure-no-impact', 'targets':t['targets'],
                            'profiles':t['profiles'], 'domains':t['domains'], 'risk':t['risk'],
                            'resource_tier':t['tier'], 'origin':t['origin']} for t in sorted(tests,key=lambda t:t['id'])],
        'suite_decisions': [{'id': p['id'], 'job':p['job'], 'architecture':p['architecture'],
                             'selected': p['id'] in selected_profiles,
                             'reason': selection_reason if full else 'impacted-test-profile' if p['id'] in selected_profiles
                             else 'synthetic-complete-closure-no-impact'} for p in PROFILES] +
                           [{'id':'docs-review', 'selected':docs or full, 'reason':'docs-or-full-policy-review'}],
        'full_gate_obligations': {'profiles':PROFILES, 'additional':FULL_EXTRAS,
                                 'required_always':True, 'pr_and_protected_push':True,
                                 'independent_exact_sha_review':True},
        'limits': ['synthetic-results-are-not-corpus-recall', 'no-current-configured-input-qualification',
                   'no-runtime-or-hardware-proof', 'no-selected-test-execution',
                   'digest-does-not-establish-evidence-authenticity'],
    }
