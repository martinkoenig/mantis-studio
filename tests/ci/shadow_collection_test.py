"""Independent tiny real-Git fixtures for the read-only shadow collector.

The configured diagnostics also use an owned tiny serial-compiled CMake fixture
and CTest JSON discovery, never product compilation or test-command execution.
All commits/refs/config mutations belong to disposable repos; no network is used.
"""
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(os.environ.get('MANTIS_SHADOW_TEST_TOOL_DIR', Path(__file__).resolve().parents[2] / 'tools'))
sys.path.insert(0, str(TOOLS))
import ci_shadow as shadow
import shadow_resource_test as resources


def graph_cli_probe(command, output):
    """Isolated fixture owner also reaps acquisition children on failed deadlines."""
    resources.subreaper(True)
    try:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              start_new_session=True) as process:
            timed_out = False
            try:
                out, err = process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                out, err = process.communicate(timeout=1)
            result = {'timeout': timed_out, 'exit': process.returncode,
                      'stdout': out.decode(), 'stderr': err.decode()}
    finally:
        resources.reap_fixture_children()
    result['owned_children_reaped'] = True
    output.write_text(json.dumps(result))


def inherited_graph_cli_probe(command, output, stdout, stderr):
    """Fork only the single-threaded fixture owner, never the production worker.

    The real CLI is still exec'd by graph_cli_probe and execs its isolated owned
    worker. Avoid importing this entire test module in a fresh interpreter for
    each replay; subreaper state and child retirement stay in the disposable owner.
    """
    with stdout.open('wb') as out, stderr.open('wb') as err:
        os.dup2(out.fileno(), 1)
        os.dup2(err.fileno(), 2)
        graph_cli_probe(command, output)


def run_inherited_graph_probe(command, output):
    import multiprocessing
    import threading
    if threading.active_count() != 1:
        raise AssertionError('graph probe fork requires a single-threaded fixture parent')
    stdout = output.with_suffix('.stdout')
    stderr = output.with_suffix('.stderr')
    owner = multiprocessing.get_context('fork').Process(
        target=inherited_graph_cli_probe, args=(command, output, stdout, stderr))
    owner.start()
    try:
        owner.join(timeout=15)  # unchanged outer probe deadline; inner remains10
        if owner.is_alive():
            raise subprocess.TimeoutExpired(command, 15)
        code = owner.exitcode
        if code is None:
            raise AssertionError('disposable graph probe owner has no terminal exit')
        return subprocess.CompletedProcess(command, code, stdout.read_bytes(), stderr.read_bytes())
    finally:
        if owner.is_alive():
            owner.kill()
            owner.join(timeout=1)
        if owner.is_alive():
            raise AssertionError('disposable graph probe owner retirement unconfirmed')
        owner.close()


class Repository:
    def __init__(self, root):
        self.root = root
        self.env = {k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
        self.env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull,
                        GIT_AUTHOR_NAME='Fixture', GIT_AUTHOR_EMAIL='fixture@example.invalid',
                        GIT_COMMITTER_NAME='Fixture', GIT_COMMITTER_EMAIL='fixture@example.invalid',
                        GIT_AUTHOR_DATE='2000-01-01T00:00:00Z', GIT_COMMITTER_DATE='2000-01-01T00:00:00Z')
        self.git('init', '--template=', '-b', 'develop', str(root), outside=True)
        self.write('src/public.hpp', 'initial header\n')
        self.write('src/store.cpp', 'initial store\n')
        self.write('docs/guide.md', 'initial documentation\n')
        self.base = self.commit('initial')

    def git(self, *args, outside=False):
        p = subprocess.run(['git', '-c', 'core.hooksPath=' + os.devnull,
                            *([] if outside else ['-C',str(self.root)]), *args],
                           env=self.env, capture_output=True, timeout=10, check=True)
        return p.stdout.decode().strip()

    def write(self, path, text):
        p=self.root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)

    def commit(self, message):
        self.git('add','--all');self.git('commit','-m',message)
        return self.git('rev-parse','HEAD')

    def change(self):
        self.write('src/public.hpp','changed public contract\n')
        self.write('src/store.cpp','changed storage\n')
        return self.commit('two independent changes')

    def pr(self):
        self.git('checkout','-b','feature')
        self.write('src/public.hpp','PR header change\n');head=self.commit('feature')
        self.git('checkout','develop');self.git('merge','--no-ff','--no-edit','feature')
        merge=self.git('rev-parse','HEAD');self.git('update-ref','refs/pull/7/merge',merge)
        return head,merge


def inventory(repo):
    """Explicit small dependency seed, not a claimed current project inventory."""
    p=shadow.policy
    profiles=p.PROFILES
    def entry(target):
        return {'targets':[target],'tests':[],'domains':['shared'],'risk':'R1',
                'kind':'shared-header','origin':'Independent seeded boundary'}
    return {'schema':'ci110-inventory-v1','scope':'reused107',
            'source':{'commit':repo.base,'tree':repo.git('rev-parse',repo.base+'^{tree}')},
            'closure':{'complete':False,'unknowns':['runtime-closure-unqualified']},
            'provenance':['independent-fixture-only'],
            'files':{'src/public.hpp':entry('data'),'src/store.cpp':entry('storage')},
            'targets':{'data':{'dependencies':[],'sources':['src/public.hpp'],'origin':'fixture public interface'},
                       'ui':{'dependencies':['data'],'sources':[],'origin':'fixture Qt consumer'},
                       'storage':{'dependencies':['data'],'sources':['src/store.cpp'],'origin':'fixture durable consumer'}},
            'tests':[{'id':name,'targets':[target],'domains':[domain],'risk':'R1','tier':'exclusive',
                      'profiles':[r['id'] for r in profiles],'origin':'independent consumer oracle'}
                     for name,target,domain in [('ui-contract','ui','UI'),('storage-contract','storage','storage')]],
            'profiles':profiles}


def chain_inventory(repo, count, reverse=False):
    """Isomorphic chains whose sorted names favor opposite traversal orders."""
    names = [f't{n:04d}' for n in (reversed(range(count)) if reverse else range(count))]
    inv = inventory(repo)
    inv['files'] = {'src/public.hpp': inv['files']['src/public.hpp']}
    inv['files']['src/public.hpp']['targets'] = [names[0]]
    inv['targets'] = {
        name: {'dependencies': [names[i - 1]] if i else [],
               'sources': [] if i else ['src/public.hpp'], 'origin': 'independent chain'}
        for i, name in enumerate(names)}
    inv['targets']['unrelated'] = {'dependencies': [], 'sources': [], 'origin': 'unaffected control'}
    inv['tests'][0].update(id='tail-consumer', targets=[names[-1]])
    inv['tests'][1].update(id='unrelated-consumer', targets=['unrelated'])
    return inv, names


