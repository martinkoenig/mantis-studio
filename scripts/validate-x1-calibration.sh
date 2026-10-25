#!/usr/bin/env bash
# Foreground runner receives Ctrl+C and finalizes/retains only its own resources.
set -eu
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec "${MANTIS_VALIDATION_PYTHON:-/usr/bin/python3}" "$script_dir/../tools/validate_x1_calibration.py" "$@"
