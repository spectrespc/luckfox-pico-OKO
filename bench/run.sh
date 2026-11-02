#!/usr/bin/env bash
# Run a bench script inside the project virtualenv.
#
#   bench/run.sh <script>.py [arguments]
#
# The venv lives in output/bench/venv and is created on first use.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OKO_BENCH_OUT:-$ROOT/output/bench}"
VENV="${OKO_BENCH_VENV:-$OUT/venv}"

if [ "$#" -lt 1 ]; then
    echo "usage: bench/run.sh <script>.py [arguments]" >&2
    echo "scripts: serial_exec.py dut.py osd_check.py" >&2
    exit 2
fi

SCRIPT="$1"
shift
if [ ! -f "$ROOT/bench/$SCRIPT" ]; then
    echo "no such bench script: $SCRIPT" >&2
    exit 2
fi

if [ ! -x "$VENV/bin/python" ]; then
    "$ROOT/bench/setup.sh"
fi

exec "$VENV/bin/python" "$ROOT/bench/$SCRIPT" "$@"