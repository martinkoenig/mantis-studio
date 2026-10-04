#!/usr/bin/env bash
# Supervise the repository-owned runner; it owns all system resources it changes.
set -uo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
runner_pid=''
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n "$runner_pid" ]]; then
        # Python's TERM handler enters its finally block, stops its own daemon,
        # restores the service/measurement links, and writes the failure report.
        if kill -0 "$runner_pid" 2>/dev/null; then
            kill -TERM "$runner_pid" 2>/dev/null || true
        fi
        wait "$runner_pid" 2>/dev/null || true
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
"${MANTIS_VALIDATION_PYTHON:-/usr/bin/python3}" "$script_dir/validate_x1_q6a.py" "$@" &
runner_pid=$!
wait "$runner_pid"
status=$?
runner_pid=''
exit "$status"