class RealGit(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mantis-shadow-regression-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.repo=Repository(self.root/'repository')

    def collect(self, head, **kwargs):
        return shadow.manifest(self.repo.root,'push','refs/heads/develop',self.repo.base,head,**kwargs)

    def full(self, result):
        decision=result['decision']
        self.assertTrue(decision['full_matrix_would_select'], 'missing evidence reduced the full matrix')
        self.assertFalse(result['execution_authorized'])
        names={r['job'] for r in decision['suite_decisions'] if r['selected'] and 'job' in r}
        self.assertEqual(names, {'linux (ubuntu-24.04, ON)','linux (ubuntu-24.04, OFF)',
                                 'linux (ubuntu-24.04-arm, ON)','linux (ubuntu-24.04-arm, OFF)','sanitizers'})

    def observation(self, event, head, tested, *, base=None, profile='ubuntu-24.04-OFF',
                    ref=None, ok=True):
        env = dict(self.repo.env, GITHUB_EVENT_NAME=event, GITHUB_SHA=tested,
                   GITHUB_REF=ref if ref is not None else (
                       'refs/pull/7/merge' if event == 'pull_request' else 'refs/heads/develop'),
                   GITHUB_JOB='sanitizers' if profile.endswith('-san') else 'linux',
                   GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2', SHADOW_PROFILE=profile,
                   SHADOW_BASE=base if base is not None else self.repo.base, SHADOW_HEAD=head,
                   PRIVATE_OBSERVATION_CANARY='never-publish-environment')
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow_observation.py')]
        completed = subprocess.run(command, cwd=self.repo.root, env=env, capture_output=True, timeout=10)
        self.assertEqual(completed.returncode, 0 if ok else 1, completed.stderr)
        self.assertEqual(completed.stdout, b'', 'raw classifier data entered the public log')
        if not ok:
            self.assertEqual(completed.stderr,
                             b'ci-shadow observation unavailable or unsafe; publication rejected\n')
            return None
        self.assertEqual(completed.stderr, b'')
        path = self.repo.root / 'build/ci-shadow/observation.json'
        raw = path.read_bytes()
        result = json.loads(raw)
        self.assertEqual(raw, shadow.policy.canonical(result))
        self.assertLessEqual(len(raw), 32768)
        for private in (str(self.repo.root), 'never-publish-environment', 'private-path-canary'):
            self.assertNotIn(private.encode(), raw, 'unsafe observation publication')
        self.assertFalse(result['execution_authorized'])
        self.assertFalse(result['closure_complete'])
        self.assertTrue(result['decision']['full_matrix_would_select'])
        self.assertTrue(result['decision']['unknown_or_error'])
        self.assertEqual({s['job'] for s in result['decision']['suite_decisions'] if 'job' in s},
                         {'linux (ubuntu-24.04, ON)', 'linux (ubuntu-24.04, OFF)',
                          'linux (ubuntu-24.04-arm, ON)', 'linux (ubuntu-24.04-arm, OFF)', 'sanitizers'})
        self.assertTrue(all(s['selected'] for s in result['decision']['suite_decisions']))
        import hashlib
        self.assertEqual(set(result['implementation_sha256']),
                         {'ci_shadow_observation.py', 'ci_shadow.py', 'ci_shadow_policy.py'})
        for name, digest in result['implementation_sha256'].items():
            self.assertEqual(digest, hashlib.sha256((TOOLS / name).read_bytes()).hexdigest())
        evidence = os.environ.get('MANTIS_SHADOW_CONFIGURED_EVIDENCE')
        if evidence:
            destination = Path(evidence) / 'workflow-observations'
            destination.mkdir(parents=True, exist_ok=True)
            saved = destination / (hashlib.sha256(raw).hexdigest() + '.json')
            if saved.exists():
                self.assertEqual(saved.read_bytes(), raw)
            else:
                saved.write_bytes(raw)
        path.unlink()  # Fixture-owned output only; each real CLI creates exclusively.
        return result

    def test_workflow_observation_real_pr_merge_is_not_branch_head(self):
        head, merge = self.repo.pr()
        r = self.observation('pull_request', head, merge)
        context = r['asserted_context']
        self.assertEqual((context['base'], context['head'], context['merge'], context['tested_commit']),
                         (self.repo.base, head, merge, merge), 'PR head was mislabeled as tested merge')
        self.assertNotEqual(context['head'], context['tested_commit'])
        self.assertEqual(r['acquisition']['tested_commit'], merge)
        self.assertEqual(r['acquisition']['identities']['merge']['parents'], [self.repo.base, head])
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertEqual(r['acquisition']['errors'], [])
        self.assertEqual(r['decision']['errors'], ['inventory-missing-inventory-anchor'])
        self.assertEqual((context['job'], context['profile'], context['run_id'], context['run_attempt']),
                         ('linux', 'x86_64-off', '123', '2'))
        # A wrong tested assertion is not silently repaired to the real checkout.
        wrong = self.observation('pull_request', head, head)
        self.assertIn('merge-parent-mismatch', wrong['acquisition']['errors'])
        self.assertIsNone(wrong['acquisition']['tested_commit'])
        missing = self.observation('pull_request', '', merge)
        self.assertIsNone(missing['asserted_context']['head'])
        self.assertEqual(missing['asserted_context']['tested_commit'], merge)
        self.assertIn('immutable-sha1-required', missing['acquisition']['errors'])
        self.assertIsNone(missing['acquisition']['tested_commit'])

    def test_workflow_observation_push_missing_shallow_and_private_paths(self):
        self.repo.write('private-path-canary\nsecret.txt', 'private-content-canary')
        head = self.repo.change()
        r = self.observation('push', head, head, profile='ubuntu-24.04-san')
        self.assertEqual(r['asserted_context']['head'], head)
        self.assertIsNone(r['asserted_context']['merge'])
        self.assertEqual(r['asserted_context']['profile'], 'x86_64-san')
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertEqual(r['acquisition']['diff']['record_count'], 3)
        self.assertNotIn('private-content-canary', json.dumps(r))
        missing = self.observation('push', head, head, base='1' * 40)
        self.assertFalse(missing['acquisition']['diff']['complete'])
        self.assertIn('git-command-failed', missing['acquisition']['errors'])
        self.repo.root.joinpath('.git/shallow').write_text(head + '\n')
        shallow = self.observation('push', head, head)
        self.assertEqual(shallow['acquisition']['errors'], ['shallow-repository'])
        self.assertEqual(shallow['acquisition']['identities'], {})
        self.assertIsNone(shallow['acquisition']['tested_commit'])
        self.assertEqual(shallow['asserted_context']['tested_commit'], head)
        self.assertEqual(shallow['decision']['errors'], ['invalid-json', 'inventory-missing-inventory-anchor'])

    def test_workflow_observation_manual_branch_and_tag_are_unsupported_full(self):
        import hashlib
        head = self.repo.change()
        # Inert workflow environment only: no tag creation or remote dispatch.
        for ref in ('refs/heads/private-path-canary', 'refs/tags/private-path-canary'):
            with self.subTest(ref=ref):
                r = self.observation('workflow_dispatch', head, head, ref=ref)
                context = r['asserted_context']
                self.assertEqual((context['event'], context['head'], context['tested_commit']),
                                 ('workflow_dispatch', head, head))
                self.assertIsNone(context['merge'])
                self.assertEqual(context['ref_sha256'], hashlib.sha256(ref.encode()).hexdigest())
                self.assertEqual(r['acquisition']['errors'], ['unsupported-event'])
                self.assertEqual(r['acquisition']['identities'], {})
                self.assertIsNone(r['acquisition']['tested_commit'])
                self.assertFalse(r['acquisition']['diff']['complete'])
                self.assertEqual(r['decision']['errors'],
                                 ['invalid-json', 'inventory-missing-inventory-anchor'])
                self.assertNotIn(ref, json.dumps(r))

    def test_workflow_observation_invalid_event_ref_rejects_without_publication(self):
        head = self.repo.change()
        for event, ref in (('workflow_dispatch', 'refs/pull/7/merge'),
                           ('workflow_dispatch', 'refs/heads/'),
                           ('workflow_dispatch', 'refs/tags/'),
                           ('workflow_dispatch', 'refs/tags/private-path-canary\n'),
                           ('push', 'refs/tags/private-path-canary'),
                           ('push', 'refs/heads/'),
                           ('pull_request', 'refs/heads/develop'),
                           ('pull_request', 'refs/pull/7/head'),
                           ('unknown-private-path-canary', 'refs/heads/develop')):
            with self.subTest(event=event, ref=ref):
                self.observation(event, head, head, ref=ref, ok=False)
                self.assertFalse((self.repo.root / 'build/ci-shadow/observation.json').exists(),
                                 'invalid context published an observation')

    def test_workflow_observation_screening_and_stale_publication_fail(self):
        import ci_shadow_observation as observation
        head = self.repo.change()
        r = self.collect(head)
        asserted = observation.context(dict(GITHUB_EVENT_NAME='push', GITHUB_SHA=head,
            GITHUB_REF='refs/heads/develop', GITHUB_JOB='linux', GITHUB_RUN_ID='1',
            GITHUB_RUN_ATTEMPT='1', SHADOW_PROFILE='ubuntu-24.04-ON',
            SHADOW_BASE=self.repo.base, SHADOW_HEAD=head))
        r['acquisition']['errors'].append('private-path-canary')
        with self.assertRaisesRegex(ValueError, 'observation-invalid-or-unsafe'):
            observation.project(r, asserted)
        r['acquisition']['errors'].clear()
        r['decision']['suite_decisions'][0]['selected'] = False
        with self.assertRaisesRegex(ValueError, 'observation-invalid-or-unsafe'):
            observation.project(r, asserted)
        output = self.repo.root / 'build/ci-shadow/observation.json'
        output.parent.mkdir(parents=True)
        output.write_text('stale-private-path-canary')
        self.observation('push', head, head, ok=False)
        self.assertEqual(output.read_text(), 'stale-private-path-canary')
        self.observation('push', head, head, profile='unsafe-private-path-canary', ok=False)

    def test_complete_diff_observes_all_consumers(self):
        head=self.repo.change();inv=shadow.policy.canonical(inventory(self.repo))
        r=self.collect(head,inventory=inv,inventory_sha256=shadow.policy.digest(inv))
        self.assertTrue(r['acquisition']['diff']['complete'])
        paths={p['path'] for p in r['acquisition']['diff']['records']}
        self.assertEqual(paths,{'src/public.hpp','src/store.cpp'},'authoritative Git diff lost changed files')
        self.assertEqual(set(r['decision']['impacted_test_ids']),{'ui-contract','storage-contract'},
                         'missing transitive consumer across the real acquired diff')
        self.assertEqual(r['acquisition']['identities']['head']['tree'],self.repo.git('rev-parse',head+'^{tree}'))
        self.assertEqual(shadow.policy.canonical(r),shadow.policy.canonical(self.collect(
            head,inventory=inv,inventory_sha256=shadow.policy.digest(inv))))
        self.full(r)

    def test_graph_dependency_visits_are_linear(self):
        inv, names = chain_inventory(self.repo, 128, reverse=True)
        shadow.policy.validate_inventory(inv)
        visits = 0
        class ObservedDependencies(list):
            def __iter__(self):
                nonlocal visits
                for dependency in super().__iter__():
                    visits += 1
                    yield dependency
        for target in inv['targets'].values():
            target['dependencies'] = ObservedDependencies(target['dependencies'])
        reasons, impacted = shadow.policy.closure(inv, [{'path': 'src/public.hpp'}])
        self.assertEqual(set(reasons), set(names), 'transitive traversal omitted or invented targets')
        self.assertEqual(impacted, {'tail-consumer'}, 'unrelated test confused with impacted test')
        # Count actual edge visits, not elapsed time or a production-reported counter.
        # Four passes over V+E allows index, reachability and reason construction,
        # but rejects repeated whole-graph scans independently of host scheduling.
        print(json.dumps({'nodes': len(inv['targets']), 'edges': len(names) - 1,
                          'observed_dependency_visits': visits, 'linear_allowance':
                          4 * (len(inv['targets']) + len(names) - 1)}))
        self.assertLessEqual(visits, 4 * (len(inv['targets']) + len(names) - 1),
                             'diagnostic traversal repeatedly revisited dependency edges')

    def test_cli_graph_impact_is_invariant_under_equivalent_renaming(self):
        self.repo.write('src/public.hpp', 'changed public contract\n')
        head = self.repo.commit('chain source change')
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'),
                   '--repository', str(self.repo.root), '--event', 'push',
                   '--ref', 'refs/heads/develop', '--base', self.repo.base, '--head', head]
        normalized = []
        for reverse in (True, False):
            with self.subTest(reverse=reverse):
                inv, names = chain_inventory(self.repo, 1500, reverse=reverse)
                shadow.policy.validate_inventory(inv)
                raw = shadow.policy.canonical(inv)
                path = self.root / 'chain.json'
                path.write_bytes(raw)
                output = self.root / 'chain-probe.json'
                completed = subprocess.run([sys.executable, '-B', str(Path(__file__)),
                    '--graph-probe', str(output), *command, '--inventory', str(path),
                    '--inventory-sha256', shadow.policy.digest(raw)],
                    capture_output=True, timeout=15, check=True)
                observed = json.loads(output.read_text())
                self.assertTrue(observed['owned_children_reaped'])
                print(json.dumps({'reverse': reverse, 'owned_children_reaped': True,
                                  'timeout': observed['timeout'], 'exit': observed['exit']}))
                self.assertFalse(observed['timeout'],
                                 'actual CLI did not emit a manifest for the bounded chain fixture')
                self.assertEqual(observed['exit'], 0)
                self.assertEqual(observed['stderr'], '')
                self.assertEqual(completed.stderr, b'')
                result = json.loads(observed['stdout'])
                self.assertEqual(result['inventory_acquisition'],
                                 {'status': 'regular-file', 'error': None, 'cleanup_errors': []})
                self.assertTrue(result['acquisition']['diff']['complete'])
                self.assertEqual(result['acquisition']['errors'], [])
                self.assertEqual(result['decision']['impacted_test_ids'], ['tail-consumer'])
                expected = {names[0]: ['source:src/public.hpp']}
                expected.update({name: ['depends-on:' + names[i - 1]]
                                 for i, name in enumerate(names) if i})
                self.assertEqual(result['decision']['target_reasons'], expected,
                                 'CLI lost the independently known transitive chain')
                self.full(result)
                logical = {name: i for i, name in enumerate(names)}
                normalized.append({logical[name]: [logical[reason.removeprefix('depends-on:')]
                    for reason in reasons if reason.startswith('depends-on:')]
                    for name, reasons in result['decision']['target_reasons'].items()})
        self.assertEqual(len(normalized), 2, 'both graph renamings must emit a complete manifest')
        self.assertEqual(normalized[0], normalized[1], 'target naming changed graph semantics')

    def test_cyclic_graph_reasons_and_invalid_inventory_recovery(self):
        self.repo.write('src/public.hpp', 'changed public contract\n')
        head = self.repo.commit('cyclic consumer source change')
        inv = inventory(self.repo)
        inv['targets']['ui']['dependencies'] = ['data', 'storage']
        inv['targets']['storage']['dependencies'] = ['ui']
        inv['targets'].update({
            'unrelated-a': {'dependencies': ['unrelated-b'], 'sources': [], 'origin': 'unreachable cycle'},
            'unrelated-b': {'dependencies': ['unrelated-a'], 'sources': [], 'origin': 'unreachable cycle'}})
        raw = shadow.policy.canonical(inv)
        good = self.collect(head, inventory=raw, inventory_sha256=shadow.policy.digest(raw))
        self.full(good)
        self.assertEqual(good['decision']['target_reasons'], {
            'data': ['source:src/public.hpp'],
            'ui': ['depends-on:data', 'depends-on:storage'],
            'storage': ['depends-on:ui']}, 'cycle reasons depended on first discovery order')
        self.assertEqual(good['decision']['impacted_test_ids'], ['storage-contract', 'ui-contract'])
        # A stale but well-formed graph remains diagnostic only, never admission.
        inv['source']['commit'] = '0' * 40
        stale = shadow.policy.canonical(inv)
        result = self.collect(head, inventory=stale, inventory_sha256=shadow.policy.digest(stale))
        self.assertIn('inventory-base-identity-mismatch', result['decision']['errors'])
        self.full(result)
        inv['targets']['ui']['dependencies'].append('missing-target')
        malformed = shadow.policy.canonical(inv)
        for data, anchor in ((None, None), (malformed, shadow.policy.digest(malformed))):
            result = self.collect(head, inventory=data, inventory_sha256=anchor)
            self.assertEqual(result['decision']['target_reasons'], {})
            self.assertEqual(result['decision']['impacted_test_ids'], [])
            self.assertEqual(result['decision']['would_select_test_ids'], [])
            self.full(result)
            self.assertEqual(result, self.collect(head, inventory=data, inventory_sha256=anchor),
                             'invalid inventory fallback was not deterministic')
        self.assertEqual(good, self.collect(head, inventory=raw, inventory_sha256=shadow.policy.digest(raw)),
                         'restored pinned graph did not recover exactly')

    def test_missing_graph_keeps_full(self):
        r=self.collect(self.repo.change());self.full(r)
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertTrue(r['decision']['unknown_or_error'])
        self.assertEqual(r['decision']['would_select_test_ids'],[])

    def test_missing_consumer_preserves_full_fallback(self):
        head=self.repo.change();inv=inventory(self.repo)
        inv['targets']['ui']['dependencies']=[]
        raw=shadow.policy.canonical(inv)
        r=self.collect(head,inventory=raw,inventory_sha256=shadow.policy.digest(raw))
        self.assertEqual({'ui-contract','storage-contract'}-set(r['decision']['impacted_test_ids']),{'ui-contract'},
                         'independent missing-consumer control did not expose the omission')
        self.assertEqual(set(r['decision']['would_select_test_ids']),{'ui-contract','storage-contract'})
        self.full(r)

    def test_real_pr_merge_and_remote_checkout_mapping(self):
        head,merge=self.repo.pr()
        r=shadow.manifest(self.repo.root,'pull_request','refs/pull/7/merge',self.repo.base,head,merge)
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertEqual(r['acquisition']['tested_commit'],merge)
        self.assertEqual(r['acquisition']['identities']['merge']['parents'],[self.repo.base,head])
        self.assertEqual(r['decision']['source']['head']['commit'],merge)
        self.assertEqual(r['acquisition']['fork_origin'],'UNKNOWN')
        self.assertTrue(r['decision']['diff']['fork'])  # conservative policy assumption
        self.repo.git('update-ref','refs/remotes/pull/7/merge',merge)
        self.repo.git('update-ref','-d','refs/pull/7/merge')
        self.assertEqual(r,shadow.manifest(self.repo.root,'pull_request','refs/pull/7/merge',self.repo.base,head,merge))
        self.full(r)

    def test_wrong_merge_parents_and_conflicting_refs(self):
        head,merge=self.repo.pr()
        bad=shadow.manifest(self.repo.root,'pull_request','refs/pull/7/merge',head,head,merge)
        self.assertIn('merge-parent-mismatch',bad['acquisition']['errors']);self.full(bad)
        self.repo.git('update-ref','refs/remotes/pull/7/merge',head)
        bad=shadow.manifest(self.repo.root,'pull_request','refs/pull/7/merge',self.repo.base,head,merge)
        self.assertIn('conflicting-event-refs',bad['acquisition']['errors']);self.full(bad)

    def test_corrupt_ref_warning_cannot_be_hidden_by_remote_mapping(self):
        head,merge=self.repo.pr()
        self.repo.git('update-ref','refs/remotes/pull/7/merge',merge)
        # Git may warn and return zero while skipping this corrupt ref.
        (self.repo.root/'.git/refs/pull/7/merge').write_text('invalid ref object\n')
        r=shadow.manifest(self.repo.root,'pull_request','refs/pull/7/merge',self.repo.base,head,merge)
        self.assertFalse(r['acquisition']['diff']['complete'])
        self.assertTrue(r['acquisition']['errors']);self.full(r)

    def test_rename_deletion_odd_paths_and_empty_diff(self):
        self.repo.git('mv','docs/guide.md','docs/renamed.md')
        (self.repo.root/'src/store.cpp').unlink()
        self.repo.write('line\nbreak\tname.txt','unusual path\n')
        self.repo.write('unicode-\u00e9.txt','unicode path\n')
        r=self.collect(self.repo.commit('rename deletion and raw paths'))
        self.assertTrue(r['acquisition']['diff']['complete'])
        records={p['path']:p for p in r['acquisition']['diff']['records']}
        self.assertEqual(records['docs/renamed.md']['status'],'R100')
        self.assertEqual(records['docs/renamed.md']['old_path'],'docs/guide.md')
        self.assertEqual(records['src/store.cpp']['status'],'D')
        self.assertIn('line\nbreak\tname.txt',records);self.assertIn('unicode-\u00e9.txt',records)
        self.full(r)
        empty=shadow.manifest(self.repo.root,'push','refs/heads/develop',self.repo.git('rev-parse','HEAD'),
                              self.repo.git('rev-parse','HEAD'))
        self.assertTrue(empty['acquisition']['diff']['complete']);self.assertEqual(empty['acquisition']['diff']['records'],[])
        self.full(empty)
        path=os.fsencode(self.repo.root)+b'/invalid-\xff.txt'
        with open(path,'wb') as stream:stream.write(b'unsupported path encoding')
        r=self.collect(self.repo.commit('non-UTF8 filename'))
        self.assertIn('unsupported-path-encoding',r['acquisition']['errors'])
        self.assertFalse(r['acquisition']['diff']['complete']);self.full(r)

    def test_shallow_and_missing_objects(self):
        head=self.repo.change();clone=self.root/'shallow'
        self.repo.git('clone','--depth=1','--no-local',str(self.repo.root),str(clone),outside=True)
        r=shadow.manifest(clone,'push','refs/heads/develop',self.repo.base,head)
        self.assertIn('shallow-repository',r['acquisition']['errors']);self.full(r)
        for base in ('f'*40,'HEAD~1','--help',self.repo.git('rev-parse',head+':src/public.hpp')):
            r=shadow.manifest(self.repo.root,'push','refs/heads/develop',base,head)
            self.assertFalse(r['acquisition']['diff']['complete']);self.full(r)

    def test_missing_tree_object_and_symlink_type_change(self):
        head=self.repo.change()
        tree=self.repo.git('rev-parse',head+'^{tree}')
        (self.repo.root/'.git/objects'/tree[:2]/tree[2:]).unlink()
        r=self.collect(head)
        self.assertFalse(r['acquisition']['diff']['complete']);self.full(r)
        # A separate disposable repository avoids repairing or hiding corruption.
        self.repo=Repository(self.root/'type-change')
        (self.repo.root/'src/public.hpp').unlink()
        (self.repo.root/'src/public.hpp').symlink_to('store.cpp')
        r=self.collect(self.repo.commit('symlink type change'))
        self.assertTrue(r['acquisition']['diff']['complete'])
        record=r['acquisition']['diff']['records'][0]
        self.assertEqual(record['status'],'T');self.assertEqual(record['new_mode'],'120000')
        self.full(r)

    def test_ref_checkout_and_acquisition_races(self):
        head=self.repo.change()
        r=shadow.manifest(self.repo.root,'push','refs/heads/missing',self.repo.base,head)
        self.assertIn('event-ref-missing',r['acquisition']['errors']);self.full(r)
        r=self.collect(self.repo.base)
        self.assertIn('event-ref-sha-mismatch',r['acquisition']['errors']);self.full(r)
        self.repo.git('checkout','--detach',self.repo.base)
        r=self.collect(head)
        self.assertIn('checkout-sha-mismatch',r['acquisition']['errors']);self.full(r)
        self.repo.git('checkout','develop')
        original=shadow.LocalGit.run
        def moving_ref(git,*args,**kwargs):
            data=original(git,*args,**kwargs)
            if args[0]=='diff':self.repo.git('update-ref','refs/heads/develop',self.repo.base)
            return data
        with patch.object(shadow.LocalGit,'run',moving_ref):r=self.collect(head)
        self.assertIn('event-ref-changed-during-acquisition',r['acquisition']['errors'])
        self.assertFalse(r['acquisition']['diff']['complete']);self.full(r)

    def test_output_and_record_bounds_and_parser_error(self):
        head=self.repo.change()
        with patch.object(shadow,'MAX_DIFF_BYTES',32):r=self.collect(head)
        self.assertIn('git-output-limit',r['acquisition']['errors']);self.full(r)
        with patch.object(shadow,'MAX_CHANGES',1):r=self.collect(head)
        self.assertIn('diff-record-limit',r['acquisition']['errors']);self.full(r)
        with patch.object(shadow,'COMMAND_SECONDS',0):r=self.collect(head)
        self.assertIn('git-command-timeout',r['acquisition']['errors']);self.full(r)
        for raw in (b'not-NUL',b'invalid\0',b':100644 100644 '+b'a'*40+b' '+b'b'*40+b' R100\0old\0'):
            with self.assertRaises(shadow.AcquisitionError):shadow.parse_diff(raw)
        with patch.object(shadow,'parse_diff',side_effect=shadow.AcquisitionError('malformed-raw-diff')):r=self.collect(head)
        self.assertIn('malformed-raw-diff',r['acquisition']['errors']);self.full(r)

    def test_submodule_type_change_and_ambient_git_settings(self):
        self.repo.git('update-index','--add','--cacheinfo','160000,'+self.repo.base+',vendor/seed')
        self.repo.git('commit','-m','gitlink without a network checkout')
        head=self.repo.git('rev-parse','HEAD')
        r=self.collect(head)
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertIn('submodule-change-unqualified',r['acquisition']['errors']);self.full(r)
        with patch.dict(os.environ,{'GIT_DIR':str(self.root/'nonexistent'),'GIT_CONFIG_COUNT':'1',
                                    'GIT_CONFIG_KEY_0':'diff.external','GIT_CONFIG_VALUE_0':'invalid-helper'}):
            self.assertEqual(r,self.collect(head))
        marker=self.root/'external-was-run'
        self.repo.git('config','diff.external','touch '+str(marker))
        self.assertEqual(r,self.collect(head));self.assertFalse(marker.exists())

    def test_repository_subdirectory_cannot_narrow_diff(self):
        self.repo.write('src/public.hpp', 'changed public contract\n')
        self.repo.write('docs/guide.md', 'changed documentation\n')
        head = self.repo.commit('changes inside and outside invocation directory')
        expected = self.collect(head)
        self.repo.git('config', 'diff.relative', 'true')
        actual = shadow.manifest(self.repo.root / 'src', 'push', 'refs/heads/develop',
                                 self.repo.base, head)
        self.assertTrue(actual['acquisition']['diff']['complete'])
        self.assertEqual({r['path'] for r in actual['acquisition']['diff']['records']},
                         {'src/public.hpp', 'docs/guide.md'},
                         'relative diff omitted or rebased committed repository paths')
        self.assertEqual(actual, expected, 'invocation directory/config changed the manifest')
        self.full(actual)

    def test_replace_objects_cannot_forge_endpoint_tree(self):
        head=self.repo.change();expected_tree=self.repo.git('rev-parse',head+'^{tree}')
        self.repo.git('replace',head,self.repo.base)
        r=self.collect(head)
        self.assertTrue(r['acquisition']['diff']['complete'])
        self.assertEqual(r['acquisition']['identities']['head']['tree'],expected_tree)
        self.assertEqual(len(r['acquisition']['diff']['records']),2);self.full(r)

    def test_cli_manifest_and_invalid_completeness_argument(self):
        head=self.repo.change()
        command=[sys.executable,'-B',str(TOOLS/'ci_shadow.py'),'--repository',str(self.repo.root),
                 '--event','push','--ref','refs/heads/develop','--base',self.repo.base,'--head',head]
        p=subprocess.run(command,capture_output=True,timeout=10,check=True)
        cli=json.loads(p.stdout)
        self.assertEqual(cli.pop('inventory_acquisition'),{'status':'not-requested','error':None,'cleanup_errors':[]})
        self.assertEqual(cli,self.collect(head))
        unsupported=list(command);unsupported[unsupported.index('push')]='workflow_dispatch'
        p=subprocess.run(unsupported,capture_output=True,timeout=10,check=True)
        self.assertIn('unsupported-event',json.loads(p.stdout)['acquisition']['errors'])
        self.full(json.loads(p.stdout))
        raw=shadow.policy.canonical(inventory(self.repo));inventory_file=self.root/'inventory.json'
        inventory_file.write_bytes(raw)
        pinned=command+['--inventory',str(inventory_file),'--inventory-sha256',shadow.policy.digest(raw)]
        p=subprocess.run(pinned,capture_output=True,timeout=10,check=True)
        cli=json.loads(p.stdout)
        self.assertEqual(cli.pop('inventory_acquisition'),{'status':'regular-file','error':None,'cleanup_errors':[]})
        self.assertEqual(cli,self.collect(head,inventory=raw,inventory_sha256=shadow.policy.digest(raw)))
        p=subprocess.run(command+['--inventory',str(inventory_file),'--inventory-sha256','0'*64],
                         capture_output=True,timeout=10,check=True)
        self.full(json.loads(p.stdout));self.assertEqual(json.loads(p.stdout)['decision']['would_select_test_ids'],[])
        inventory_file.write_bytes(b'{')
        p=subprocess.run(pinned,capture_output=True,timeout=10,check=True)
        self.full(json.loads(p.stdout))
        p=subprocess.run(command+['--complete'],capture_output=True,timeout=10)
        self.assertEqual(p.returncode,2);self.assertEqual(p.stdout,b'')
        r=shadow.manifest(self.root/'missing','push','refs/heads/develop',self.repo.base,head)
        self.assertFalse(r['acquisition']['diff']['complete']);self.full(r)


