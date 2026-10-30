#!/usr/bin/env bash
set -eu
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec /usr/bin/python3 "$script_dir/../tools/qualify_qt_sanitizer.py" "$@"
