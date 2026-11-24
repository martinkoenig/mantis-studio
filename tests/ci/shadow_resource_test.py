"""Owned OS-resource controls for Issue110; no project tests or native campaign.

The group probes enable a Linux subreaper only in disposable probe processes,
never in the collector or test parent. That fixture owner adopts/reaps only its
own descendants, including on assertion failure. No host setting is changed.
"""
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(os.environ.get('MANTIS_SHADOW_TEST_TOOL_DIR', Path(__file__).resolve().parents[2] / 'tools'))
sys.path.insert(0, str(TOOLS))
import ci_shadow as shadow
import ci_shadow_inventory as reader


def subreaper(enabled):
    if ctypes.CDLL(None, use_errno=True).prctl(36, int(enabled), 0, 0, 0) != 0:
        raise RuntimeError('fixture-local subreaper unavailable')


def reap_owned(child):
    # waitid proves this is still our unreaped child before any fallback signal.
    try:
        status = os.waitid(os.P_PID, child, os.WEXITED | os.WNOWAIT | os.WNOHANG)
    except ChildProcessError:
        return
    if status is None:
        os.kill(child, signal.SIGKILL)
    os.waitpid(child, 0)


def group_probe(exit_status, mode, output):
    subreaper(True)
    with tempfile.TemporaryDirectory(prefix='shadow-owned-group-') as directory:
        root = Path(directory)
        identities = root/'identities.json'
        fake = root/'git'
        fake.write_text('#!' + sys.executable + '\n' + '''import json,os,signal
from pathlib import Path
r,w=os.pipe()
child=os.fork()
if child==0:
    os.close(r)
    for fd in (0,1,2):
        os.close(fd)
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    os.write(w,b'closed-streams')
    os.close(w)
    while True:signal.pause()
os.close(w)
assert os.read(r,32)==b'closed-streams'
os.close(r)
Path(''' + repr(str(identities)) + ''').write_text(json.dumps({'leader':os.getpid(),'child':child,'group':os.getpgrp()}))
os._exit(''' + str(exit_status) + ')\n')
        fake.chmod(0o700)
        result = {}
        collector = None
        try:
            if mode == 'unconfirmed':
                collector = os.fork()
                if collector == 0:
                    subreaper(False)  # orphans belong to the outer fixture owner
            if collector in (None, 0):
                git = shadow.LocalGit(root)
                git.env['PATH'] = str(root) + os.pathsep + git.env.get('PATH','')
                shadow.COMMAND_SECONDS = 0.5
                shadow.CLEANUP_SECONDS = 0.15
                try:
                    git.run('--version')
                    result = {'primary':None,'cleanup':[]}
                except shadow.AcquisitionError as exc:
                    result = {'primary':str(exc),'cleanup':list(getattr(exc,'cleanup_errors',()))}
                if collector == 0:
                    output.write_text(json.dumps(result))
                    os._exit(0)
            if collector:
                waited,status=os.waitpid(collector,0)
                assert waited==collector and status==0,'owned collector fixture failed'
                collector=None
                result=json.loads(output.read_text())
            ids=json.loads(identities.read_text())
            child=ids['child']
            if mode == 'unconfirmed':
                assert result['primary']=='git-command-failed','primary cause replaced by cleanup'
                assert 'owned-group-retirement-unconfirmed' in result['cleanup'],'missing unconfirmed retirement diagnostic'
                # WNOWAIT sees actual SIGKILL termination before fixture owner reaps.
                state=os.waitid(os.P_PID,child,os.WEXITED|os.WNOWAIT|os.WNOHANG)
                assert state and state.si_code==os.CLD_KILLED and state.si_status==signal.SIGKILL,'descendant still running'
                reap_owned(child)
                try:
                    os.killpg(ids['group'],0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError('owned group still exists after fixture reap')
            else:
                try:
                    os.killpg(ids['group'],0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError('closed-stream descendant survived owner cleanup')
                try:
                    os.waitpid(child,os.WNOHANG)
                except ChildProcessError:
                    pass
                else:
                    raise AssertionError('owned descendant was not reaped')
                expected='git-command-failed' if exit_status else None
                assert result['primary']==expected,'leader primary outcome changed'
                assert not result['cleanup'],'retirement unexpectedly unconfirmed'
            result.update(leader_exit=exit_status,group_absent=True,descendant_reaped=True,
                          closed_stream_entry_observed=True,mode=mode)
            output.write_text(json.dumps(result,sort_keys=True))
        finally:
            # On a real regression the independent oracle fails first, then the
            # fixture owner still stops/reaps its own child and collector only.
            if collector:
                reap_owned(collector)
            if identities.exists():
                ids=json.loads(identities.read_text())
                reap_owned(ids['child'])


def reap_fixture_children():
    """Only this isolated fixture owner's adopted children, never a host scan."""
    children_path = Path('/proc/self/task')/str(os.getpid())/'children'
    for _ in range(8):
        children = [int(p) for p in children_path.read_text().split()]
        if not children:
            return
        for child in children:
            reap_owned(child)
    assert not children_path.read_text().strip(), 'fixture-owned retirement unconfirmed'


def inventory_probe(tools, path, output):
    subreaper(True)
    command = [sys.executable,'-B',str(tools/'ci_shadow.py'),'--event','workflow_dispatch',
               '--ref','refs/heads/develop','--base','1'*40,'--head','2'*40,
               '--inventory',str(path),'--inventory-sha256','0'*64]
    result = None
    try:
        with subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                              start_new_session=True) as process:
            try:
                out,err=process.communicate(timeout=2)
                result={'timeout':False,'returncode':process.returncode,
                        'out':out.decode(),'err':err.decode()}
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL)
                out,err=process.communicate(timeout=1)
                result={'timeout':True,'returncode':process.returncode,
                        'out':out.decode(),'err':err.decode()}
    finally:
        reap_fixture_children()
    result['owned_fixture_children_reaped']=True
    output.write_text(json.dumps(result))