class ConfiguredInventory(unittest.TestCase):
    """Real CMake/CTest producer, independent named graph oracles, no test runs."""
    @classmethod
    def setUpClass(cls):
        import copy
        import shutil
        import shadow_configured_fixture as fixture
        import ci_shadow_configured as configured
        cls.fixture, cls.configured = fixture, configured
        cls.copy = staticmethod(copy.deepcopy)
        prepared = os.environ.get('MANTIS_SHADOW_PREPARED_FIXTURE')
        if prepared:
            cls.temp = None
            cls.root = Path(prepared)
        else:
            cls.temp = tempfile.TemporaryDirectory(prefix='mantis-configured-')
            cls.root = Path(cls.temp.name)
        def retire():
            evidence = os.environ.get('MANTIS_SHADOW_CONFIGURED_EVIDENCE')
            if evidence:
                dest = Path(evidence) / cls.root.name
                def preserve_file(source, target):
                    import stat
                    if stat.S_ISFIFO(os.lstat(source).st_mode):
                        Path(str(target) + '.node.json').write_text('{"type": "fifo", "contents": "not-readable"}\n')
                        return target
                    return shutil.copy2(source, target)
                shutil.copytree(cls.root, dest, symlinks=True, copy_function=preserve_file)
                print(json.dumps({'retained_configured_fixture': str(dest)}))
            if cls.temp is not None:
                cls.temp.cleanup()
        cls.addClassCleanup(retire)
        # Optional local hoisting is strict: invalid/stale input never triggers a
        # compiler under the active CTest parent. Default CI still produces exactly
        # the same two fixtures under their single unchanged setup12 budget.
        cls.prepared_sha256 = (os.environ.get('MANTIS_SHADOW_PREPARED_SHA256') if prepared
                               else fixture.prepare(cls.root))
        first, cls.multi_inputs = fixture.load_prepared(cls.root, cls.prepared_sha256)
        cls.original, cls.path, cls.base = first
        cls.original_bytes = {(row['root'], row['path']):
                              (Path(cls.original['roots'][row['root']]) / row['path']).read_bytes()
                              for row in cls.original['files']}
        cls.collector_cache = {}

    def setUp(self):
        self.inv = self.copy(self.original)
        self.changed = []
        self.addCleanup(self.restore)

    def restore(self):
        for root, path in self.changed:
            (Path(self.inv['roots'][root]) / path).write_bytes(self.original_bytes[root, path])

    def repin(self, root, path, value):
        self.changed.append((root, path))
        self.fixture.repin(self.inv, root, path, value)

    def document(self, kind):
        row = next(r for r in self.inv['files'] if r['root'] == 'build' and
                   Path(r['path']).name.startswith(kind + '-'))
        return row['path'], json.loads(self.original_bytes['build', row['path']])

    def diagnostic(self, changes=('public.hpp',), expected=None, raw=None, pin=None):
        raw = self.fixture.encode(self.inv) if raw is None else raw
        self.path.write_bytes(raw)
        return self.configured.diagnose(self.path, self.fixture.digest(raw) if pin is None else pin,
                                       expected or self.inv['source'], [{'path': p} for p in changes])

    def full(self, result):
        self.assertFalse(result['execution_authorized'])
        self.assertTrue(result['full_matrix_would_select'])
        self.assertFalse(result['closure_complete'])

    def unknowns(self, result):
        return {u['reason'] for u in result['unknowns']}

    def proto_collector(self):
        source = self.inv['roots']['source']
        base = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD^'], timeout=5).decode().strip()
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'), '--repository', source,
                   '--event', 'push', '--ref', 'refs/heads/develop', '--base', base,
                   '--head', self.inv['source']['commit']]

        def collect(argv):
            p = subprocess.run(argv, capture_output=True, timeout=10, check=True)
            self.assertEqual(p.stderr, b'')
            return json.loads(p.stdout)

        baseline = collect(command)
        self.assertEqual([r['path'] for r in baseline['acquisition']['diff']['records']], [self.fixture.PROTO])
        self.assertEqual(baseline.pop('schema'), 'mantis-git-shadow-v1')
        self.assertIs(baseline['execution_authorized'], False)
        self.assertTrue(baseline['decision']['full_matrix_would_select'])

        def configured(*, save=None):
            raw = self.fixture.encode(self.inv)
            self.path.write_bytes(raw)
            report = collect(command + ['--configured-inputs', str(self.path), '--configured-inputs-sha256',
                                        self.fixture.digest(raw)])
            if save is not None:
                save.write_bytes(shadow.policy.canonical(report))
            self.assertEqual(report.pop('schema'), 'mantis-git-shadow-v2')
            result = report.pop('configured_diagnostic')
            self.assertEqual(shadow.policy.canonical(report), shadow.policy.canonical(baseline),
                             'generator evidence changed the unconditional FULL projection')
            self.full(result)
            self.assertEqual(result['status'], 'ingested', result)
            return result

        return configured

    def test_real_protobuf_rule_reachability_miss_and_exact_restoration(self):
        configured = self.proto_collector()
        source = self.inv['roots']['source']

        def expect_hit(result):
            reached = {t['name'] for t in result['targets'] if t['id'] in result['target_reasons']}
            self.assertEqual(reached, {'mantis-wire', 'middle', 'app'}, 'configured generator consumer MISS')
            self.assertEqual(result['impacted_test_ids'], ['app-contract'], 'configured CTest consumer MISS')
            wire = next(t['id'] for t in result['targets'] if t['name'] == 'mantis-wire')
            self.assertEqual([e for e in result['file_target_edges'] if e['file'] == self.fixture.PROTO],
                             [{'file': self.fixture.PROTO, 'target': wire, 'evidence': 'observed',
                               'reason': 'ninja-query-protobuf-cxx'}])

        legacy = configured()
        self.assertEqual(legacy['impacted_test_ids'], [])
        self.assertIn('changed-path-consumers-unresolved', self.unknowns(legacy))
        self.inv['schema'] = 'ci110-configured-inputs-v2'
        hit = configured()
        expect_hit(hit)

        name = self.fixture.RULE_RECEIPT
        exact = self.original_bytes['build', name]
        receipt = json.loads(exact)
        edge = '    ' + str(Path(source) / self.fixture.PROTO) + '\n'
        for output, text in receipt['queries'].items():
            self.assertEqual(text.count(edge), 1, 'fixture query must witness the real input edge')
            receipt['queries'][output] = text.replace(edge, '')
        self.repin('build', name, receipt)
        miss = configured()
        self.assertEqual(miss['target_reasons'], {})
        self.assertEqual(miss['impacted_test_ids'], [])
        self.assertTrue({'protobuf-ninja-rule-input-missing', 'changed-path-consumers-unresolved'}
                        <= self.unknowns(miss))
        # The very same positive oracle goes RED on a repinned semantic loss.
        with self.assertRaisesRegex(AssertionError, 'configured generator consumer MISS'):
            expect_hit(miss)
        self.repin('build', name, exact)
        restored = configured()
        expect_hit(restored)
        self.assertEqual(restored, hit, 'exact evidence restoration changed the diagnostic')

    def test_protobuf_rule_unknown_on_stale_missing_or_unsupported_evidence(self):
        name, proto = self.fixture.RULE_RECEIPT, self.fixture.PROTO
        exact = self.original_bytes['build', name]
        receipt = json.loads(exact)
        input_line = '    ' + str(Path(self.inv['roots']['source']) / proto) + '\n'

        def unknown(reason, result):
            self.full(result)
            self.assertEqual(result['status'], 'ingested', result)
            self.assertEqual(result['target_reasons'], {})
            self.assertEqual(result['impacted_test_ids'], [])
            self.assertIn({'reason': reason, 'subject': proto, 'evidence': 'unresolved'}, result['unknowns'])

        self.inv['schema'] = 'ci110-configured-inputs-v2'
        self.inv['files'] = [r for r in self.inv['files'] if r['path'] != name]
        unknown('protobuf-ninja-evidence-missing', self.diagnostic((proto,)))
        self.inv = self.copy(self.original)
        self.inv['schema'] = 'ci110-configured-inputs-v2'
        mismatch = 'protobuf-ninja-evidence-context-mismatch'
        unsupported = 'protobuf-ninja-evidence-unsupported'
        for field, value, reason in (
                ('source', {**receipt['source'], 'commit': self.base}, mismatch),
                ('roots', {**receipt['roots'], 'build': receipt['roots']['source']}, mismatch),
                ('configuration', 'Release', mismatch), ('generator', 'Ninja Multi-Config', mismatch),
                ('schema', 'unrecognized', unsupported), ('query_file', '../arbitrary.query', unsupported)):
            with self.subTest(field=field):
                self.repin('build', name, {**receipt, field: value})
                unknown(reason, self.diagnostic((proto,)))
        for replacement, reason in (
                (input_line * 2, 'protobuf-ninja-rule-ambiguous'),
                ('    || ' + input_line[4:], unsupported),
                ('    ' + proto + '\n', 'protobuf-ninja-rule-input-missing')):
            with self.subTest(query=reason):
                queries = dict(receipt['queries'])
                queries['mantis.pb.h'] = queries['mantis.pb.h'].replace(input_line, replacement)
                self.repin('build', name, {**receipt, 'queries': queries})
                unknown(reason, self.diagnostic((proto,)))
        self.repin('build', name, exact)
        # The outer bytes are all correctly repinned, but the query snapshot is stale.
        ninja = self.original_bytes['build', 'build.ninja']
        self.repin('build', 'build.ninja', ninja + b'\n# new graph revision\n')
        unknown(mismatch, self.diagnostic((proto,)))
        self.repin('build', 'build.ninja', ninja)

        path, owner = self.fixture.target_document(self.inv, 'mantis-wire')
        header = next(row for row in owner['sources'] if row['path'].endswith('/mantis.pb.h'))
        header['path'] = str(Path(header['path']).parent / 'elsewhere' / 'mantis.pb.h')
        self.repin('build', path, owner)
        self.repin('build', name, {**receipt, 'sha256': {**receipt['sha256'],
                   'owner': self.fixture.digest(self.fixture.encode(owner))}})
        unknown('protobuf-ninja-codemodel-mismatch', self.diagnostic((proto,)))

        multi, _, _ = self.multi_inputs
        raw = self.fixture.encode({**multi, 'schema': 'ci110-configured-inputs-v2'})
        other = self.root / 'multi-rule-inputs.json'
        other.write_bytes(raw)
        unknown(unsupported, self.configured.diagnose(other, self.fixture.digest(raw), multi['source'],
                                                     [{'path': proto}]))

    def test_real_python_protobuf_rule_reaches_only_configured_consumers(self):
        configured = self.proto_collector()
        self.inv['schema'] = 'ci110-configured-inputs-v3'
        proto = self.fixture.PROTO
        output, python_receipt, cxx_receipt = (self.fixture.PYTHON_OUTPUT, self.fixture.PYTHON_RECEIPT,
                                              self.fixture.RULE_RECEIPT)
        graph = 'build.ninja'
        build = Path(self.inv['roots']['build'])
        evidence = Path(tempfile.mkdtemp(prefix='python-rule-evidence-', dir=self.root))

        def expect_hit(result):
            names = {t['id']: t['name'] for t in result['targets']}
            self.assertEqual({names[t] for t in result['target_reasons']},
                             {'mantis-wire', 'middle', 'app', 'mantis-python', 'python-consumer'},
                             'configured Python generator consumer MISS')
            self.assertEqual(result['impacted_test_ids'], ['app-contract', 'python-generator-contract'])
            self.assertIn('peer-contract', {t['id'] for t in result['tests']})
            self.assertEqual({(names[e['target']], e['reason'], e['evidence'])
                              for e in result['file_target_edges'] if e['file'] == proto},
                             {('mantis-wire', 'ninja-query-protobuf-cxx', 'observed'),
                              ('mantis-python', 'ninja-query-protobuf-python', 'observed')})

        def record(phase, result, *files):
            dest = evidence / phase
            dest.mkdir()
            (dest / 'diagnostic.json').write_bytes(shadow.policy.canonical(result))
            (dest / 'manifest.json').write_bytes(self.path.read_bytes())
            for file in files:
                (dest / Path(file).name).write_bytes((build / file).read_bytes())
            return result

        shadow_path = evidence / 'shadow.json'
        hit = record('intact', configured(save=shadow_path), graph, python_receipt, cxx_receipt)
        expect_hit(hit)
        cxx = [e for e in hit['file_target_edges'] if e['reason'] == 'ninja-query-protobuf-cxx']
        self.assertEqual(len(cxx), 1)
        corpus, observation = self.corpus_case(shadow_raw=shadow_path.read_bytes())
        observation.update(test_ids=['python-generator-contract'], basis='independent-utility-consumer-expectation')
        raw = self.fixture.encode(observation)
        (self.root / 'observation.json').write_bytes(raw)
        corpus['cases'][0]['id'] = 'proto-python'
        corpus['cases'][0]['observation']['sha256'] = self.fixture.digest(raw)
        comparison = self.compare_corpus(corpus)
        self.assertEqual(comparison['status'], 'compared', comparison)
        self.assertEqual(comparison['cases'][0]['diagnostic']['hits'], ['python-generator-contract'])

        def expect_miss(result, reason):
            with self.assertRaisesRegex(AssertionError, 'configured Python generator consumer MISS'):
                expect_hit(result)
            self.full(result)
            self.assertEqual(result['status'], 'ingested', result)
            self.assertEqual([e for e in result['file_target_edges'] if e['file'] == proto], cxx)
            self.assertEqual(result['target_reasons'], {t: hit['target_reasons'][t] for t in
                             (row['id'] for row in hit['targets'] if row['name'] in ('mantis-wire', 'middle', 'app'))})
            self.assertEqual(result['impacted_test_ids'], ['app-contract'])
            self.assertIn({'reason': reason, 'subject': proto, 'evidence': 'unresolved'}, result['unknowns'])

        exact_graph = self.original_bytes['build', graph]
        exact_python = self.original_bytes['build', python_receipt]
        exact_cxx = self.original_bytes['build', cxx_receipt]
        receipt = json.loads(exact_python)
        # Change the actual disposable configured edge, not just a receipt's text.
        line = next(row for row in exact_graph.splitlines() if row.startswith(f'build {output} | '.encode()))
        source_proto = str(Path(self.inv['roots']['source']) / proto)
        # Ninja escapes spaces/colons/dollars in path tokens; query stdout does not.
        escaped = source_proto.replace('$', '$$').replace(' ', '$ ').replace(':', '$:').encode()
        dependency = b': CUSTOM_COMMAND ' + escaped
        self.assertTrue(line.endswith(dependency), line)
        self.repin('build', graph, exact_graph.replace(line + b'\n', line.removesuffix(dependency) +
                   b': CUSTOM_COMMAND\n', 1))
        graph_pin = self.fixture.digest((build / graph).read_bytes())

        def query(node):
            proc = subprocess.run(['ninja', '-C', str(build), '-t', 'query', node],
                                  capture_output=True, check=True, timeout=5)
            self.assertEqual(proc.stderr, b'')
            return proc.stdout.decode()

        cxx_capture = json.loads(exact_cxx)
        self.assertEqual({node: query(node) for node in cxx_capture['queries']}, cxx_capture['queries'])
        self.repin('build', cxx_receipt, {**cxx_capture, 'sha256': {**cxx_capture['sha256'], graph: graph_pin}})
        mismatch = 'protobuf-python-ninja-evidence-context-mismatch'
        expect_miss(record('stale-graph', self.diagnostic((proto,)), graph, python_receipt, cxx_receipt), mismatch)
        fresh_query = query(output)
        self.assertEqual(fresh_query, receipt['query'].replace('    ' + source_proto + '\n', '', 1))
        self.repin('build', python_receipt, {**receipt, 'query': fresh_query,
                   'sha256': {**receipt['sha256'], graph: graph_pin}})
        expect_miss(record('missing-input', configured(), graph, python_receipt, cxx_receipt),
                    'protobuf-python-ninja-rule-input-missing')
        self.repin('build', graph, exact_graph)
        self.repin('build', cxx_receipt, exact_cxx)

        # Deliberately cross with the pinned C++ owner's reply digest, then test
        # a coherently repinned File API reply that no longer associates the rule.
        wrong_digest = cxx_capture['sha256']['owner']
        self.repin('build', python_receipt, {**receipt, 'sha256': {**receipt['sha256'], 'owner': wrong_digest}})
        expect_miss(record('crossed-owner-pin', self.diagnostic((proto,)), python_receipt), mismatch)
        path, owner = self.fixture.target_document(self.inv, 'mantis-python')
        rule = next(row for row in owner['sources'] if row['path'] == str(build / (output + '.rule')))
        rule['path'] = str(build / 'elsewhere' / (output + '.rule'))
        changed_owner = self.fixture.encode(owner)
        self.repin('build', path, changed_owner)
        self.repin('build', python_receipt, {**receipt, 'sha256': {**receipt['sha256'],
                   'owner': self.fixture.digest(changed_owner)}})
        expect_miss(record('wrong-owner', self.diagnostic((proto,)), path, python_receipt),
                    'protobuf-python-ninja-codemodel-mismatch')

        self.repin('build', path, self.original_bytes['build', path])
        self.repin('build', python_receipt, exact_python)
        restored = record('restored', configured(), graph, python_receipt, cxx_receipt)
        expect_hit(restored)
        self.assertEqual(shadow.policy.canonical(restored), shadow.policy.canonical(hit))

    def test_python_protobuf_optional_evidence_is_independent_and_bounded(self):
        proto, name = self.fixture.PROTO, self.fixture.PYTHON_RECEIPT
        receipt = json.loads(self.original_bytes['build', name])
        legacy = self.diagnostic((proto,))
        self.assertEqual(legacy['impacted_test_ids'], [])
        self.inv['schema'] = 'ci110-configured-inputs-v2'
        cxx_only = self.diagnostic((proto,))
        self.assertEqual(cxx_only['impacted_test_ids'], ['app-contract'])
        for result in (legacy, cxx_only):
            self.full(result)
            self.assertFalse(any(reason.startswith('protobuf-python-') for reason in self.unknowns(result)))

        def unresolved(reason, result, expected=('app-contract',)):
            self.full(result)
            self.assertEqual(result['status'], 'ingested', result)
            self.assertEqual(result['impacted_test_ids'], list(expected))
            self.assertIn({'reason': reason, 'subject': proto, 'evidence': 'unresolved'}, result['unknowns'])

        self.inv['schema'] = 'ci110-configured-inputs-v3'
        files = self.inv['files']
        self.inv['files'] = [r for r in files if r['path'] != name]
        unresolved('protobuf-python-ninja-evidence-missing', self.diagnostic((proto,)))
        self.inv['files'] = [r for r in files if r['path'] != self.fixture.RULE_RECEIPT]
        unresolved('protobuf-ninja-evidence-missing', self.diagnostic((proto,)), ('python-generator-contract',))
        self.inv['files'] = files
        unsupported = 'protobuf-python-ninja-evidence-unsupported'
        input_line = '    ' + str(Path(self.inv['roots']['source']) / proto) + '\n'
        variants = (
            (b'{', unsupported),
            ({**receipt, 'query': receipt['query'].replace(input_line, '    || ' + input_line[4:])}, unsupported),
            ({**receipt, 'query': receipt['query'].replace(input_line, input_line * 2)},
             'protobuf-python-ninja-rule-ambiguous'),
            ({**receipt, 'source': {**receipt['source'], 'commit': self.base}},
             'protobuf-python-ninja-evidence-context-mismatch'),
        )
        for value, reason in variants:
            with self.subTest(reason=reason):
                self.repin('build', name, value)
                unresolved(reason, self.diagnostic((proto,)))

        multi, _, _ = self.multi_inputs
        raw = self.fixture.encode({**multi, 'schema': 'ci110-configured-inputs-v3'})
        other = self.root / 'multi-python-rule-inputs.json'
        other.write_bytes(raw)
        unresolved(unsupported, self.configured.diagnose(other, self.fixture.digest(raw), multi['source'],
                                                        [{'path': proto}]), ())

    def test_real_named_direct_transitive_and_header_oracles(self):
        result = self.diagnostic()
        self.assertEqual(result['status'], 'ingested', result)
        self.full(result)
        names = {t['id']: t['name'] for t in result['targets']}
        observed = {(e['file'], names[e['target']]) for e in result['file_target_edges']
                    if e['evidence'] == 'observed'}
        self.assertTrue({('core.cpp', 'core'), ('public.hpp', 'core'), ('middle.cpp', 'middle'),
                         ('main.cpp', 'app'), ('peer.cpp', 'peer')} <= observed)
        includes = {(e['file'], names[e['target']], e['reason']) for e in result['file_target_edges']
                    if e['evidence'] == 'conservative-inference'}
        self.assertIn(('transitive.hpp', 'core', 'literal-include-from:public.hpp'), includes)
        deps = {(names[e['dependency']], names[e['consumer']]) for e in result['target_target_edges']}
        self.assertTrue({('core', 'middle'), ('middle', 'app')} <= deps)
        for path, expected in [('core.cpp', ['app-contract']), ('public.hpp', ['app-contract']),
                               ('transitive.hpp', ['app-contract']), ('main.cpp', ['app-contract']),
                               ('peer.cpp', ['peer-contract'])]:
            with self.subTest(path=path):
                current = self.diagnostic((path,))
                self.assertEqual(current['impacted_test_ids'], expected,
                                 'configured diagnostic dropped a real direct/transitive consumer')
                self.full(current)
        reasons = self.unknowns(result)
        self.assertTrue({'generated-source-unresolved', 'protobuf-or-qml-generation-unresolved',
                         'object-artifacts-unresolved',
                         'test-fixture-or-runtime-dependency-unresolved',
                         'preprocessor-and-transitive-closure-unqualified',
                         'dependency-completeness-unqualified'} <= reasons)
        self.assertEqual(result['source'], self.inv['source'])
        self.assertEqual(result['configuration'], 'Debug')
        self.assertEqual(result['generator'], 'Ninja')
        self.assertEqual({(r['root'], r['path'], r['sha256']) for r in result['input_files']},
                         {(r['root'], r['path'], r['sha256']) for r in self.inv['files']})

    def test_cli_versioned_composition_read_only_and_no_absolute_paths(self):
        # Real produced/frozen bytes, not a mocked producer's own expected answer.
        with patch.object(self.fixture, 'produce', side_effect=AssertionError('native setup during TEST')):
            loaded = self.fixture.load_prepared(self.root, self.prepared_sha256)
            self.assertEqual([entry[0]['generator'] for entry in loaded], ['Ninja', 'Ninja Multi-Config'])
            with self.assertRaisesRegex(AssertionError, 'prepared receipt byte mismatch'):
                self.fixture.load_prepared(self.root, '0' * 64)
            public = Path(self.original['roots']['source']) / 'public.hpp'
            pristine = public.read_bytes()
            try:
                public.write_bytes(pristine + b'// changed after native retirement\n')
                with self.assertRaisesRegex(AssertionError, 'prepared input byte mismatch'):
                    self.fixture.load_prepared(self.root, self.prepared_sha256)
            finally:
                public.write_bytes(pristine)
            self.assertEqual(self.fixture.load_prepared(self.root, self.prepared_sha256)[0][0], self.original)
        result = self.diagnostic()
        before = {key: (self.fixture.digest(data), (Path(self.inv['roots'][key[0]]) / key[1]).stat().st_mtime_ns)
                  for key, data in self.original_bytes.items()}
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'), '--repository', self.inv['roots']['source'],
                   '--event', 'push', '--ref', 'refs/heads/develop', '--base', self.base,
                   '--head', self.inv['source']['commit']]
        legacy = json.loads(subprocess.run(command, capture_output=True, timeout=10, check=True).stdout)
        p = subprocess.run(command + ['--configured-inputs', str(self.path), '--configured-inputs-sha256',
                                     self.fixture.digest(self.path.read_bytes())],
                           capture_output=True, timeout=10, check=True)
        self.assertEqual(p.stderr, b'')
        actual = json.loads(p.stdout)
        diagnostic = actual.pop('configured_diagnostic')
        self.assertEqual(diagnostic['status'], 'ingested', diagnostic)
        self.assertEqual(diagnostic['impacted_test_ids'], ['app-contract'])
        self.assertEqual(actual.pop('schema'), 'mantis-git-shadow-v2')
        self.assertEqual(legacy.pop('schema'), 'mantis-git-shadow-v1')
        self.assertEqual(actual, legacy, 'configured diagnostics changed the unconditional legacy decision')
        self.full(diagnostic)
        self.assertTrue(actual['decision']['full_matrix_would_select'])
        for root in self.inv['roots'].values():
            self.assertNotIn(root.encode(), p.stdout, 'public diagnostic leaked a private absolute root')
        after = {key: (self.fixture.digest((Path(self.inv['roots'][key[0]]) / key[1]).read_bytes()),
                       (Path(self.inv['roots'][key[0]]) / key[1]).stat().st_mtime_ns)
                 for key in before}
        self.assertEqual(before, after, 'importer changed an input byte or modification time')
        self.assertEqual(result['status'], 'ingested')
        empty = list(command)
        empty[empty.index('--base') + 1] = self.inv['source']['commit']
        p = subprocess.run(empty + ['--configured-inputs', str(self.path), '--configured-inputs-sha256',
                                   self.fixture.digest(self.path.read_bytes())],
                           capture_output=True, timeout=10, check=True)
        self.assertEqual(json.loads(p.stdout)['configured_diagnostic']['status'], 'ingested')
        self.assertTrue(json.loads(p.stdout)['decision']['full_matrix_would_select'])
        empty_corpus, observation = self.corpus_case(shadow_raw=p.stdout)
        observation['test_ids'] = None
        raw = self.fixture.encode(observation); (self.root / 'observation.json').write_bytes(raw)
        empty_corpus['cases'][0]['observation']['sha256'] = self.fixture.digest(raw)
        empty_replay = self.compare_corpus(empty_corpus)
        self.assertEqual(empty_replay['status'], 'compared', empty_replay)
        self.assertEqual(empty_replay['cases'][0]['diagnostic']['state'], 'UNKNOWN')
        # Real two-parent commit context: only the tested merge identity qualifies.
        source = self.inv['roots']['source']
        merge = subprocess.run(['git', '-C', source, '-c', 'user.name=Fixture',
                                '-c', 'user.email=fixture@example.invalid', 'commit-tree',
                                self.inv['source']['tree'], '-p', self.base,
                                '-p', self.inv['source']['commit'], '-m', 'merge fixture'],
                               capture_output=True, timeout=10, check=True).stdout.decode().strip()
        def git(*args):
            return subprocess.run(['git', '-C', source, *args], capture_output=True, timeout=10, check=True)
        git('update-ref', 'refs/heads/develop', merge)
        self.addCleanup(git, 'update-ref', 'refs/heads/develop', self.original['source']['commit'])
        git('update-ref', 'refs/pull/77/merge', merge)
        pr = list(command)
        pr[pr.index('push')] = 'pull_request'
        pr[pr.index('refs/heads/develop')] = 'refs/pull/77/merge'
        pr += ['--merge', merge, '--configured-inputs', str(self.path), '--configured-inputs-sha256',
               self.fixture.digest(self.path.read_bytes())]
        stale = json.loads(subprocess.run(pr, capture_output=True, timeout=10, check=True).stdout)
        self.assertEqual(stale['configured_diagnostic']['errors'], ['configured-source-identity-mismatch'])
        self.inv['source']['commit'] = merge
        self.diagnostic()
        pr[-1] = self.fixture.digest(self.path.read_bytes())
        static = self.static_bytes()
        (self.root / 'static.json').write_bytes(static)
        pr += ['--inventory', str(self.root / 'static.json'), '--inventory-sha256', self.fixture.digest(static)]
        merged = json.loads(subprocess.run(pr, capture_output=True, timeout=10, check=True).stdout)
        self.assertEqual(merged['configured_diagnostic']['status'], 'ingested', merged)
        self.assertEqual(merged['configured_diagnostic']['source']['commit'], merge)
        self.assertEqual(merged['configured_diagnostic']['impacted_test_ids'], ['app-contract'])
        self.full(merged['configured_diagnostic'])
        corpus, _ = self.corpus_case(shadow_raw=self.fixture.encode(merged), static_raw=static)
        compared = self.compare_corpus(corpus)
        self.assertEqual(compared['status'], 'compared', compared)
        self.assertEqual(compared['cases'][0]['context']['tested_commit'], merge)
        self.assertEqual(compared['cases'][0]['diagnostic']['hits'], ['app-contract'])
        self.assertEqual(compared['cases'][0]['static_inventory_replay']['decision_sha256'],
                         self.fixture.digest(self.fixture.encode(merged['decision'])))
        self.assertEqual(merged['decision']['source']['head']['commit'], merge)
        original_merged = self.fixture.encode(merged)
        # PR branch head cannot replace the actual tested merge in the saved
        # decision, even if the acquisition and configured graph remain valid.
        for section, key, value in [
                ('source', 'head', {k: merged['acquisition']['identities']['head'][k]
                                    for k in ('commit', 'tree')}),
                ('diff', 'head_commit', self.original['source']['commit']),
                ('diff', 'event', 'protected-push'),
                ('input_sha256', 'request', '0' * 64)]:
            bad = self.copy(merged); bad['decision'][section][key] = value
            raw = self.fixture.encode(bad); (self.root / 'shadow.json').write_bytes(raw)
            corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(raw)
            rejected = self.compare_corpus(corpus)
            self.assertEqual(rejected['errors'], ['corpus-decision-acquisition-mismatch'])
            self.assertEqual(rejected['cases'], [])
        (self.root / 'shadow.json').write_bytes(original_merged)
        corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(original_merged)
        self.assertEqual(self.compare_corpus(corpus), compared, 'exact PR restoration changed replay')
        corpus['cases'][0]['context']['tested_commit'] = self.original['source']['commit']
        self.assertEqual(self.compare_corpus(corpus)['errors'], ['corpus-evidence-context-mismatch'])

    def static_bytes(self):
        from types import SimpleNamespace
        repo = SimpleNamespace(base=self.base, git=lambda *args: subprocess.run(
            ['git', '-C', self.inv['roots']['source'], *args], capture_output=True,
            timeout=10, check=True).stdout.decode().strip())
        seed = inventory(repo)
        seed['files'] = {'public.hpp': seed['files']['src/public.hpp']}
        seed['targets']['data']['sources'] = ['public.hpp']
        seed['targets']['storage']['sources'] = []
        return self.fixture.encode(seed)

    def corpus_case(self, shadow_raw=None, *, static_raw=None, case_id='header-app',
                    expected=('app-contract',), basis='independent-cmake-link-consumer-expectation'):
        """Independent app/peer expectations; never derived from impacted IDs."""
        self.diagnostic()
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'), '--repository', self.inv['roots']['source'],
                   '--event', 'push', '--ref', 'refs/heads/develop', '--base', self.base,
                   '--head', self.inv['source']['commit'], '--configured-inputs', str(self.path),
                   '--configured-inputs-sha256', self.fixture.digest(self.path.read_bytes())]
        if static_raw is not None:
            (self.root / 'static.json').write_bytes(static_raw)
            command += ['--inventory', str(self.root / 'static.json'),
                        '--inventory-sha256', self.fixture.digest(static_raw)]
        if shadow_raw is None:
            # Reuse only the two pristine fixture collector envelopes with exact
            # command/configured closure/static-byte identity. Corrupted, omitted
            # edge, PR and policy-rejected inputs still acquire their own CLI
            # evidence. Every comparison/restoration still crosses the real CLI.
            pristine = self.inv == self.original and all(
                (Path(self.inv['roots'][root]) / path).read_bytes() == raw
                for (root, path), raw in self.original_bytes.items())
            key = tuple(command)
            shadow_raw = self.collector_cache.get(key) if pristine else None
            if shadow_raw is None:
                run = subprocess.run(command, capture_output=True, timeout=10, check=True)
                self.assertEqual(run.stderr, b'')
                shadow_raw = run.stdout
                if pristine and (static_raw is None or static_raw == self.static_bytes()):
                    self.collector_cache[key] = shadow_raw
        manifest = json.loads(shadow_raw)
        (self.root / 'shadow.json').write_bytes(shadow_raw)
        diag = manifest['configured_diagnostic']
        context = {'event': manifest['acquisition']['event'], 'ref': manifest['acquisition']['ref'],
                   'identities': manifest['acquisition']['identities'],
                   'tested_commit': manifest['acquisition']['tested_commit'],
                   'configuration': 'Debug', 'generator': 'Ninja',
                   'configured_manifest_sha256': self.fixture.digest(self.path.read_bytes()),
                   'test_inventory_sha256': self.fixture.digest(self.fixture.encode(diag['tests']))}
        observation = {'schema': 'ci110-impact-observation-v1', 'context': context,
                       'kind': 'controlled-fixture', 'origin': 'owned-tiny-consumer-contract',
                       'trust': 'caller-asserted-unverified', 'test_ids': list(expected), 'basis': basis}
        def pin(name, raw):
            (self.root / name).write_bytes(raw)
            return {'path': name, 'sha256': self.fixture.digest(raw)}
        case = {'id': case_id, 'context': context,
                'shadow': {'path': 'shadow.json', 'sha256': self.fixture.digest(shadow_raw)},
                'configured_inputs': {'path': 'inputs.json', 'sha256': self.fixture.digest(self.path.read_bytes())},
                'observation': pin('observation.json', self.fixture.encode(observation))}
        if static_raw is not None:
            case['static_inventory'] = {'path': 'static.json', 'sha256': self.fixture.digest(static_raw)}
        return {'schema': 'ci110-impact-corpus-v2' if static_raw is not None else 'ci110-impact-corpus-v1',
                'cases': [case]}, observation

    def compare_corpus(self, corpus, *, cli=True, tools=TOOLS):
        raw = self.fixture.encode(corpus)
        path = self.root / 'corpus.json'; path.write_bytes(raw)
        command = [sys.executable, '-B', str(tools / 'ci_shadow_corpus.py'), '--corpus', str(path),
                   '--corpus-sha256', self.fixture.digest(raw)]
        if cli:
            output = self.root / 'corpus-probe.json'
            run = run_inherited_graph_probe(command, output)
            self.assertEqual(run.returncode, 0, 'disposable comparison probe failed: ' + run.stderr.decode())
            self.assertEqual(run.stderr, b'')
            owned = json.loads(output.read_text())
            self.assertTrue(owned['owned_children_reaped'])
            self.assertFalse(owned['timeout'], 'comparison exceeded the owned CLI deadline')
            self.assertEqual(owned['exit'], 0, 'comparison CLI must emit record-only evidence: ' + owned['stderr'])
            self.assertEqual(owned['stderr'], '')
            text = owned['stdout']
        else:
            # Exhaustive parser cases share the actual worker entrypoint; real
            # CLI composition/containment is exercised separately, not mocked.
            import ci_shadow_corpus as comparison
            text = self.fixture.encode(comparison.compare(path, self.fixture.digest(raw))).decode()
        result = json.loads(text)
        for root in self.inv['roots'].values():
            self.assertNotIn(root, text)
        self.full(result)
        evidence = os.environ.get('MANTIS_SHADOW_CONFIGURED_EVIDENCE')
        if evidence:
            archive = Path(evidence) / 'corpus-replays'
            archive.mkdir(parents=True, exist_ok=True)
            blob_root = archive / 'blobs'; blob_root.mkdir(exist_ok=True)
            def retain(data):
                digest = self.fixture.digest(data)
                blob = blob_root / digest
                if not blob.exists():
                    blob.write_bytes(data)
                return digest
            import stat
            candidates = [self.path, self.root / 'shadow.json', self.root / 'observation.json',
                          self.root / 'peer-observation.json', self.root / 'static.json',
                          *(Path(self.inv['roots'][row['root']]) / row['path'] for row in self.inv['files'])]
            for case in corpus.get('cases', []):
                for key in ('shadow', 'configured_inputs', 'observation', 'static_inventory'):
                    ref = case.get(key)
                    if isinstance(ref, dict) and self.configured.relative(ref.get('path')):
                        candidates.append(self.root / ref['path'])
            files = {str(p): retain(p.read_bytes()) for p in candidates
                     if p.exists() and stat.S_ISREG(p.lstat().st_mode)}
            receipt = {'entrypoint': command if cli else 'ci_shadow_corpus.compare',
                       'corpus_sha256': retain(raw), 'result_sha256': retain(text.encode()),
                       'input_candidate_snapshots': files,
                       'snapshot_limit': 'candidate bytes retained after call; not proof all were consumed',
                       'interpreter': sys.executable}
            receipt_path = archive / ('replay-%04d.json' % len(list(archive.glob('replay-*.json'))))
            receipt_path.write_bytes(self.fixture.encode(receipt))
        return result

    def one_file_comparison(self, path, evidence):
        """Real one-file diff, leaving the shared configured checkout at HEAD."""
        source = Path(self.inv['roots']['source'])
        head = self.inv['source']['commit']
        env = {**os.environ, 'GIT_INDEX_FILE': str(evidence / 'alternate.index')}

        def git(*args, data=None):
            return subprocess.run(['git', '-C', source, '-c', 'user.name=Fixture',
                                   '-c', 'user.email=fixture@example.invalid', *args],
                                  env=env, input=data, capture_output=True, timeout=5, check=True).stdout

        git('read-tree', head)
        raw = self.original_bytes['source', path]
        blob = git('hash-object', '-w', '--stdin', data=raw + b'\n# disposable comparison\n').decode().strip()
        git('update-index', '--cacheinfo', '100644', blob, path)
        tree = git('write-tree').decode().strip()
        base = git('commit-tree', tree, '-p', head, '-m', 'script-only comparison').decode().strip()
        diff = git('diff', '--name-status', base, head)
        self.assertEqual(diff, f'M\t{path}\n'.encode())
        (evidence / 'script.diff').write_bytes(git('diff', '--binary', base, head))
        return base

    def test_real_alg03_enclosure_script_only_collector_and_replay(self):
        self.addCleanup(self.path.write_bytes, self.path.read_bytes())
        self.inv['schema'] = 'ci110-configured-inputs-v4'
        script, test = 'tests/unit/alg03_enclosure.py', 'alg03-enclosure'
        source = Path(self.inv['roots']['source'])
        evidence = Path(tempfile.mkdtemp(prefix='enclosure-evidence-', dir=self.root))
        head = self.inv['source']['commit']
        raw = self.original_bytes['source', script]
        base = self.one_file_comparison(script, evidence)
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'), '--repository', source,
                   '--event', 'push', '--ref', 'refs/heads/develop', '--base', base, '--head', head,
                   '--configured-inputs', self.path, '--configured-inputs-sha256']

        def collect(phase):
            raw = self.fixture.encode(self.inv)
            self.path.write_bytes(raw)
            run = subprocess.run(command + [self.fixture.digest(raw)], capture_output=True, timeout=10, check=True)
            self.assertEqual(run.stderr, b'')
            (evidence / f'{phase}-inputs.json').write_bytes(raw)
            (evidence / f'{phase}-shadow.json').write_bytes(run.stdout)
            report = json.loads(run.stdout)
            self.assertEqual(report['schema'], 'mantis-git-shadow-v2')
            self.assertEqual([r['path'] for r in report['acquisition']['diff']['records']], [script])
            self.assertFalse(report['execution_authorized'])
            self.assertTrue(report['decision']['full_matrix_would_select'])
            diag = report['configured_diagnostic']
            self.assertEqual(diag['status'], 'ingested', diag.get('errors'))
            self.full(diag)
            self.assertEqual(diag['target_reasons'], {})
            return run.stdout, diag

        def compare(raw, state):
            corpus, _ = self.corpus_case(shadow_raw=raw, case_id='enclosure-script', expected=(test,),
                                         basis='independent-exact-fixture-registration')
            replay = self.compare_corpus(corpus)
            self.assertEqual(replay['status'], 'compared', replay)
            case = replay['cases'][0]
            self.assertEqual(case['diagnostic'], {'state': state, 'denominator': 1,
                             'hits': [test] if state == 'HIT' else [],
                             'misses': [test] if state == 'MISS' else []})
            self.assertTrue({'alg03-numeric', 'peer-contract'} <= set(case['full_would_select_test_ids']))
            return shadow.policy.canonical(replay)

        intact, hit = collect('intact')
        self.assertEqual(hit['file_test_edges'], [{'file': script, 'test': test, 'evidence': 'observed',
                                                   'reason': 'ctest-alg03-enclosure-argv-match'}])
        self.assertEqual(hit['impacted_test_ids'], [test], 'configured enclosure consumer MISS')
        self.assertIn({'reason': 'test-runtime-closure-unqualified', 'subject': test, 'evidence': 'unresolved'},
                      hit['unknowns'])
        intact_comparison = compare(intact, 'HIT')

        ctest = self.inv['ctest']
        exact = self.original_bytes['build', ctest]
        document = json.loads(exact)
        row = next(row for row in document['tests'] if row['name'] == test)
        self.assertEqual(row['command'][2], str(source / script))
        decoy = evidence / Path(script).name
        decoy.write_bytes(raw)
        row['command'][2] = str(decoy)  # existing same-basename file, not the source-root script
        (evidence / 'decoy-ctest.json').write_bytes(self.fixture.encode(document))
        self.repin('build', ctest, document)
        changed, miss = collect('decoy')
        self.assertEqual(miss['file_test_edges'], [])
        self.assertEqual(miss['impacted_test_ids'], [])
        self.assertIn({'reason': 'alg03-enclosure-argv-mismatch', 'subject': script, 'evidence': 'unresolved'},
                      miss['unknowns'])
        compare(changed, 'MISS')

        self.repin('build', ctest, exact)
        restored, _ = collect('restored')
        self.assertEqual(restored, intact)
        self.assertEqual(compare(restored, 'HIT'), intact_comparison)

    def test_real_python_calibration_copyonly_collector_and_replay(self):
        import platform

        self.addCleanup(self.path.write_bytes, self.path.read_bytes())
        self.inv['schema'] = 'ci110-configured-inputs-v5'
        source, build = (Path(self.inv['roots'][root]) for root in ('source', 'build'))
        cal, init, script = self.fixture.CALIBRATION_FILES
        test = 'python-calibration'
        ctest = self.inv['ctest']
        exact = self.original_bytes['build', ctest]
        document = json.loads(exact)
        row, = (t for t in document['tests'] if t['name'] == test)
        self.assertEqual(row['command'], [sys.executable, str(source / script), str(build)])
        for path in (cal, init):
            self.assertEqual((source / path).read_bytes(), (build / 'python/mantis' / Path(path).name).read_bytes())

        def check_unknown(diag, reason):
            self.assertEqual(diag['status'], 'ingested', diag.get('errors'))
            self.full(diag)
            self.assertNotIn(test, {e['test'] for e in diag['file_test_edges']})
            self.assertEqual(diag['impacted_test_ids'], [])
            self.assertIn({'reason': 'python-calibration-' + reason, 'subject': cal, 'evidence': 'unresolved'},
                          diag['unknowns'])

        if platform.machine() != 'x86_64':
            check_unknown(self.diagnostic((cal,)), 'context-unsupported')
            return

        evidence = Path(tempfile.mkdtemp(prefix='calibration-evidence-', dir=self.root))
        base = self.one_file_comparison(cal, evidence)
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'), '--repository', source,
                   '--event', 'push', '--ref', 'refs/heads/develop', '--base', base,
                   '--head', self.inv['source']['commit'], '--configured-inputs', self.path,
                   '--configured-inputs-sha256']

        def collect():
            raw = self.fixture.encode(self.inv)
            self.path.write_bytes(raw)
            run = subprocess.run(command + [self.fixture.digest(raw)], capture_output=True, timeout=10, check=True)
            self.assertEqual(run.stderr, b'')
            report = json.loads(run.stdout)
            self.assertEqual([r['path'] for r in report['acquisition']['diff']['records']], [cal])
            self.assertFalse(report['execution_authorized'])
            self.assertTrue(report['decision']['full_matrix_would_select'])
            diag = report['configured_diagnostic']
            self.assertEqual(diag['status'], 'ingested', diag.get('errors'))
            self.full(diag)
            return run.stdout, diag

        def compare(raw):
            corpus, _ = self.corpus_case(shadow_raw=raw, case_id='python-calibration-copy', expected=(test,),
                                         basis='independent-copyonly-registration-and-reexport')
            replay = self.compare_corpus(corpus)
            self.assertEqual(replay['status'], 'compared', replay)
            case, = replay['cases']
            self.assertEqual(case['diagnostic'], {'state': 'HIT', 'denominator': 1, 'hits': [test], 'misses': []})
            self.assertIn('peer-contract', case['full_would_select_test_ids'])
            return shadow.policy.canonical(replay)

        intact, diag = collect()
        self.assertEqual(diag['impacted_test_ids'], [test], 'configured COPYONLY consumer MISS')
        self.assertEqual(diag['target_reasons'], {})
        edges = [e for e in diag['file_test_edges'] if e['test'] == test]
        self.assertEqual(edges, [{'file': cal, 'test': test, 'evidence': 'conservative-inference',
                                  'reason': 'ctest-python-calibration-copyonly-import-match'}])
        self.assertIn({'reason': 'test-runtime-closure-unqualified', 'subject': test, 'evidence': 'unresolved'},
                      diag['unknowns'])
        comparison = compare(intact)

        row['command'][2] = str(build / 'elsewhere')
        self.repin('build', ctest, document)
        check_unknown(self.diagnostic((cal,)), 'argv-mismatch')
        row['command'][2] = str(build)
        row['backtrace'] = -1
        self.repin('build', ctest, document)
        check_unknown(self.diagnostic((cal,)), 'registration-unresolved')
        self.repin('build', ctest, exact)

        copy_init = 'python/mantis/__init__.py'
        init_raw = self.original_bytes['source', init]
        # Both copies still match and all digests are refreshed, but the named
        # export is gone. Missing import evidence cannot be inferred from bytes alone.
        wrong = init_raw.replace(b'from .calibration import (Calibration,', b'from .calibration import (', 1)
        self.assertNotEqual(wrong, init_raw)
        self.repin('source', init, wrong)
        self.repin('build', copy_init, wrong)
        check_unknown(self.diagnostic((cal,)), 'import-unrecognized')
        self.repin('source', init, init_raw)
        self.repin('build', copy_init, init_raw)

        files = self.inv['files']
        self.inv['files'] = [pin for pin in files if (pin['root'], pin['path']) != ('build', copy_init)]
        check_unknown(self.diagnostic((cal,)), 'pin-missing')
        self.inv['files'] = files
        self.changed.append(('build', copy_init))
        (build / copy_init).write_bytes(wrong)
        rejected = self.diagnostic((cal,))
        self.assertEqual(rejected['status'], 'rejected')
        self.assertEqual(rejected['errors'], ['configured-file-digest-mismatch'])
        self.full(rejected)
        (build / copy_init).write_bytes(init_raw)

        restored, _ = collect()
        self.assertEqual(restored, intact)
        self.assertEqual(compare(restored), comparison)

    def test_alg03_enclosure_wrong_context_artifact_and_pins(self):
        # Restore the exact borrowed manifest even if an assertion fails; the
        # unchanged prepared-loader control must still see its sealed bytes.
        self.addCleanup(self.path.write_bytes, self.path.read_bytes())
        script, test = 'tests/unit/alg03_enclosure.py', 'alg03-enclosure'
        proto = self.fixture.PROTO
        self.inv['schema'] = 'ci110-configured-inputs-v3'
        legacy = self.diagnostic((proto,))
        self.assertNotIn('file_test_edges', legacy)
        self.assertEqual(self.diagnostic((script,))['impacted_test_ids'], [])
        self.inv['schema'] = 'ci110-configured-inputs-v4'
        pristine = self.copy(self.inv)
        raw = self.fixture.encode(pristine)
        current = self.diagnostic((proto,))
        self.assertEqual(current['file_target_edges'], legacy['file_target_edges'])
        self.assertEqual(current['target_test_edges'], legacy['target_test_edges'])
        self.assertEqual(current['impacted_test_ids'], ['app-contract', 'python-generator-contract'])
        intact = self.diagnostic((script,))
        self.assertEqual(intact['impacted_test_ids'], [test])

        def suppressed(reason, result):
            self.full(result)
            self.assertEqual(result['status'], 'ingested', result.get('errors'))
            self.assertEqual(result['file_test_edges'], [])
            self.assertEqual(result['impacted_test_ids'], [])
            self.assertIn({'reason': 'alg03-enclosure-' + reason, 'subject': script, 'evidence': 'unresolved'},
                          result['unknowns'])

        ctest = self.inv['ctest']
        exact = self.original_bytes['build', ctest]
        ctest_doc = json.loads(exact)
        ctest_row = next(row for row in ctest_doc['tests'] if row['name'] == test)
        python, flag, path, artifact = ctest_row['command']
        other_artifact = next(row['command'][0] for row in ctest_doc['tests'] if row['name'] == 'peer-contract')
        for command in (['/bin/false', flag, path, artifact], [python, '-I', path, artifact],
                        [python, flag, artifact, path], [python, flag, path, other_artifact],
                        [python, flag, path, str(Path(artifact).parent) + '/./' + Path(artifact).name],
                        [python, flag, path, artifact, '--extra'], ['/bin/sh', '-c', ' '.join(ctest_row['command'])]):
            with self.subTest(command=command):
                changed = self.copy(ctest_doc)
                next(r for r in changed['tests'] if r['name'] == test)['command'] = command
                self.repin('build', ctest, changed)
                suppressed('argv-mismatch', self.diagnostic((script,)))
        # A real, changed saved CTest file with the original manifest pin fails
        # before scoped matching; refreshing this outer pin is not implicit.
        stale = self.diagnostic((script,), raw=raw)
        self.assertEqual(stale['errors'], ['configured-file-digest-mismatch'])
        self.assertEqual(stale['status'], 'rejected')
        self.full(stale)
        renamed = self.copy(ctest_doc)
        next(r for r in renamed['tests'] if r['name'] == test)['name'] = 'generic-python-control'
        self.repin('build', ctest, renamed)
        suppressed('registration-missing', self.diagnostic((script,)))
        missing = self.copy(ctest_doc)
        del next(r for r in missing['tests'] if r['name'] == test)['command']
        self.repin('build', ctest, missing)
        suppressed('command-missing', self.diagnostic((script,)))
        self.repin('build', ctest, exact)

        owner, doc = self.fixture.target_document(self.inv, 'mantis-alg03-numeric-tests')
        self.repin('build', owner, {**doc, 'type': 'UTILITY'})
        suppressed('artifact-unresolved', self.diagnostic((script,)))
        self.repin('build', owner, {**doc, 'artifacts': doc['artifacts'] * 2})
        suppressed('artifact-unresolved', self.diagnostic((script,)))
        self.repin('build', owner, self.original_bytes['build', owner])

        cache_path, cache = self.document('cache')
        next(r for r in cache['entries'] if r['name'] == 'MANTIS_BUILD_STUDIO')['value'] = 'ON'
        self.repin('build', cache_path, cache)
        suppressed('cache-mismatch', self.diagnostic((script,)))
        _, cache = self.document('cache')
        next(r for r in cache['entries'] if r['name'] == 'Python3_EXECUTABLE')['value'] = '/bin/false'
        self.repin('build', cache_path, cache)
        suppressed('interpreter-unresolved', self.diagnostic((script,)))
        self.repin('build', cache_path, self.original_bytes['build', cache_path])
        system, = [p for root, p in self.original_bytes if root == 'build' and p.endswith('/CMakeSystem.cmake')]
        system_raw = self.original_bytes['build', system]
        cross = system_raw.replace(b'CMAKE_CROSSCOMPILING "FALSE"', b'CMAKE_CROSSCOMPILING "TRUE"')
        self.assertNotEqual(cross, system_raw)
        self.repin('build', system, cross)
        suppressed('context-unsupported', self.diagnostic((script,)))
        self.repin('build', system, system_raw)

        multi, _, _ = self.multi_inputs
        multi_raw = self.fixture.encode({**multi, 'schema': 'ci110-configured-inputs-v4'})
        other = self.root / 'enclosure-multi.json'
        other.write_bytes(multi_raw)
        suppressed('context-unsupported', self.configured.diagnose(other, self.fixture.digest(multi_raw),
                                                                   multi['source'], [{'path': script}]))
        for root, path, reason in [('build', system, 'system-pin-missing'),
                                   ('source', script, 'script-pin-missing')]:
            self.inv['files'] = [r for r in pristine['files'] if (r['root'], r['path']) != (root, path)]
            suppressed(reason, self.diagnostic((script,)))
        self.inv = pristine
        stale = self.diagnostic((script,), pin='0' * 64)
        self.full(stale)
        self.assertEqual(stale['status'], 'rejected')
        self.assertEqual(stale['errors'], ['configured-manifest-digest-mismatch'])
        self.assertEqual(shadow.policy.canonical(self.diagnostic((script,))), shadow.policy.canonical(intact))

    def test_corpus_cli_independent_hit_unrelated_and_full_separation(self):
        corpus, observation = self.corpus_case()
        result = self.compare_corpus(corpus)
        self.assertEqual(result['status'], 'compared', result)
        case = result['cases'][0]
        self.assertEqual(case['diagnostic'], {'hits': ['app-contract'], 'misses': [],
                         'denominator': 1, 'state': 'HIT'})
        self.assertEqual(case['diagnostic_impacted_test_ids'], ['app-contract'])
        self.assertEqual(case['full_would_select_test_ids'],
                         ['alg03-enclosure', 'alg03-numeric', 'app-contract', 'fixture-setup',
                          'peer-contract', 'python-calibration', 'python-generator-contract'])
        self.assertEqual(result, self.compare_corpus(corpus), 'replay output must be deterministic')
        absent = self.copy(corpus); absent['schema'] = 'ci110-impact-corpus-v2'
        absent['cases'][0]['static_inventory'] = None
        absent_result = self.compare_corpus(absent)
        self.assertEqual(absent_result['status'], 'compared', absent_result)
        absent_case = absent_result['cases'][0]
        self.assertIsNone(absent_case.pop('static_inventory_replay'))
        self.assertIsNone(absent_case['evidence_sha256'].pop('static_inventory'))
        self.assertEqual(absent_case, case, 'explicit v2 absence changed legacy comparison')
        # A second independent consumer is unrelated to this header: FULL must
        # cover it counterfactually, while diagnostics retain the mismatch.
        other = self.copy(corpus['cases'][0]); other['id'] = 'unrelated-peer'
        observation.update(test_ids=['peer-contract'], kind='caller-assertion')
        raw = self.fixture.encode(observation)
        (self.root / 'peer-observation.json').write_bytes(raw)
        other['observation'] = {'path': 'peer-observation.json', 'sha256': self.fixture.digest(raw)}
        corpus['cases'].insert(0, other)
        mixed = self.compare_corpus(corpus)
        self.assertEqual([c['id'] for c in mixed['cases']], ['header-app', 'unrelated-peer'])
        self.assertEqual(mixed['cases'][1]['diagnostic']['misses'], ['peer-contract'])
        self.assertEqual(mixed['cases'][1]['full_observation_coverage']['hits'], ['peer-contract'])
        self.assertEqual(mixed['groups']['controlled-fixture']['hits'], 1)
        self.assertEqual(mixed['groups']['controlled-fixture']['misses'], 0)
        self.assertEqual(mixed['groups']['caller-assertion']['misses'], 1)
        self.assertIsNone(mixed['cases'][1]['causal_denominator'])
        corpus['cases'].reverse()
        reordered = self.compare_corpus(corpus)
        self.assertEqual(reordered['cases'], mixed['cases'])
        self.assertEqual(reordered['groups'], mixed['groups'])

    def test_corpus_dropped_real_edge_miss_is_not_hidden_by_full(self):
        static = self.static_bytes()
        corpus, _ = self.corpus_case(static_raw=static)
        path, middle = self.fixture.target_document(self.inv, 'middle')
        app_path, app = self.fixture.target_document(self.inv, 'app')
        _, core = self.fixture.target_document(self.inv, 'core')
        middle['dependencies'] = []
        app['dependencies'] = [d for d in app['dependencies'] if d['id'] != core['id']]
        self.repin('build', path, middle)
        self.repin('build', app_path, app)
        dropped, _ = self.corpus_case(static_raw=static)
        result = self.compare_corpus(dropped)
        self.assertEqual(result['status'], 'compared', result)
        case = result['cases'][0]
        self.assertEqual(case['diagnostic'], {'hits': [], 'misses': ['app-contract'],
                         'denominator': 1, 'state': 'MISS'},
                         'FULL conflation concealed the independently known dropped consumer')
        self.assertEqual(case['full_observation_coverage']['hits'], ['app-contract'])
        self.assertEqual(case['reason'], 'diagnostic-edge-miss-despite-unconditional-FULL')
        self.assertEqual(result['groups']['controlled-fixture']['misses'], 1)
        saved = json.loads((self.root / 'shadow.json').read_bytes())
        self.assertEqual(saved['decision']['impacted_test_ids'],
                         ['storage-contract', 'ui-contract'], 'static success must not hide configured MISS')
        self.restore(); self.changed = []; self.inv = self.copy(self.original)
        restored, _ = self.corpus_case(static_raw=static)
        self.assertEqual(restored, corpus, 'exact input restoration changed evidence identities')
        self.assertEqual(self.compare_corpus(restored)['cases'][0]['diagnostic']['state'], 'HIT')

    def test_corpus_observation_kinds_empty_unknown_and_unknown_test(self):
        corpus, observation = self.corpus_case(static_raw=self.static_bytes())
        # Batch the four independent labels in one bounded real CLI invocation
        # per expectation shape, not twelve redundant replays of the same closure.
        # All twelve semantic cases/assertions and the supervised path remain.
        for test_ids in (['app-contract'], [], None):
            batch = self.copy(corpus); batch['cases'] = []
            for kind in ('controlled-fixture', 'causally-adjudicated', 'caller-assertion', 'historical-unadjudicated'):
                observation.update(kind=kind, test_ids=test_ids)
                raw = self.fixture.encode(observation)
                name = kind + '-observation.json'; (self.root / name).write_bytes(raw)
                row = self.copy(corpus['cases'][0]); row['id'] = kind
                row['observation'] = {'path': name, 'sha256': self.fixture.digest(raw)}
                batch['cases'].append(row)
            result = self.compare_corpus(batch)
            self.assertEqual(result['status'], 'compared', result)
            self.assertEqual([c['kind'] for c in result['cases']],
                             ['caller-assertion', 'causally-adjudicated', 'controlled-fixture', 'historical-unadjudicated'])
            for case in result['cases']:
                with self.subTest(kind=case['kind'], test_ids=test_ids):
                    self.assertEqual(case['diagnostic']['state'], 'HIT' if test_ids else 'UNKNOWN')
                    self.assertEqual(case['causal_denominator'], 1 if test_ids and case['kind'] in
                                     ('controlled-fixture', 'causally-adjudicated') else None)
                    self.assertEqual(result['historical_causal_recall'], 'UNKNOWN')
                    self.assertTrue(all(g['coverage_claim'] == 'NONE' for g in result['groups'].values()))
        observation['test_ids'] = ['never-registered']
        raw = self.fixture.encode(observation); (self.root / 'observation.json').write_bytes(raw)
        corpus['cases'][0]['observation']['sha256'] = self.fixture.digest(raw)
        bad = self.compare_corpus(corpus)
        self.assertEqual(bad['status'], 'rejected')
        self.assertEqual(bad['cases'], [], 'partial results escaped invalid corpus')
        self.assertEqual(bad['errors'], ['corpus-unknown-or-duplicate-test'])
        empty = self.compare_corpus({'schema': 'ci110-impact-corpus-v1', 'cases': []})
        self.assertEqual(empty['status'], 'compared')
        self.assertEqual(empty['cases'], [])
        self.assertTrue(all(g['causal_recall'] == 'UNKNOWN' for g in empty['groups'].values()))

    def test_corpus_rejects_invalid_stale_unsafe_and_partial_evidence(self):
        import ci_shadow_corpus as comparison
        corpus, observation = self.corpus_case()
        original_observation = self.fixture.encode(observation)
        good = self.compare_corpus(corpus)
        cases = []
        bad = self.copy(corpus); bad['schema'] = 'future-v9'; cases.append(bad)
        bad = self.copy(corpus); bad['cases'] *= 2; cases.append(bad)
        bad = self.copy(corpus); bad['cases'] *= 9; cases.append(bad)
        for key, value in [('configuration', 'Release'), ('generator', 'Other'), ('tested_commit', '0' * 40),
                           ('test_inventory_sha256', '0' * 64), ('configured_manifest_sha256', '0' * 64)]:
            bad = self.copy(corpus); bad['cases'][0]['context'][key] = value; cases.append(bad)
        bad = self.copy(corpus); bad['cases'][0]['context']['identities']['head']['tree'] = '0' * 40; cases.append(bad)
        for path in ('../escape.json', '/absolute.json', 'https://invalid.example/input', 'sub/../shadow.json'):
            bad = self.copy(corpus); bad['cases'][0]['shadow']['path'] = path; cases.append(bad)
        bad = self.copy(corpus); bad['cases'][0]['shadow']['sha256'] = '0' * 64; cases.append(bad)
        bad = self.copy(corpus); bad['cases'][0]['shadow']['path'] = 'missing.json'; cases.append(bad)
        link = self.root / 'linked.json'; link.symlink_to(self.root / 'shadow.json')
        bad = self.copy(corpus); bad['cases'][0]['shadow']['path'] = link.name; cases.append(bad)
        fifo = self.root / 'corpus-fifo'; os.mkfifo(fifo)
        bad = self.copy(corpus); bad['cases'][0]['shadow']['path'] = fifo.name; cases.append(bad)
        for index, bad in enumerate(cases):
            result = self.compare_corpus(bad, cli=index in (0, len(cases) - 3, len(cases) - 2, len(cases) - 1))
            self.assertEqual(result['status'], 'rejected', bad)
            self.assertEqual(result['cases'], [], 'partial corpus escaped rejection')
        for raw in (b'{', b'[]', b'{"schema":1,"schema":2}', b'{"v":NaN}'):
            path = self.root / 'malformed.json'; path.write_bytes(raw)
            result = comparison.compare(path, self.fixture.digest(raw))
            self.assertEqual(result['status'], 'rejected')
            self.full(result)
        for change in ('stale-context', 'duplicate-test', 'unsupported-kind', 'missing-basis'):
            bad = self.copy(observation)
            if change == 'stale-context': bad['context']['configuration'] = 'Release'
            elif change == 'duplicate-test': bad['test_ids'] *= 2
            elif change == 'unsupported-kind': bad['kind'] = 'trusted-history'
            else: bad.pop('basis')
            raw = self.fixture.encode(bad); (self.root / 'observation.json').write_bytes(raw)
            corpus['cases'][0]['observation']['sha256'] = self.fixture.digest(raw)
            self.assertEqual(self.compare_corpus(corpus)['status'], 'rejected', change)
        (self.root / 'observation.json').write_bytes(original_observation)
        corpus['cases'][0]['observation']['sha256'] = self.fixture.digest(original_observation)
        # Repinned stale serialized projection must be rejected, not trusted.
        raw = (self.root / 'shadow.json').read_bytes(); saved = json.loads(raw)
        malformed = []
        for key, value in [('schema', 'mantis-git-shadow-v1'), ('execution_authorized', True)]:
            bad = self.copy(saved); bad[key] = value; malformed.append(bad)
        for key, value in [('tested_commit', '0' * 40), ('errors', ['missing-evidence']),
                           ('privileged_access', True)]:
            bad = self.copy(saved); bad['acquisition'][key] = value; malformed.append(bad)
        for key, value in [('complete', False), ('bytes', True), ('sha256', '0' * 64), ('record_count', 0)]:
            bad = self.copy(saved); bad['acquisition']['diff'][key] = value; malformed.append(bad)
        for key, value in [('full_matrix_would_select', False), ('activation_qualified', True),
                           ('schema', 'future-v9')]:
            bad = self.copy(saved); bad['decision'][key] = value; malformed.append(bad)
        for bad in malformed:
            bad_raw = self.fixture.encode(bad); (self.root / 'shadow.json').write_bytes(bad_raw)
            corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(bad_raw)
            rejected = self.compare_corpus(corpus, cli=False)
            self.assertEqual(rejected['status'], 'rejected', bad)
            self.assertEqual(rejected['cases'], [])
        saved['configured_diagnostic']['impacted_test_ids'] = []
        stale = self.fixture.encode(saved); (self.root / 'shadow.json').write_bytes(stale)
        corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(stale)
        self.assertEqual(self.compare_corpus(corpus)['errors'], ['corpus-stale-or-mismatched-shadow'])
        (self.root / 'shadow.json').write_bytes(raw)
        corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(raw)
        self.assertEqual(self.compare_corpus(corpus), good, 'invalid input contaminated restored replay')

    def test_corpus_saved_envelope_provenance_rejection_and_restoration(self):
        corpus, _ = self.corpus_case()
        original = (self.root / 'shadow.json').read_bytes()
        saved = json.loads(original)
        # Missing OPTIONAL static inventory is normal, not malformed evidence.
        self.assertEqual(saved['inventory_acquisition'],
                         {'status': 'not-requested', 'error': None, 'cleanup_errors': []})
        self.assertEqual(saved['decision']['errors'], ['inventory-missing-inventory-anchor'])
        good = self.compare_corpus(corpus)
        self.assertEqual(good['status'], 'compared', good)
        self.assertEqual(good['cases'][0]['diagnostic']['hits'], ['app-contract'])
        # A valid first case must not escape when the second envelope is invalid.
        (self.root / 'original-shadow.json').write_bytes(original)
        first = self.copy(corpus['cases'][0]); first['id'] = 'valid-first'
        first['shadow']['path'] = 'original-shadow.json'
        corpus['cases'].insert(0, first)
        variants = []
        bad = self.copy(saved)
        bad['decision'] = {k: saved['decision'][k] for k in
                           ('schema', 'policy_version', 'shadow_only', 'execution_authorized',
                            'activation_qualified', 'full_matrix_would_select')}
        variants.append(('truncated-decision', bad))
        for section, key, value in [
                ('source', 'head', {'commit': '0' * 40, 'tree': '0' * 40}),
                ('diff', 'changes', []), ('diff', 'reported_count', 0),
                ('input_sha256', 'request', '0' * 64),
                ('input_sha256', 'expected_inventory', '0' * 64)]:
            bad = self.copy(saved); bad['decision'][section][key] = value
            variants.append((section + '-' + key, bad))
        for key, value in [('source', None), ('mode', 'shadow-final'), ('extra', None),
                           ('limits', []), ('inventory', {}), ('suite_decisions', [])]:
            bad = self.copy(saved); bad['decision'][key] = value; variants.append((key, bad))
        bad = self.copy(saved); bad['decision']['suite_decisions'][0]['selected'] = 1
        variants.append(('boolean-is-not-integer', bad))
        for key, value in [('closure_complete', 0), ('execution_authorized', 0),
                           ('full_matrix_would_select', 1), ('extra', None)]:
            bad = self.copy(saved); bad['configured_diagnostic'][key] = value
            variants.append(('configured-' + key, bad))
        for key, value in [('inventory_acquisition', None), ('limits', None), ('limits', []),
                           ('limits', saved['limits'] + ['unsupported-limit'])]:
            bad = self.copy(saved); bad[key] = value; variants.append((key, bad))
        for key, value in [('status', 'regular-file'), ('error', 'missing'),
                           ('cleanup_errors', ['unconfirmed']), ('extra', None)]:
            bad = self.copy(saved); bad['inventory_acquisition'][key] = value
            variants.append(('receipt-' + key, bad))
        for label, bad in variants:
            with self.subTest(variant=label):
                raw = self.fixture.encode(bad); (self.root / 'shadow.json').write_bytes(raw)
                corpus['cases'][1]['shadow']['sha256'] = self.fixture.digest(raw)
                # Required provenance regressions cross the real supervised CLI;
                # additional schema/type permutations use its unchanged worker.
                result = self.compare_corpus(corpus, cli=label in {
                    'truncated-decision', 'source-head', 'diff-changes', 'input_sha256-request',
                    'inventory_acquisition', 'limits'})
                self.assertEqual(result['status'], 'rejected',
                                 'Malformed or conflicting saved envelope admitted as compared: ' + label)
                self.assertEqual(result['cases'], [], 'partial case escaped saved-envelope rejection')
                self.assertEqual(len(result['errors']), 1)
                self.assertRegex(result['errors'][0], r'^corpus-[a-z-]+$')
        (self.root / 'shadow.json').write_bytes(original)
        corpus['cases'].pop(0)
        corpus['cases'][0]['shadow']['sha256'] = self.fixture.digest(original)
        self.assertEqual(self.compare_corpus(corpus), good, 'exact saved bytes did not restore GREEN')
        # The collector still supports a supplied static inventory. This replay
        # schema cannot re-read its original bytes, so reject that path explicitly
        # rather than pretend a saved static projection proves input integrity.
        from types import SimpleNamespace
        repo = SimpleNamespace(base=self.base, git=lambda *args: subprocess.run(
            ['git', '-C', self.inv['roots']['source'], *args], capture_output=True,
            timeout=10, check=True).stdout.decode().strip())
        static = self.fixture.encode(inventory(repo)); static_path = self.root / 'static.json'
        static_path.write_bytes(static)
        command = [sys.executable, '-B', str(TOOLS / 'ci_shadow.py'),
                   '--repository', self.inv['roots']['source'], '--event', 'push',
                   '--ref', 'refs/heads/develop', '--base', self.base, '--head', self.inv['source']['commit'],
                   '--configured-inputs', str(self.path), '--configured-inputs-sha256',
                   self.fixture.digest(self.path.read_bytes()), '--inventory', str(static_path),
                   '--inventory-sha256', self.fixture.digest(static)]
        supplied = subprocess.run(command, capture_output=True, timeout=10, check=True)
        self.assertEqual(supplied.stderr, b'')
        supplied_saved = json.loads(supplied.stdout)
        self.assertEqual(supplied_saved['inventory_acquisition']['status'], 'regular-file')
        self.assertIsNotNone(supplied_saved['decision']['inventory'])
        unsupported, _ = self.corpus_case(shadow_raw=supplied.stdout)
        result = self.compare_corpus(unsupported)
        self.assertEqual(result['errors'], ['corpus-static-inventory-evidence-unsupported'])
        self.assertEqual(result['cases'], [])
        # Actual unpinned and failed collector acquisition is still unsupported,
        # even when a v2 caller supplies bytes or an explicit absence marker.
        for mode in ('unpinned', 'failed'):
            bad_command = list(command)
            if mode == 'unpinned':
                bad_command = bad_command[:-2]
            else:
                bad_command[bad_command.index('--inventory') + 1] = str(self.root)
            failure = subprocess.run(bad_command, capture_output=True, timeout=10, check=True)
            actual = json.loads(failure.stdout)
            self.assertEqual(actual['inventory_acquisition']['status'],
                             'regular-file' if mode == 'unpinned' else 'failed')
            unsupported, _ = self.corpus_case(shadow_raw=failure.stdout)
            unsupported['schema'] = 'ci110-impact-corpus-v2'
            unsupported['cases'][0]['static_inventory'] = {'path': 'static.json', 'sha256': self.fixture.digest(static)}
            bad = self.compare_corpus(unsupported)
            self.assertEqual(bad['errors'], ['corpus-decision-acquisition-mismatch' if mode == 'unpinned' else
                                            'corpus-static-inventory-evidence-unsupported'])
            self.assertEqual(bad['cases'], [])
        (self.root / 'shadow.json').write_bytes(original)
        self.assertEqual(self.compare_corpus(corpus), good, 'static rejection contaminated absent-inventory replay')

    def test_corpus_pinned_static_original_bytes_and_receipt_consistency(self):
        static = self.static_bytes()
        corpus, _ = self.corpus_case(static_raw=static)
        saved = json.loads((self.root / 'shadow.json').read_bytes())
        good = self.compare_corpus(corpus)
        self.assertEqual(good['status'], 'compared', good)
        self.assertEqual(good['schema'], 'ci110-impact-comparison-v2')
        case = good['cases'][0]
        self.assertEqual(case['static_inventory_replay'], {
            'decision_sha256': self.fixture.digest(self.fixture.encode(saved['decision'])),
            'decision_errors': saved['decision']['errors']})
        self.assertEqual(case['evidence_sha256']['static_inventory'], self.fixture.digest(static))
        self.assertEqual(case['diagnostic']['hits'], ['app-contract'])
        self.assertEqual(saved['decision']['impacted_test_ids'], ['storage-contract', 'ui-contract'])
        self.assertTrue(saved['decision']['unknown_or_error'])
        self.assertIn('dependency-closure-incomplete', case['static_inventory_replay']['decision_errors'])
        # A valid first case cannot escape whole-corpus rejection of case two.
        second = self.copy(corpus['cases'][0]); second['id'] = 'second-static'
        corpus['cases'].append(second)
        variants = []
        for key, value in [('input_sha256', None), ('inventory', None), ('source', None)]:
            bad = self.copy(saved); bad['decision'][key] = value; variants.append((key, bad))
        for key in ('inventory', 'expected_inventory', 'request'):
            bad = self.copy(saved); bad['decision']['input_sha256'][key] = '0' * 64
            variants.append(('digest-' + key, bad))
        bad = self.copy(saved); bad['decision']['inventory']['source']['tree'] = '0' * 40
        variants.append(('wrong-static-source', bad))
        for key, value in [('status', 'failed'), ('status', 'not-requested'), ('error', 'inventory-read-failed'),
                           ('cleanup_errors', ['unconfirmed']), ('cleanup_errors', None), ('extra', None),
                           ('status', True), ('error', False)]:
            bad = self.copy(saved); bad['inventory_acquisition'][key] = value
            variants.append(('receipt-' + key, bad))
        bad = self.copy(saved); bad['decision']['inventory']['closure']['complete'] = 0
        variants.append(('boolean-not-int', bad))
        for label, bad in variants:
            raw = self.fixture.encode(bad); (self.root / 'bad-static-shadow.json').write_bytes(raw)
            second['shadow'] = {'path': 'bad-static-shadow.json', 'sha256': self.fixture.digest(raw)}
            result = self.compare_corpus(corpus, cli=label in {
                'digest-expected_inventory', 'wrong-static-source'})
            self.assertEqual(result['status'], 'rejected', 'static cross-consistency bypass: ' + label)
            self.assertEqual(result['cases'], [])
        corpus['cases'].pop()
        # Missing/null/extra/unpinned refs cannot borrow the saved projection.
        variants = []
        for ref in (None, {}, {'path': 'static.json'}, {'path': 'static.json', 'sha256': None},
                    {'path': 'static.json', 'sha256': True},
                    {'path': 'static.json', 'sha256': '0' * 64},
                    {**corpus['cases'][0]['static_inventory'], 'extra': None}):
            bad = self.copy(corpus); bad['cases'][0]['static_inventory'] = ref; variants.append(bad)
        bad = self.copy(corpus); bad['cases'][0].pop('static_inventory'); variants.append(bad)
        for name in ('../static.json', '/static.json', 'sub/../static.json', 'missing-static.json',
                     'static-link', 'static-fifo', 'static-dir', 'static-ancestor/static.json', 'static-large'):
            bad = self.copy(corpus); bad['cases'][0]['static_inventory']['path'] = name; variants.append(bad)
        (self.root / 'static-link').symlink_to(self.root / 'static.json')
        os.mkfifo(self.root / 'static-fifo'); (self.root / 'static-dir').mkdir()
        (self.root / 'static-ancestor').symlink_to(self.root, target_is_directory=True)
        with (self.root / 'static-large').open('wb') as stream:
            stream.truncate(2_000_001)
        for index, bad in enumerate(variants):
            result = self.compare_corpus(bad, cli=index in (0, 8, 12, 13))
            self.assertEqual(result['status'], 'rejected', bad)
            self.assertEqual(result['cases'], [])
        # A fresh ref digest cannot replay old saved expected/inventory digests.
        (self.root / 'static.json').write_bytes(static + b' ')
        self.assertEqual(self.compare_corpus(corpus)['errors'], ['corpus-file-digest-mismatch'])
        repinned = self.copy(corpus)
        repinned['cases'][0]['static_inventory']['sha256'] = self.fixture.digest(static + b' ')
        self.assertEqual(self.compare_corpus(repinned)['errors'], ['corpus-decision-acquisition-mismatch'])
        (self.root / 'static.json').write_bytes(static)
        self.assertEqual(self.compare_corpus(corpus), good, 'exact original-byte restoration did not restore GREEN')
        # Real successful read but rejected policy input remains an exact failure,
        # not a fabricated acquisition failure or successful complete inventory.
        for raw, error in [(b'{', 'inventory-invalid-json'),
                           (self.fixture.encode({**json.loads(static), 'source': self.inv['source']}),
                            'inventory-base-identity-mismatch')]:
            rejected_policy, _ = self.corpus_case(static_raw=raw)
            actual = json.loads((self.root / 'shadow.json').read_bytes())
            replay = self.compare_corpus(rejected_policy)
            self.assertEqual(replay['status'], 'compared', replay)
            self.assertEqual(replay['cases'][0]['static_inventory_replay'], {
                'decision_sha256': self.fixture.digest(self.fixture.encode(actual['decision'])),
                'decision_errors': actual['decision']['errors']})
            self.assertIn(error, replay['cases'][0]['static_inventory_replay']['decision_errors'])
            self.assertTrue(actual['decision']['unknown_or_error'])
        restored, _ = self.corpus_case(static_raw=static)
        self.assertEqual(self.compare_corpus(restored), good)

    def test_corpus_snapshot_mutation_bounds_read_only_and_inert_data(self):
        import ci_shadow_corpus as comparison
        corpus, observation = self.corpus_case(static_raw=self.static_bytes())
        before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in
                  [self.path, self.root / 'shadow.json', self.root / 'observation.json', self.root / 'static.json',
                   *(Path(self.inv['roots'][row['root']]) / row['path'] for row in self.inv['files'])]}
        self.compare_corpus(corpus)
        self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in before})
        path = self.root / 'corpus.json'; digest = self.fixture.digest(path.read_bytes())
        for name, value in (('MAX_TOTAL_BYTES', 1), ('MAX_FILES', 1), ('MAX_CASES', 0)):
            with patch.object(comparison, name, value):
                self.assertEqual(comparison.compare(path, digest)['status'], 'rejected', name)
        with patch.object(shadow, 'MAX_INVENTORY_BYTES', 1):
            self.assertEqual(comparison.compare(path, digest)['errors'], ['corpus-output-limit'])
        original = comparison.compare_case
        def mutate(*args):
            result = original(*args)
            p = self.root / 'observation.json'
            p.write_bytes(p.read_bytes() + b' ')
            return result
        with patch.object(comparison, 'compare_case', mutate):
            result = comparison.compare(path, digest)
        self.assertEqual(result['status'], 'rejected')
        self.assertEqual(result['errors'], ['corpus-input-mutated'])
        self.assertEqual(result['cases'], [])
        (self.root / 'observation.json').write_bytes(before[self.root / 'observation.json'][0])
        # Actual supervised CLI: a disposable source mutation changes the raw
        # static bytes AFTER all case comparisons, before the whole-input recheck.
        # No validator/receipt mock, no timing race, and exact restoration below.
        tools = self.root / 'mutation-tools'; tools.mkdir()
        for name in ('ci_shadow.py', 'ci_shadow_corpus.py', 'ci_shadow_policy.py',
                     'ci_shadow_inventory.py', 'ci_shadow_configured.py'):
            (tools / name).write_bytes((TOOLS / name).read_bytes())
        worker = tools / 'ci_shadow_corpus.py'; source = worker.read_text()
        anchor = '    def verify(self):\n'
        self.assertEqual(source.count(anchor), 1)
        worker.write_text(source.replace(anchor, anchor +
            "        p = Path(" + repr(str(self.root / 'static.json')) + ")\n" +
            "        p.write_bytes(p.read_bytes() + b' ')\n"))
        mutated = self.compare_corpus(corpus, tools=tools)
        self.assertEqual(mutated['errors'], ['corpus-input-mutated'])
        self.assertEqual(mutated['cases'], [])
        (self.root / 'static.json').write_bytes(before[self.root / 'static.json'][0])
        self.assertEqual(self.compare_corpus(corpus)['status'], 'compared')
        # Real imported command is inert. Independently expect an unmapped test
        # to MISS diagnostic impact even though FULL still covers its identifier.
        ctest = json.loads(self.original_bytes['build', 'ctest.json'])
        marker = self.root / 'corpus-command-never-ran'
        next(t for t in ctest['tests'] if t['name'] == 'app-contract')['command'] = [
            sys.executable, '-c', 'from pathlib import Path; Path(' + repr(str(marker)) + ').touch()']
        self.repin('build', 'ctest.json', ctest)
        inert, _ = self.corpus_case()
        result = self.compare_corpus(inert)
        self.assertEqual(result['cases'][0]['diagnostic']['misses'], ['app-contract'])
        self.assertFalse(marker.exists(), 'corpus replay executed an imported command')
        # Independent negative for the inherited fixture owner: a real failed
        # command leaves a closed-stream child. Only the disposable subreaper may
        # adopt/kill/reap it; preserve stdout/stderr/primary exit and parent state.
        import ctypes
        before_subreaper = ctypes.c_int()
        libc = ctypes.CDLL(None, use_errno=True)
        self.assertEqual(libc.prctl(37, ctypes.byref(before_subreaper), 0, 0, 0), 0)
        identities = self.root / 'inherited-owner-child.json'
        launcher = '''import json,os,signal,sys
from pathlib import Path
r,w=os.pipe()
child=os.fork()
if child==0:
    os.close(r)
    for fd in (0,1,2):os.close(fd)
    os.write(w,b'entered');os.close(w)
    while True:signal.pause()
os.close(w)
assert os.read(r,32)==b'entered'
os.close(r)
Path(sys.argv[1]).write_text(json.dumps({'child':child,'group':os.getpgrp()}))
print('primary stdout',flush=True)
print('primary stderr',file=sys.stderr,flush=True)
sys.exit(7)
'''
        output = self.root / 'inherited-owner-probe.json'
        probe = run_inherited_graph_probe([sys.executable, '-B', '-c', launcher, str(identities)], output)
        self.assertEqual((probe.returncode, probe.stdout, probe.stderr), (0, b'', b''))
        observed = json.loads(output.read_text())
        self.assertEqual(observed, {'timeout': False, 'exit': 7, 'stdout': 'primary stdout\n',
                                   'stderr': 'primary stderr\n', 'owned_children_reaped': True})
        ids = json.loads(identities.read_text())
        with self.assertRaises(ProcessLookupError):
            os.killpg(ids['group'], 0)
        with self.assertRaises(ProcessLookupError):
            os.kill(ids['child'], 0)
        after_subreaper = ctypes.c_int()
        self.assertEqual(libc.prctl(37, ctypes.byref(after_subreaper), 0, 0, 0), 0)
        self.assertEqual(after_subreaper.value, before_subreaper.value, 'fixture changed parent subreaper state')

    def test_identity_pins_configuration_roots_missing_and_stale(self):
        cases = []
        for key, value in [('schema', 'future-v9'), ('configuration', 'Release'), ('generator', 'Other')]:
            inv = self.copy(self.original); inv[key] = value; cases.append(inv)
        inv = self.copy(self.original); inv['source']['tree'] = '0' * 40; cases.append(inv)
        # Drop a required codemodel document, not an unrelated optional pin.
        inv = self.copy(self.original)
        inv['files'] = [r for r in inv['files'] if not Path(r['path']).name.startswith('codemodel-')]
        cases.append(inv)
        inv = self.copy(self.original); inv['files'].append(dict(inv['files'][0])); cases.append(inv)
        inv = self.copy(self.original); inv['index'] = '../escape.json'; cases.append(inv)
        inv = self.copy(self.original); inv['files'][0]['path'] = '/private/unsupported'; cases.append(inv)
        inv = self.copy(self.original); inv['roots']['build'] = self.inv['roots']['source']; cases.append(inv)
        for inv in cases:
            with self.subTest(mutation=cases.index(inv)):
                self.inv = inv
                result = self.diagnostic(expected=self.original['source'])
                self.assertEqual(result['status'], 'rejected', result)
                self.full(result)
        self.inv = self.copy(self.original)
        self.assertEqual(self.diagnostic(pin='0' * 64)['status'], 'rejected')
        self.assertEqual(self.diagnostic(pin='')['status'], 'rejected')
        path = 'public.hpp'
        self.changed.append(('source', path))
        (Path(self.inv['roots']['source']) / path).write_text('// stale source\n')
        result = self.diagnostic()
        self.assertEqual(result['errors'], ['configured-file-digest-mismatch'])
        self.full(result)

    def test_malformed_duplicate_truncated_and_unsupported_documents(self):
        path, original = self.document('codemodel')
        for value in (b'{', b'{"duplicate":1,"duplicate":2}', b'[]', b'null',
                      b'{"value":NaN}', {'version': {'major': 99}, 'kind': 'codemodel'},
                      {**original, 'configurations': original['configurations'] * 2}):
            with self.subTest(value=type(value).__name__):
                self.repin('build', path, value)
                result = self.diagnostic()
                self.assertEqual(result['status'], 'rejected', result)
                self.full(result)
        self.repin('build', path, original)
        path, doc = self.document('index')
        doc['objects'].append(dict(doc['objects'][0]))
        self.repin('build', path, doc)
        self.assertEqual(self.diagnostic()['status'], 'rejected')
        self.repin('build', path, json.loads(self.original_bytes['build', path]))
        for value in (b'{', b'{"schema":1,"schema":2}', b'[]'):
            self.assertEqual(self.diagnostic(raw=value)['status'], 'rejected')
        path, doc = self.fixture.target_document(self.inv, 'core')
        for mutation in ('duplicate-source', 'duplicate-dependency', 'unsupported-target', 'unsafe-path', 'wrong-target-id'):
            bad = self.copy(doc)
            if mutation == 'duplicate-source':
                bad['sources'].append(dict(bad['sources'][0]))
            elif mutation == 'duplicate-dependency':
                bad['dependencies'] = [{'id': doc['id']}, {'id': doc['id']}]
            elif mutation == 'unsupported-target':
                bad['type'] = 'FUTURE'
            elif mutation == 'unsafe-path':
                bad['sources'][0]['path'] = '../outside.cpp'
            else:
                bad['id'] = 'unrelated-id'
            self.repin('build', path, bad)
            with self.subTest(mutation=mutation):
                result = self.diagnostic()
                self.assertEqual(result['status'], 'rejected', result)
                self.full(result)

    def test_omitted_dependencies_cycles_unknown_headers_and_missing_commands(self):
        path, doc = self.fixture.target_document(self.inv, 'middle')
        original = self.copy(doc)
        app_path, app = self.fixture.target_document(self.inv, 'app')
        app_original = self.copy(app)
        core_path, core = self.fixture.target_document(self.inv, 'core')
        # CMake may report flattened link dependencies on the executable too.
        # Omit the boundary from every consumer, not only the middle library.
        doc['dependencies'] = []
        app['dependencies'] = [d for d in app['dependencies'] if d['id'] != core['id']]
        self.repin('build', path, doc)
        self.repin('build', app_path, app)
        dropped = self.diagnostic(('core.cpp',))
        self.assertEqual(dropped['status'], 'ingested')
        self.assertEqual(dropped['impacted_test_ids'], [])
        self.assertIn('dependency-completeness-unqualified', self.unknowns(dropped),
                      'omission was falsely treated as complete closure')
        self.full(dropped)
        self.repin('build', path, original)
        self.repin('build', app_path, app_original)
        core['dependencies'] = [{'id': app['id']}]
        self.repin('build', core_path, core)
        cycle = self.diagnostic(('core.cpp',))
        self.assertIn('target-dependency-cycle', self.unknowns(cycle))
        self.assertEqual(cycle['impacted_test_ids'], ['app-contract'])
        self.full(cycle)
        self.repin('source', 'public.hpp', b'#include "absent.hpp"\n#include GENERATED_HEADER\n')
        unknown = self.diagnostic(('never-seen.hpp',))
        self.assertTrue({'unresolved-literal-include', 'nonliteral-or-unsupported-include',
                         'changed-path-consumers-unresolved'} <= self.unknowns(unknown))
        ctest = json.loads(self.original_bytes['build', 'ctest.json'])
        ctest['tests'][0].pop('command', None)
        self.repin('build', 'ctest.json', ctest)
        self.assertIn('missing-ctest-command', self.unknowns(self.diagnostic()))
        ctest['tests'].append(dict(ctest['tests'][0]))
        self.repin('build', 'ctest.json', ctest)
        self.assertEqual(self.diagnostic()['status'], 'rejected')

    def test_ordering_and_imported_commands_never_executed(self):
        first = self.diagnostic()
        self.assertEqual(first, self.diagnostic(), 'unchanged pins did not yield byte-stable diagnostics')
        self.inv['files'].reverse()
        self.assertEqual({k: v for k, v in first.items() if k != 'manifest_sha256'},
                         {k: v for k, v in self.diagnostic().items() if k != 'manifest_sha256'})
        model_path, model = self.document('codemodel')
        model['configurations'][0]['targets'].reverse()
        self.repin('build', model_path, model)
        ctest = json.loads(self.original_bytes['build', 'ctest.json'])
        ctest['tests'].reverse()
        self.repin('build', 'ctest.json', ctest)
        reordered = self.diagnostic()
        for key in ('file_target_edges', 'target_target_edges', 'target_test_edges', 'target_reasons',
                    'unknowns', 'impacted_test_ids', 'tests', 'targets'):
            self.assertEqual(first[key], reordered[key], 'metadata ordering altered ' + key)
        marker = self.root / 'not-executed'
        ctest['tests'][0]['command'] = [sys.executable, '-c', 'from pathlib import Path; Path(' + repr(str(marker)) + ').touch()']
        self.repin('build', 'ctest.json', ctest)
        result = self.diagnostic()
        self.assertEqual(result['status'], 'ingested')
        self.assertFalse(marker.exists(), 'importer executed an untrusted inventory command')
        self.assertIn('unmapped-test-command', self.unknowns(result))
        self.full(result)

    def test_missing_source_nonregular_and_bounded_inputs_remain_full(self):
        object_path, object_doc = self.fixture.target_document(self.inv, 'objects')
        object_doc['artifacts'] = [{'path': 'CMakeFiles/objects.dir/./core.cpp.o'}]
        self.repin('build', object_path, object_doc)
        object_result = self.diagnostic()
        self.assertEqual(object_result['status'], 'ingested')
        self.assertIn('object-artifacts-unresolved', self.unknowns(object_result))
        self.full(object_result)
        self.inv['files'] = [r for r in self.inv['files'] if not
                             (r['root'] == 'source' and r['path'] == 'core.cpp')]
        result = self.diagnostic()
        self.assertEqual(result['status'], 'ingested')
        self.assertIn('unpinned-target-source', self.unknowns(result))
        self.full(result)
        self.inv = self.copy(self.original)
        with patch.object(self.configured, 'MAX_TOTAL_BYTES', 1):
            self.assertEqual(self.diagnostic()['errors'], ['configured-total-byte-limit'])
        with patch.object(self.configured, 'MAX_FILES', 1):
            self.assertEqual(self.diagnostic()['errors'], ['configured-file-limit'])
        with patch.object(self.configured, 'MAX_RECORDS', 1):
            self.assertEqual(self.diagnostic()['status'], 'rejected')
        for kind in ('missing', 'directory', 'fifo', 'symlink'):
            path = self.root / ('invalid-' + kind)
            if kind == 'directory':
                path.mkdir()
            elif kind == 'fifo':
                os.mkfifo(path)
            elif kind == 'symlink':
                path.symlink_to(self.path)
            result = self.configured.diagnose(path, '0' * 64, self.inv['source'], [])
            self.assertEqual(result['status'], 'rejected', kind)
            self.full(result)

    def test_real_multi_configuration_is_explicitly_unqualified(self):
        inv, path, _ = self.multi_inputs
        result = self.configured.diagnose(path, self.fixture.digest(path.read_bytes()), inv['source'],
                                         [{'path': 'transitive.hpp'}])
        self.assertEqual(result['status'], 'ingested', result)
        self.assertEqual(result['impacted_test_ids'], ['app-contract'])
        self.assertIn('multi-configuration-inventory', self.unknowns(result))
        self.full(result)


if __name__=='__main__':
    if len(sys.argv) > 3 and sys.argv[1] == '--graph-probe':
        graph_cli_probe(sys.argv[3:], Path(sys.argv[2]))
    else:
        unittest.main()
