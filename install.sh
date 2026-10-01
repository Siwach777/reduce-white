#!/bin/bash
# Unix entry point for the shared installer; an optional positional prefix is supported.
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required for this installer. See docs/installation.md for manual CMake commands." >&2
    exit 1
fi
if (( $# > 0 )) && [[ "$1" != -* ]]; then
    prefix="$1"
    shift
    exec python3 "$source_dir/install.py" --prefix "$prefix" "$@"
fi
exec python3 "$source_dir/install.py" "$@"
