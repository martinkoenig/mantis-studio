"""Binary boundary checks supplement actual non-testing target-graph isolation."""
import subprocess
import sys
for binary in sys.argv[1:-1]:
    symbols = subprocess.check_output(['nm','-C',binary],text=True)
    strings = subprocess.check_output(['strings',binary],text=True)
    assert 'mantis::recorder_diagnostics::' not in symbols, binary
    assert 'MANTIS_RECORDER_OBSERVE_DIR' not in strings, binary
    assert 'account.lock.acquired' not in strings and 'cycle.end' not in strings, binary
symbols = subprocess.check_output(['nm','-C',sys.argv[-1]],text=True)
assert 'mantis::recorder_diagnostics::StoreSpan::StoreSpan' in symbols
assert 'mantis::recorder_diagnostics::pop(' in symbols
assert 'mantis::recorder_diagnostics::cleanup(' in symbols
print('PASS: observer absent from product binaries, present only in isolated diagnostic daemon')