class ResourceBounds(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mantis-shadow-resource-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def probe(self, status, mode='normal'):
        output=self.root/'probe.json'
        p=subprocess.run([sys.executable,'-B',str(Path(__file__)),
                          '--group-probe',str(status),mode,str(output)],
                         capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stderr)
        receipt=json.loads(output.read_text())
        self.assertTrue(receipt['descendant_reaped']);self.assertTrue(receipt['group_absent'])
        print(json.dumps({'group_control':receipt},sort_keys=True))
        return receipt

    def test_imports_acquire_no_process_network_or_data_file(self):
        script = '''import os, sys
sys.path.insert(0, sys.argv[1])
def audit(event, args):
    if event.startswith(('subprocess.', 'socket.', 'os.exec', 'os.spawn')) or event in ('os.fork', 'os.system', 'os.kill', 'os.killpg'):
        raise AssertionError('resource acquisition on import: ' + event)
    if event == 'open':
        path, mode, flags = args
        writes = os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND
        if not isinstance(path, str) or not path.endswith(('.py', '.pyc', '.so')) or flags & writes:
            raise AssertionError('non-module file opened on import')
sys.addaudithook(audit)
import ci_shadow, ci_shadow_inventory, ci_shadow_policy, ci_shadow_configured
'''
        result = subprocess.run([sys.executable, '-I', '-B', '-c', script, str(TOOLS)],
                                capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(result.stdout, b'')
        self.assertEqual(result.stderr, b'')

    def test_uncertain_inventory_cleanup_prevents_git_acquisition(self):
        import contextlib
        import io
        output = io.BytesIO()
        stream = io.TextIOWrapper(output)
        failure = shadow.AcquisitionError('inventory-command-failed', returncode=5,
                                          cleanup_errors=['owned-group-retirement-unconfirmed'])
        with patch.object(shadow, '_run_owned', side_effect=failure), \
                patch.object(shadow, 'LocalGit', side_effect=AssertionError('Git acquired after uncertain cleanup')), \
                contextlib.redirect_stdout(stream):
            self.assertEqual(shadow.main(['--event', 'push', '--ref', 'refs/heads/develop',
                                          '--base', '1' * 40, '--head', '2' * 40,
                                          '--inventory', str(self.root / 'inventory.json')]), 0)
        stream.flush()
        result = json.loads(output.getvalue())
        self.assertEqual(result['inventory_acquisition']['error'], 'inventory-read-failed')
        self.assertEqual(result['acquisition']['errors'], ['owned-group-retirement-unconfirmed'])
        self.assertFalse(result['acquisition']['diff']['complete'])
        self.assertTrue(result['decision']['full_matrix_would_select'])
        self.assertFalse(result['execution_authorized'])

    def test_successful_leader_closed_stream_descendant(self):
        self.assertIsNone(self.probe(0)['primary'])

    def test_failed_leader_closed_stream_descendant(self):
        self.assertEqual(self.probe(1)['primary'],'git-command-failed')

    def test_primary_failure_preserved_when_orphan_reap_unconfirmed(self):
        self.assertIn('owned-group-retirement-unconfirmed',self.probe(1,'unconfirmed')['cleanup'])

    def cli(self, path, tools=TOOLS):
        # Isolated owner adopts/reaps a reader even if a regressed collector fails
        # its external deadline. No orphan is left to the host or another task.
        output=self.root/'inventory-probe.json'
        p=subprocess.run([sys.executable,'-B',str(Path(__file__)), '--inventory-probe',
                          str(tools),str(path),str(output)],capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stderr)
        observed=json.loads(output.read_text())
        self.assertTrue(observed['owned_fixture_children_reaped'])
        self.assertFalse(observed['timeout'],'inventory input did not emit bounded full fallback')
        self.assertEqual(observed['returncode'],0,observed['err'])
        result=json.loads(observed['out'])
        self.assertTrue(result['decision']['full_matrix_would_select'])
        receipt=result['inventory_acquisition']
        print(json.dumps({'inventory_control':path.name,'receipt':receipt,
                          'full_matrix':True,'fixture_children_reaped':True},sort_keys=True))
        return receipt

    def test_fifo_symlink_and_directory_reach_type_guard(self):
        fifo=self.root/'input.fifo';os.mkfifo(fifo)
        self.assertEqual(self.cli(fifo)['error'],'inventory-not-regular')
        link=self.root/'link';link.symlink_to(fifo)
        self.assertEqual(self.cli(link)['error'],'inventory-not-regular')
        self.assertEqual(self.cli(self.root)['error'],'inventory-not-regular')
        regular=self.root/'regular.json';regular.write_text('{}')
        link.unlink();link.symlink_to(regular)
        self.assertEqual(self.cli(link)['error'],'inventory-not-regular')

    def test_size_and_missing_input_reach_actual_guards(self):
        large=self.root/'oversized'
        with large.open('wb') as stream:stream.truncate(reader.MAX_BYTES+1)  # sparse; never read/hash contents
        self.assertEqual(self.cli(large)['error'],'inventory-size-limit')
        self.assertEqual(self.cli(self.root/'missing')['error'],'inventory-read-failed')

    def test_open_races_validate_real_descriptor(self):
        path=self.root/'regular';path.write_text('{}')
        real_open=os.open
        def replace_with_fifo(name,flags):
            path.unlink();os.mkfifo(path)
            return real_open(name,flags)
        with patch.object(reader.os,'open',replace_with_fifo):
            with self.assertRaises(reader.ReadError) as caught:reader.read_regular(path)
        self.assertEqual(caught.exception.status,2,'opened FIFO descriptor escaped the type guard')
        path.unlink();path.write_text('{}')
        replacement=self.root/'replacement';replacement.write_text('[]')
        def replace_regular(name,flags):
            os.replace(replacement,path)
            return real_open(name,flags)
        with patch.object(reader.os,'open',replace_regular):
            with self.assertRaises(reader.ReadError) as caught:reader.read_regular(path)
        self.assertEqual(caught.exception.status,3,'replaced opened inode escaped the identity guard')

    def test_read_race_rechecks_open_descriptor_and_path(self):
        path=self.root/'regular';path.write_text('{}')
        replacement=self.root/'replacement';replacement.write_text('[]')
        real_read=os.read
        def replace_after_read(descriptor,count):
            data=real_read(descriptor,count)
            if replacement.exists():os.replace(replacement,path)
            return data
        with patch.object(reader.os,'read',replace_after_read):
            with self.assertRaises(reader.ReadError) as caught:reader.read_regular(path)
        self.assertEqual(caught.exception.status,3,'post-read inode replacement escaped the guard')

    def test_real_blocked_worker_reaches_deadline_and_is_reaped(self):
        tools=self.root/'tools';tools.mkdir()
        for name in ('ci_shadow.py','ci_shadow_policy.py','ci_shadow_inventory.py'):
            (tools/name).write_bytes((TOOLS/name).read_bytes())
        collector=tools/'ci_shadow.py'
        collector.write_text(collector.read_text().replace('INVENTORY_SECONDS = 1','INVENTORY_SECONDS = 0.25'))
        gate=self.root/'gate.fifo';os.mkfifo(gate)
        marker=self.root/'after-fstat.json'
        worker=tools/'ci_shadow_inventory.py';text=worker.read_text()
        anchor='        data = bytearray()\n'
        self.assertEqual(text.count(anchor),1)
        # A source-local disposable I/O stall, AFTER real lstat/open/fstat checks.
        # No fake verified flag or zero-deadline shortcut can satisfy the marker.
        injected=("        from pathlib import Path\n        import json\n"
                  +"        Path("+repr(str(marker))+").write_text(json.dumps({'pid':os.getpid(),'group':os.getpgrp()}))\n"
                  +"        os.open("+repr(str(gate))+",os.O_RDONLY)\n")
        worker.write_text(text.replace(anchor,injected+anchor))
        regular=self.root/'regular';regular.write_text('{}')
        receipt=self.cli(regular,tools)
        self.assertTrue(marker.exists(),'deadline control never reached the real descriptor guard')
        self.assertEqual(receipt['error'],'inventory-command-timeout')
        self.assertEqual(receipt['cleanup_errors'],[])
        identity=json.loads(marker.read_text())
        with self.assertRaises(ProcessLookupError):os.kill(identity['pid'],0)
        with self.assertRaises(ProcessLookupError):os.killpg(identity['group'],0)


if __name__=='__main__':
    if len(sys.argv)==5 and sys.argv[1]=='--group-probe':
        group_probe(int(sys.argv[2]),sys.argv[3],Path(sys.argv[4]))
    elif len(sys.argv)==5 and sys.argv[1]=='--inventory-probe':
        inventory_probe(Path(sys.argv[2]),Path(sys.argv[3]),Path(sys.argv[4]))
    else:
        unittest.main()
