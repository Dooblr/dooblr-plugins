#!/usr/bin/env bash
#
# Sets up the Python runtime the dooblr-generate worker runs models with: a virtual environment
# with PyTorch (Metal/MPS on Apple Silicon), diffusers (AudioLDM) and stable-audio-tools
# (Stable Audio Open Small), pinned by worker/requirements-lock.txt. Re-running it repairs a
# broken runtime. No sudo, nothing installed system-wide. Model weights are NOT
# downloaded here; the plug-in downloads them (or you pick a folder) on first use.
#
# Environment variables:
#   VENV    where to create it (default: ~/Library/Application Support/dooblr/generate/runtime/venv,
#           which the plug-in finds on its own; ~/dooblr-build/generate-venv is also found)
#   PYTHON  interpreter to build it from (default: the first of python3.11, python3.10 on PATH).
#           stable-audio-tools 0.0.20 declares Python <3.11; it installs and imports on 3.11 with
#           --ignore-requires-python (checked on macOS 27, Apple Silicon).

set -euo pipefail

VENV="${VENV:-"$HOME/Library/Application Support/dooblr/generate/runtime/venv"}"

if [[ -z "${PYTHON:-}" ]]; then
    for candidate in python3.11 python3.10; do
        if command -v "$candidate" >/dev/null 2>&1; then
            PYTHON="$(command -v "$candidate")"
            break
        fi
    done
fi

if [[ -z "${PYTHON:-}" ]]; then
    echo "error: needs Python 3.10 or 3.11 (set PYTHON=/path/to/python3.11)" >&2
    exit 1
fi

LOCK="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/worker/requirements-lock.txt"
WORKER="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/worker/dooblr_generate_worker.py"

echo "==> Creating $VENV with $PYTHON"
"$PYTHON" -m venv "$VENV"
"$VENV/bin/pip" install --upgrade pip
# The exact, tested set (see the lock file's header). --ignore-requires-python: stable-audio-tools
# 0.0.20 declares Python <3.11 but installs and runs on 3.11. --force-reinstall also repairs an
# existing venv (e.g. one where NumPy 2 replaced the NumPy 1.x that PyWavelets 1.4.1 needs).
"$VENV/bin/pip" install --ignore-requires-python --force-reinstall --no-cache-dir -r "$LOCK"

echo "==> Self-check (imports both model stacks)"
"$VENV/bin/python" "$WORKER" check-runtime

echo "Runtime ready: $VENV/bin/python3"
