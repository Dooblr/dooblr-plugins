#!/usr/bin/env bash
#
# Sets up the Python runtime the dooblr-generate worker runs models with: a virtual environment
# with PyTorch (Metal/MPS on Apple Silicon), diffusers (AudioLDM) and stable-audio-tools
# (Stable Audio Open Small). No sudo, nothing installed system-wide. Model weights are NOT
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

echo "==> Creating $VENV with $PYTHON"
"$PYTHON" -m venv "$VENV"
"$VENV/bin/pip" install --upgrade pip
"$VENV/bin/pip" install torch torchaudio "diffusers>=0.30" "transformers>=4.40,<4.50" accelerate soundfile scipy numpy safetensors huggingface_hub einops
"$VENV/bin/pip" install --ignore-requires-python stable-audio-tools==0.0.20

echo "==> Checking imports"
"$VENV/bin/python" - <<'PY'
import torch, diffusers
from stable_audio_tools.models.factory import create_model_from_config
print("torch", torch.__version__, "mps" if torch.backends.mps.is_available() else "cpu", "diffusers", diffusers.__version__)
PY

echo "Runtime ready: $VENV/bin/python3"
