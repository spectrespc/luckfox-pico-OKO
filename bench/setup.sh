#!/usr/bin/env bash
# Create the bench virtualenv under output/bench/venv and install the pinned
# requirements.  Safe to re-run: an existing venv is reused.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="${OKO_BENCH_VENV:-$ROOT/output/bench/venv}"

python3 -m venv "$VENV"
"$VENV/bin/pip" install --upgrade pip
"$VENV/bin/pip" install -r "$ROOT/bench/requirements.txt"

echo "bench venv: $VENV"