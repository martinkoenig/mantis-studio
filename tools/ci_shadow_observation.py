"""Publish a narrow static-classifier observation, never a CI selection instruction."""
import hashlib
import os
from pathlib import Path
import re
import sys

import ci_shadow as shadow


OUTPUT = Path('build/ci-shadow/observation.json')
PROFILES = {
    'ubuntu-24.04-ON': ('linux', 'x86_64-on'),
    'ubuntu-24.04-OFF': ('linux', 'x86_64-off'),
    'ubuntu-24.04-arm-ON': ('linux', 'aarch64-on'),
    'ubuntu-24.04-arm-OFF': ('linux', 'aarch64-off'),
    'ubuntu-24.04-san': ('sanitizers', 'x86_64-san'),
}
# Only fixed collector diagnostics may cross the publication boundary. Unknown
# future diagnostics fail publication, rather than leaking exception/input text.
ACQUISITION_ERRORS = frozenset('''
unsupported-event event-ref-mismatch shallow-repository immutable-sha1-required
endpoint-not-commit invalid-tree-object commit-tree-mismatch invalid-parent-record
merge-parent-mismatch unexpected-merge-identity invalid-ref-object event-ref-missing
conflicting-event-refs event-ref-sha-mismatch checkout-sha-mismatch
unterminated-nul-diff malformed-raw-diff missing-diff-path missing-rename-target
invalid-git-path invalid-old-git-path diff-record-limit invalid-git-encoding
unsupported-path-encoding empty-or-duplicate-diff-path
submodule-change-unqualified event-ref-changed-during-acquisition
checkout-changed-during-acquisition unsupported-command-containment-platform
owned-leader-status-unavailable owned-leader-identity-lost owned-group-signal-failed
owned-leader-retirement-unconfirmed owned-group-reap-failed
owned-group-retirement-unconfirmed owned-handle-close-failed
 git-command-timeout git-output-limit git-diagnostic-limit git-command-failed
 git-reported-diagnostic git-unavailable-or-inaccessible
'''.split())
POLICY_ERRORS = frozenset(('inventory-missing-inventory-anchor', 'invalid-json',
                           'diff-count-or-empty', 'invalid-or-duplicate-path',
                           'invalid-change-kind', 'unsupported-change-kind'))


def require(ok):
    if not ok:
        raise ValueError('observation-invalid-or-unsafe')


def sha(value):
    require(shadow.policy.sha(value))
    return value


def number(value):
    require(isinstance(value, str) and re.fullmatch(r'[1-9][0-9]{0,19}', value))
    return value


def errors(values, allowed):
    require(isinstance(values, list) and len(values) <= 64
            and all(isinstance(v, str) and v in allowed for v in values))
    return values.copy()


def identity(value):
    require(isinstance(value, dict) and set(value) == {'commit', 'tree', 'parents'})
    require(isinstance(value['parents'], list) and len(value['parents']) <= 64)
    return {'commit': sha(value['commit']), 'tree': sha(value['tree']),
            'parents': [sha(p) for p in value['parents']]}


def context(env):
    event = env['GITHUB_EVENT_NAME']
    require(event in ('pull_request', 'push', 'workflow_dispatch'))
    profile = env['SHADOW_PROFILE']
    require(profile in PROFILES and env['GITHUB_JOB'] == PROFILES[profile][0])
    tested = sha(env['GITHUB_SHA'])
    base = env.get('SHADOW_BASE', '')
    head = env.get('SHADOW_HEAD', '') if event == 'pull_request' else tested
    # Missing event evidence stays missing; do not replace it with HEAD/parents.
    require(not base or shadow.policy.sha(base))
    require(not head or shadow.policy.sha(head))
    if event == 'pull_request':
        require(re.fullmatch(r'refs/pull/[1-9][0-9]{0,19}/merge', env['GITHUB_REF']))
        merge = tested
    else:
        require(env['GITHUB_REF'].startswith('refs/heads/'))
        require(not head or head == tested)
        merge = None
    return {'event': event, 'base': base or None, 'head': head or None,
            'merge': merge, 'tested_commit': tested,
            'ref_sha256': hashlib.sha256(env['GITHUB_REF'].encode()).hexdigest(),
            'run_id': number(env['GITHUB_RUN_ID']),
            'run_attempt': number(env['GITHUB_RUN_ATTEMPT']),
            'job': env['GITHUB_JOB'], 'profile': PROFILES[profile][1],
            'trust': 'event-context-assertion-not-provider-authentication'}


