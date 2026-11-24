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


def setup_probe(mode, root):
    """Independent owner/oracle around the actual tiny setup helper."""
    import shadow_configured_fixture as fixture
    import time
    subreaper(True)
    marker = root / 'setup-entry.json'
    # Child acknowledges closed streams and installed TERM disposition before
    # the leader publishes identities. No sleep guesses descendant entry.
    launcher = '''import json,os,signal,sys
from pathlib import Path
r,w=os.pipe()
child=os.fork()
if child==0:
    os.close(r)
    for fd in ((0,) if sys.argv[2].endswith('-open') else (0,1,2)):os.close(fd)
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    os.write(w,b'entered');os.close(w)
    while True:signal.pause()
os.close(w)
assert os.read(r,32)==b'entered'
os.close(r)
Path(sys.argv[1]).write_text(json.dumps({'child':child,'group':os.getpgrp()}))
print('setup partial stdout',flush=True)
print('setup partial stderr',file=sys.stderr,flush=True)
mode=sys.argv[2]
if mode.startswith('cancel'):os.kill(os.getppid(),signal.SIGINT if mode=='cancel-int' else signal.SIGTERM)
if mode=='timeout' or mode.startswith('cancel'):
    while True:signal.pause()
os._exit(7 if mode.startswith('nonzero') else 0)
'''
    original = None
    result = None
    started = time.monotonic()
    try:
        try:
            command = [sys.executable, '-B', '-c', launcher, str(marker), mode]
            if mode in ('success', 'nonzero', 'success-open', 'nonzero-open'):
                result = fixture.run(command, root)
            else:
                result = fixture.run(command, root, deadline=time.monotonic() + 0.4)
        except BaseException as exc:
            original = exc
        assert marker.exists(), 'setup descendant never entered controlled state'
        ids = json.loads(marker.read_text())
        # A reaped child is stronger than kill(pid,0): zombies are not retirement.
        try:
            waited, _ = os.waitpid(ids['child'], os.WNOHANG)
        except ChildProcessError:
            pass
        else:
            raise AssertionError('setup descendant survived or was not reaped: ' + str(waited))
        try:
            os.killpg(ids['group'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('setup group still exists after helper return')
        rows = [json.loads(line) for line in (root / 'producer-commands.jsonl').read_text().splitlines()]
        assert len(rows) == 1, 'missing or duplicate setup receipt'
        row = rows[0]
        assert 'setup partial stdout' in row['stdout'], 'partial stdout lost'
        assert 'setup partial stderr' in row['stderr'], 'partial stderr lost'
        assert row['cleanup_errors'] == [], row
        if mode.startswith('success'):
            assert original is None and result == b'setup partial stdout\n', repr(original)
            assert row['exit'] == 0 and row['error'] is None, row
        elif mode.startswith('nonzero'):
            assert isinstance(original, subprocess.CalledProcessError) and original.returncode == 7, repr(original)
            assert row['exit'] == 7 and row['error'] == 'CalledProcessError', row
        elif mode == 'timeout':
            assert isinstance(original, subprocess.TimeoutExpired), repr(original)
            assert row['error'] == 'TimeoutExpired', row
            assert original.output is not None and b'setup partial stdout' in original.output
            assert original.stderr is not None and b'setup partial stderr' in original.stderr
            assert row['reaped_exit'] == -signal.SIGKILL, row
        else:
            assert isinstance(original, KeyboardInterrupt), repr(original)
            assert row['error'] == 'KeyboardInterrupt', row
        assert time.monotonic() - started < 3, 'cleanup consumed outer CTest budget'
        print(json.dumps({'setup_control': mode, 'descendant_entry': True, 'group_absent': True,
                          'child_reaped': True, 'primary': row['error'], 'partial_output': True}))
    finally:
        if marker.exists():
            reap_owned(json.loads(marker.read_text())['child'])
        reap_fixture_children()


def setup_boundary_probe(mode, root):
    """Deliver a real signal inside a transaction, not a fake read/spawn error."""
    import inspect
    import select
    import time
    import shadow_configured_fixture as fixture
    libc = ctypes.CDLL(None, use_errno=True)
    def state():
        value = ctypes.c_int()
        assert libc.prctl(37, ctypes.byref(value), 0, 0, 0) == 0
        return value.value, [signal.getsignal(s) for s in (signal.SIGTERM, signal.SIGINT)]
    before = state()
    subreaper(True)  # independent owner also contains rejected-helper failures
    marker = root / 'boundary-entry.json'
    gate = root / 'boundary-entry.fifo'
    os.mkfifo(gate)
    entry = os.open(gate, os.O_RDONLY | os.O_NONBLOCK)
    launcher = '''import json,os,signal,sys
from pathlib import Path
r,w=os.pipe();child=os.fork()
if child==0:
    os.close(r)
    for fd in (0,1,2):os.close(fd)
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    os.write(w,b'entered');os.close(w)
    while True:signal.pause()
os.close(w);assert os.read(r,32)==b'entered';os.close(r)
Path(sys.argv[1]).write_text(json.dumps({'child':child,'group':os.getpgrp()}))
print('boundary stdout',flush=True)
print('boundary stderr',file=sys.stderr,flush=True)
fd=os.open(sys.argv[2],os.O_WRONLY);os.write(fd,b'entered');os.close(fd)
while True:signal.pause()
'''
    signum = signal.SIGINT if mode.endswith('-int') else signal.SIGTERM
    seen, captured = {}, {}
    lines, first = inspect.getsourcelines(fixture.run)
    record_line = first + next(i for i, line in enumerate(lines) if 'destination.extend(block)' in line)
    read_line = first + next(i for i, line in enumerate(lines) if 'block = os.read(' in line)
    entered = []
    def handshake():
        if not entered:
            assert select.select([entry], [], [], 1)[0], 'boundary lacks descendant/output entry'
            assert os.read(entry, 32) == b'entered', 'boundary handshake missing'
            entered.append(True)
    def trace(frame, event, arg):
        if event == 'return' and frame.f_code is subprocess.Popen.__init__.__code__:
            process = frame.f_locals['self']
            if getattr(process, 'pid', None) is not None and not captured:
                captured['process'] = process
                if mode.startswith('spawn-'):
                    handshake()
                    seen['boundary'] = 'successful-spawn-before-handle-publication'
                    os.kill(os.getpid(), signum)
        if event == 'line' and frame.f_code.co_filename == fixture.__file__ and frame.f_code.co_name == 'read_stream' and not seen:
            block = frame.f_locals.get('block', b'')
            expected = b'boundary stderr' if mode.startswith('read-stderr-') else b'boundary stdout'
            if mode.startswith('read-') and frame.f_lineno == record_line and expected in block:
                handshake()
                seen['boundary'] = 'consumed-read-before-buffer'
                seen['consumed'] = block.decode()
                os.kill(os.getpid(), signum)
            elif mode.startswith('pre-read-') and frame.f_lineno == read_line:
                # select readiness is observed by the real helper before entry.
                handshake()
                seen['boundary'] = 'before-read'
                os.kill(os.getpid(), signum)
        return trace
    original = None
    started = time.monotonic()
    try:
        try:
            sys.settrace(trace)
            fixture.run([sys.executable, '-B', '-c', launcher, str(marker), str(gate)],
                        root, deadline=time.monotonic() + 2)
        except BaseException as exc:
            original = exc
        finally:
            sys.settrace(None)
        assert seen, 'real-signal boundary never reached'
        assert marker.exists(), 'boundary descendant entry not observed'
        row = json.loads((root / 'producer-commands.jsonl').read_text())
        ids = json.loads(marker.read_text())
        # Independent wait status, not merely the helper's receipt/returncode.
        leader_reaped = child_reaped = group_absent = False
        try:
            os.waitid(os.P_PID, captured['process'].pid, os.WEXITED | os.WNOWAIT | os.WNOHANG)
        except ChildProcessError:
            leader_reaped = True
        try:
            os.waitpid(ids['child'], os.WNOHANG)
        except ChildProcessError:
            child_reaped = True
        try:
            os.killpg(ids['group'], 0)
        except ProcessLookupError:
            group_absent = True
        receipt = dict(mode=mode, injection=seen, producer=row, leader_reaped=leader_reaped,
                       child_reaped=child_reaped, group_absent=group_absent,
                       primary=type(original).__name__, state_restored=state() == (1, before[1]))
        (root / 'boundary-result.json').write_text(json.dumps(receipt, sort_keys=True))
        assert leader_reaped and child_reaped and group_absent, 'F3: cancellation crossed spawn ownership publication'
        assert 'boundary stdout' in row['stdout'], 'F2: cancellation lost consumed stdout before buffering'
        assert 'boundary stderr' in row['stderr'], 'boundary cancellation lost stderr'
        assert isinstance(original, KeyboardInterrupt) and str(signum) in str(original), repr(original)
        assert row['error'] == 'KeyboardInterrupt' and row['reaped_exit'] == -signal.SIGKILL, row
        assert row['cleanup_errors'] == [], row
        assert state() == (1, before[1]), 'helper leaked signal/subreaper state'
        assert time.monotonic() - started < 3, 'boundary cleanup consumed outer CTest budget'
        print(json.dumps(receipt, sort_keys=True))
    finally:
        sys.settrace(None)
        os.close(entry)
        # The negative oracle is allowed to fail; its owned tree is not allowed
        # to escape. Keep the unreaped leader handle until fallback signalling.
        try:
            if captured and captured['process'].returncode is None:
                errors = shadow._retire_owned_group(captured['process'])
                assert not errors, errors
            reap_fixture_children()
            remaining = (Path('/proc/self/task') / str(os.getpid()) / 'children').read_text().strip()
            assert not remaining, 'boundary owner left an owned child'
            (root / 'owner-cleanup.json').write_text(json.dumps({'owned_children_absent': True}))
        finally:
            subreaper(bool(before[0]))
        assert state() == before, 'independent owner leaked local state'


def setup_publication_probe(mode, root):
    """Real signals at receipt open, serialization and close; no fake I/O."""
    import inspect
    import time
    import shadow_configured_fixture as fixture
    _, outcome, boundary, delivery, *failure = mode.split('-')
    receipt_path = root / 'producer-commands.jsonl'
    if failure:
        receipt_path.mkdir()  # actual EISDIR after entry to Path.open
    libc = ctypes.CDLL(None, use_errno=True)
    def state():
        value = ctypes.c_int()
        assert libc.prctl(37, ctypes.byref(value), 0, 0, 0) == 0
        return value.value, [signal.getsignal(s) for s in (signal.SIGTERM, signal.SIGINT)]
    before = state()
    def caller_term(signum, frame):
        raise KeyboardInterrupt('caller TERM disposition')
    signal.signal(signal.SIGTERM, caller_term)
    caller_state = state()
    lines, first = inspect.getsourcelines(fixture.run)
    open_line = first + next(i for i, line in enumerate(lines)
                             if "with (root / 'producer-commands.jsonl').open" in line)
    accounting_line = first + next(i for i, line in enumerate(lines) if line.strip() == 'if cleanup:')
    seen, captured = {}, {}
    open_visits = 0
    def inject():
        seen['boundary'] = boundary
        # Do not read the suspended run frame's f_locals: on Python3.12 the
        # trace-return locals sync can overwrite a signal handler's changed cell.
        signals = {'term': (signal.SIGTERM,), 'int': (signal.SIGINT,),
                   'repeat': (signal.SIGTERM, signal.SIGINT, signal.SIGTERM)}[delivery]
        for signum in signals:
            os.kill(os.getpid(), signum)
    def trace(frame, event, arg):
        nonlocal open_visits
        if event == 'return' and frame.f_code is subprocess.Popen.__init__.__code__:
            if getattr(frame.f_locals['self'], 'pid', None):
                captured['process'] = frame.f_locals['self']
        if delivery != 'none' and not seen:
            if event == 'line' and frame.f_code is fixture.run.__code__ and frame.f_lineno == open_line:
                open_visits += 1
                if boundary == ('open' if open_visits == 1 else 'close'):
                    inject()
            elif (boundary == 'serialize' and event == 'return' and frame.f_code is json.dumps.__code__
                  and frame.f_back.f_code is fixture.run.__code__):
                inject()  # bytes computed, not yet passed to real write
            elif (boundary == 'accounting' and event == 'line' and frame.f_code is fixture.run.__code__
                  and frame.f_lineno == accounting_line):
                inject()
        return trace
    script = "import sys,time;print('publication stdout',flush=True);print('publication stderr',file=sys.stderr,flush=True);"
    script += 'time.sleep(10)' if outcome == 'timeout' else 'sys.exit(' + ('7' if outcome == 'nonzero' else '0') + ')'
    original, result = None, None
    started = time.monotonic()
    try:
        try:
            sys.settrace(trace)
            result = fixture.run([sys.executable, '-B', '-c', script], root,
                                 deadline=time.monotonic() + 0.4)
        except BaseException as exc:
            original = exc
        finally:
            sys.settrace(None)
        assert state() == caller_state, 'publication leaked signal/subreaper state'
        process = captured['process']
        try:
            os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT | os.WNOHANG)
        except ChildProcessError:
            pass
        else:
            raise AssertionError('publication leader was not reaped')
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('publication group still exists')
        expected = {'nonzero': subprocess.CalledProcessError, 'timeout': subprocess.TimeoutExpired}.get(outcome)
        first_signal = signal.SIGINT if delivery == 'int' else signal.SIGTERM
        observed = dict(mode=mode, injection=seen, primary=type(original).__name__,
                        notes=getattr(original, '__notes__', []), leader_reaped=True,
                        group_absent=True, state_restored=True, receipt_exists=receipt_path.is_file())
        (root / 'publication-result.json').write_text(json.dumps(observed, sort_keys=True))
        if expected is None and delivery != 'none':
            expected = KeyboardInterrupt
        if expected is None and failure:
            expected = OSError
        assert (isinstance(original, expected) if expected else original is None), \
            'F4: publication signal replaced primary outcome: ' + repr(original)
        if delivery != 'none':
            assert seen, 'publication signal boundary not reached'
            first_signal = signal.SIGINT if delivery == 'int' else signal.SIGTERM
            assert any('signal ' + str(first_signal) in n for n in getattr(original, '__notes__', ())) or \
                (isinstance(original, KeyboardInterrupt) and str(first_signal) in str(original)), \
                'publication cancellation disappeared'
        if outcome in ('nonzero', 'timeout'):
            assert isinstance(original, (subprocess.CalledProcessError, subprocess.TimeoutExpired))
            assert original.output is not None and original.stderr is not None
            assert b'publication stdout' in original.output and b'publication stderr' in original.stderr
        if outcome == 'nonzero':
            assert isinstance(original, subprocess.CalledProcessError)
            assert original.returncode == 7
        if failure:
            if expected is not OSError:
                assert original is not None
                assert any('receipt failed' in n for n in original.__notes__), 'secondary receipt error lost'
            rows = []
        else:
            rows = [json.loads(line) for line in receipt_path.read_text().splitlines()]
            assert 1 <= len(rows) <= 2, 'missing/unbounded publication receipts'
            row = rows[0]
            assert 'publication stdout' in row['stdout'] and 'publication stderr' in row['stderr']
            assert row['cleanup_errors'] == []
            assert row['reaped_exit'] == (-signal.SIGKILL if outcome == 'timeout' else (7 if outcome == 'nonzero' else 0))
            if delivery != 'none':
                assert expected is not None
                assert rows[-1]['cancellation_signal'] == first_signal, 'late signal not retained in receipt'
                assert rows[-1]['error'] == expected.__name__, 'receipt primary outcome inaccurate'
            elif outcome == 'success':
                assert result == b'publication stdout\n' and row['error'] is None
        assert time.monotonic() - started < 3, 'publication consumed outer CTest budget'
        print(json.dumps(dict(mode=mode, injection=seen, primary=type(original).__name__,
                              producer=rows, leader_reaped=True, group_absent=True, state_restored=True)))
    finally:
        sys.settrace(None)
        try:
            if captured and captured['process'].returncode is None:
                assert not shadow._retire_owned_group(captured['process'])
        finally:
            for signum, handler in zip((signal.SIGTERM, signal.SIGINT), before[1]):
                signal.signal(signum, handler)
        assert state() == before, 'publication oracle leaked caller state'


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

    def test_tiny_setup_owns_descendants_and_retains_failures(self):
        previous = ctypes.c_int()
        self.assertEqual(ctypes.CDLL(None).prctl(37, ctypes.byref(previous), 0, 0, 0), 0)
        subreaper(True)
        try:
            modes = ('success', 'nonzero', 'success-open', 'nonzero-open', 'timeout', 'cancel', 'cancel-int',
                     'pre-read-term', 'pre-read-int', 'read-term', 'read-int',
                     'read-stderr-term', 'read-stderr-int', 'spawn-term', 'spawn-int')
            # Final publication is a distinct transaction from retirement. Trace
            # selects real signal timing; commands, reads and receipt I/O stay real.
            publication = []
            for outcome in ('success', 'nonzero', 'timeout'):
                publication.extend('publish-' + outcome + '-open-' + delivery
                                   for delivery in ('none', 'term', 'int', 'repeat'))
                publication.extend(('publish-' + outcome + '-serialize-repeat',
                                    'publish-' + outcome + '-close-repeat',
                                    'publish-' + outcome + '-accounting-repeat',
                                    'publish-' + outcome + '-open-repeat-fail',
                                    'publish-' + outcome + '-open-none-fail'))
            for mode in (*modes, *publication):
                with self.subTest(mode=mode):
                    root = self.root / mode
                    root.mkdir()
                    process = subprocess.run([sys.executable, '-B', str(Path(__file__)),
                                              '--setup-probe', mode, str(root)],
                                             capture_output=True, text=True, timeout=5)
                    self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                    print(process.stdout, end='')
        finally:
            # If a regressed helper exceeds the external bound, run() reaps the
            # probe, and this independent owner adopts/retires its descendants.
            # Never leave the negative's grandchildren to the host's PID1.
            try:
                reap_fixture_children()
            finally:
                subreaper(bool(previous.value))

    def test_tiny_setup_preserves_primary_and_restores_local_state(self):
        import shadow_configured_fixture as fixture
        import time
        libc = ctypes.CDLL(None, use_errno=True)
        def state():
            value = ctypes.c_int()
            self.assertEqual(libc.prctl(37, ctypes.byref(value), 0, 0, 0), 0)
            return value.value, [signal.getsignal(s) for s in (signal.SIGTERM, signal.SIGINT)]
        before = state()
        command = [sys.executable, '-B', '-c', "import sys;print('evidence',flush=True);sys.exit(9)"]
        # A real logging failure after a real child failure cannot replace it.
        (self.root / 'producer-commands.jsonl').mkdir()
        with self.assertRaises(subprocess.CalledProcessError) as caught:
            fixture.run(command, self.root)
        self.assertEqual(caught.exception.returncode, 9)
        self.assertEqual(caught.exception.output, b'evidence\n')
        self.assertTrue(any('receipt failed' in n for n in caught.exception.__notes__))
        self.assertEqual(state(), before, 'process-local containment state leaked')
        (self.root / 'producer-commands.jsonl').rmdir()
        # Expired campaign budget must not start a fresh command/per-command clock.
        marker = self.root / 'should-not-launch'
        with self.assertRaises(subprocess.TimeoutExpired):
            fixture.run([sys.executable, '-c', 'from pathlib import Path;Path(' + repr(str(marker)) + ').touch()'],
                        self.root, deadline=time.monotonic() - 1)
        self.assertFalse(marker.exists())
        self.assertEqual(state(), before)
        row = json.loads((self.root / 'producer-commands.jsonl').read_text())
        self.assertEqual(row['error'], 'TimeoutExpired')
        self.assertIsNone(row['exit'])
        self.assertEqual(row['cleanup_errors'], [])
        # Cancellation arriving during cleanup must neither vanish on success nor
        # replace an already established command/timeout error. A second signal
        # cannot interrupt retirement, output draining or receipt publication.
        for outcome in ('success', 'nonzero', 'timeout'):
            with self.subTest(cleanup_cancellation=outcome):
                root = self.root / ('late-' + outcome)
                root.mkdir()
                sent = []
                def trace(frame, event, arg):
                    if frame.f_code is shadow._retire_owned_group.__code__ and event == 'return' and not sent:
                        sent.append(True)
                        os.kill(os.getpid(), signal.SIGTERM)
                        os.kill(os.getpid(), signal.SIGINT)
                    return trace
                script = "import sys,time;print('late stdout',flush=True);print('late stderr',file=sys.stderr,flush=True);"
                script += 'time.sleep(10)' if outcome == 'timeout' else 'sys.exit(' + ('7' if outcome == 'nonzero' else '0') + ')'
                expected = {'success': KeyboardInterrupt, 'nonzero': subprocess.CalledProcessError,
                            'timeout': subprocess.TimeoutExpired}[outcome]
                try:
                    sys.settrace(trace)
                    with self.assertRaises(expected) as caught:
                        fixture.run([sys.executable, '-B', '-c', script], root,
                                    deadline=time.monotonic() + 0.4)
                finally:
                    sys.settrace(None)
                self.assertTrue(sent, 'late cancellation boundary not reached')
                row = json.loads((root / 'producer-commands.jsonl').read_text())
                self.assertEqual(row['error'], expected.__name__)
                self.assertEqual(row['cancellation_signal'], signal.SIGTERM)
                self.assertEqual(row['cleanup_errors'], [])
                self.assertIn('late stdout', row['stdout'])
                self.assertIn('late stderr', row['stderr'])
                if outcome == 'nonzero':
                    self.assertEqual(caught.exception.returncode, 7)
                if outcome != 'success':
                    self.assertIn(b'late stdout', caught.exception.output)
                    self.assertIn(b'late stderr', caught.exception.stderr)
                self.assertEqual(state(), before, 'late cancellation leaked local state')

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
import ci_shadow, ci_shadow_inventory, ci_shadow_policy, ci_shadow_configured, ci_shadow_corpus
sys.path.insert(0, sys.argv[2])
import shadow_configured_fixture
'''
        result = subprocess.run([sys.executable, '-I', '-B', '-c', script, str(TOOLS), str(Path(__file__).parent)],
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
    elif len(sys.argv)==4 and sys.argv[1]=='--setup-probe':
        if sys.argv[2].startswith('publish-'):
            setup_publication_probe(sys.argv[2], Path(sys.argv[3]))
        elif sys.argv[2].startswith(('pre-read-', 'read-', 'spawn-')):
            setup_boundary_probe(sys.argv[2], Path(sys.argv[3]))
        else:
            setup_probe(sys.argv[2], Path(sys.argv[3]))
    else:
        unittest.main()
