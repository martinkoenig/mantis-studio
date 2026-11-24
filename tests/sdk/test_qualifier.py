#!/usr/bin/env python3
"""Executable negative/cleanup controls for the standalone SDK qualifier itself."""
import argparse
import ctypes
from pathlib import Path
import sys

from qualify import Campaign


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    libc = ctypes.CDLL(None)
    before = ctypes.c_int()
    assert libc.prctl(37, ctypes.byref(before), 0, 0, 0) == 0
    campaign = Campaign(args.output)
    try:
        campaign.run('runner-positive', [sys.executable, '-c', 'print("positive")'])
        campaign.run('runner-intended-negative', [sys.executable, '-c', 'import sys; print("intended diagnosis"); sys.exit(7)'], expected='intended diagnosis')
        controls = [
            ('unrelated-error', 'import sys; print("unrelated"); sys.exit(3)', 'intended diagnosis', 'intended failure not observed', 10),
            ('zero-is-not-negative', 'print("intended diagnosis")', 'intended diagnosis', 'intended failure not observed', 10),
            ('unexpected-positive-failure', 'import sys; sys.exit(9)', None, 'unexpected exit 9', 10),
            ('signal-is-not-negative', 'import os, signal; os.kill(os.getpid(), signal.SIGTERM)', '.', 'intended failure not observed', 10),
            # Both processes ignore TERM. The process-group custodian must KILL and
            # reap its forked descendant, preserving TimeoutExpired as first cause.
            ('timeout-is-not-negative', 'import os, signal; signal.signal(signal.SIGTERM, signal.SIG_IGN); os.fork(); print("intended diagnosis", flush=True); signal.pause()', 'intended diagnosis', 'TimeoutExpired', 1),
        ]
        for label, code, diagnosis, failure, timeout in controls:
            try:
                campaign.run(label, [sys.executable, '-c', code], expected=diagnosis, timeout=timeout)
            except RuntimeError as exc:
                assert failure in str(exc), (label, exc)
                receipt = campaign.records[-1]
                assert not receipt['remaining_after_cleanup'], receipt
                if label.startswith('timeout'):
                    assert receipt['first_cause'] == 'TimeoutExpired', receipt
                    assert receipt['exit'] == -9, receipt
                    assert len(receipt['remaining_before_cleanup']) == 2, receipt
            else:
                raise AssertionError(f'Qualifier falsely accepted {label}')
        print('Qualifier controls passed: unrelated/zero/signal/timeout rejection and owned descendant retirement')
    finally:
        campaign.close()
    after = ctypes.c_int()
    assert libc.prctl(37, ctypes.byref(after), 0, 0, 0) == 0 and before.value == after.value


if __name__ == '__main__':
    main()