def project(result, asserted):
    """Construct the entire public document from typed, closed allowlists.

    Never copy diff paths, inventory strings, raw Git/version diagnostics, commands,
    environment, cache or configured inputs. Static invocation requests no inventory.
    """
    require(result['schema'] == 'mantis-git-shadow-v1'
            and result['shadow_only'] is True and result['execution_authorized'] is False)
    a, d = result['acquisition'], result['decision']
    require(a['event'] == asserted['event'] and set(a['identities']) <= {'base', 'head', 'merge'})
    checked = {key: identity(value) for key, value in a['identities'].items()}
    for key, value in checked.items():
        require(value['commit'] == asserted[key])
    tested = a['tested_commit']
    require(tested is None or sha(tested) == asserted['tested_commit'])
    diff = a['diff']
    require(type(diff['complete']) is bool)
    for key in ('bytes', 'record_count'):
        require(diff[key] is None or type(diff[key]) is int and 0 <= diff[key] <= shadow.MAX_DIFF_BYTES)
    digest = diff['sha256']
    require(digest is None or isinstance(digest, str) and re.fullmatch('[0-9a-f]{64}', digest))
    require(d['schema'] == 'ci110-decision-v1' and d['shadow_only'] is True
            and d['execution_authorized'] is False and d['activation_qualified'] is False
            and d['full_matrix_would_select'] is True and d['unknown_or_error'] is True
            and d['inventory'] is None and not d['impacted_test_ids'] and not d['test_decisions']
            and d['selection_reason'] == 'fail-closed-full-matrix')
    require(d['mode'] in ('shadow-feedback', 'untrusted'))
    expected = [dict(id=p['id'], job=p['job'], architecture=p['architecture'],
                     selected=True, reason='fail-closed-full-matrix') for p in shadow.policy.PROFILES]
    expected.append(dict(id='docs-review', selected=True, reason='docs-or-full-policy-review'))
    require(shadow.policy.canonical(d['suite_decisions']) == shadow.policy.canonical(expected))
    return {'schema': 'ci110-workflow-observation-v1', 'asserted_context': asserted,
            'acquisition': {'identities': checked, 'tested_commit': tested,
                            'diff': {k: diff[k] for k in ('complete', 'sha256', 'bytes', 'record_count')},
                            'errors': errors(a['errors'], ACQUISITION_ERRORS)},
            'decision': {'mode': d['mode'], 'unknown_or_error': True,
                         'errors': errors(d['errors'], POLICY_ERRORS),
                         'selection_reason': 'fail-closed-full-matrix',
                         'suite_decisions': expected, 'full_matrix_would_select': True},
            'shadow_only': True, 'execution_authorized': False, 'closure_complete': False,
            'limits': ['observation-not-executed-coverage', 'no-dependency-completeness',
                       'historical-recall-UNKNOWN', 'no-safe-omission-or-speedup',
                       'no-producer-authentication-or-OS-isolation',
                       'all-existing-local-and-remote-gates-remain']}


def main():
    try:
        asserted = context(os.environ)
        result = shadow.manifest(Path('.'), asserted['event'], os.environ['GITHUB_REF'],
                                 asserted['base'] or '', asserted['head'] or '', asserted['merge'])
        observation = project(result, asserted)
        # Bind the actual checked-out implementation bytes, not a claimed producer.
        observation['implementation_sha256'] = {
            name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
            for name in ('ci_shadow_observation.py', 'ci_shadow.py', 'ci_shadow_policy.py')}
        raw = shadow.policy.canonical(observation)
        require(len(raw) <= 32_768)
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        require(not OUTPUT.parent.is_symlink() and not OUTPUT.parent.parent.is_symlink())
        # Exclusive creation rejects stale receipts; upload additionally requires this
        # step's success, so failure cannot publish an old/partially written file.
        with OUTPUT.open('xb') as stream:
            stream.write(raw)
    except (ValueError, KeyError, TypeError, OSError):
        print('ci-shadow observation unavailable or unsafe; publication rejected', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
