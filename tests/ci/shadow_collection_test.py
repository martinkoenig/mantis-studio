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
            cls.temp.cleanup()
        cls.addClassCleanup(retire)
        cls.original, cls.path, cls.base = fixture.produce(cls.root)
        cls.original_bytes = {(row['root'], row['path']):
                              (Path(cls.original['roots'][row['root']]) / row['path']).read_bytes()
                              for row in cls.original['files']}

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
        merged = json.loads(subprocess.run(pr, capture_output=True, timeout=10, check=True).stdout)
        self.assertEqual(merged['configured_diagnostic']['status'], 'ingested', merged)
        self.assertEqual(merged['configured_diagnostic']['source']['commit'], merge)
        self.assertEqual(merged['configured_diagnostic']['impacted_test_ids'], ['app-contract'])
        self.full(merged['configured_diagnostic'])

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
        root = self.root / 'multi'; root.mkdir()
        inv, path, _ = self.fixture.produce(root, 'Ninja Multi-Config')
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
