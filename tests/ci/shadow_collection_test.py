"""Independent tiny real-Git fixtures for the read-only shadow collector.

No compiler, native program, CMake/CTest discovery, external network or project
fixture is used. All commits/refs/config mutations belong to disposable repos.
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


if __name__=='__main__':
    if len(sys.argv) > 3 and sys.argv[1] == '--graph-probe':
        graph_cli_probe(sys.argv[3:], Path(sys.argv[2]))
    else:
        unittest.main()
