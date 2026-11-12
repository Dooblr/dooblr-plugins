#!/usr/bin/env bash
# Fetch large third-party artefacts (ONNX Runtime, Piper voice, espeak-ng source)
# and verify SHA-256. WORLD source is vendored in-tree (small, BSD).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
SUMS="$TP/checksums.txt"
CACHE="${DOOBLRTHROAT_FETCH_CACHE:-$HOME/dooblr-build/throat-0.2.2-artifacts/downloads}"
mkdir -p "$CACHE" "$TP"

sha256_of() { shasum -a 256 "$1" | awk '{print $1}'; }
expect_sha() {
    local file="$1" name="$2"
    local got want
    got="$(sha256_of "$file")"
    want="$(awk -v n="$name" '$2==n {print $1; exit}' "$SUMS")"
    if [[ -z "$want" ]]; then
        echo "error: no checksum for $name in $SUMS" >&2
        exit 1
    fi
    if [[ "$got" != "$want" ]]; then
        echo "error: checksum mismatch for $name" >&2
        echo "  expected $want" >&2
        echo "  got      $got" >&2
        exit 1
    fi
}

download() {
    local url="$1" dest="$2"
    if [[ -f "$dest" ]]; then
        return 0
    fi
    echo "==> Downloading $url"
    curl -L --fail --retry 3 -o "$dest.partial" "$url"
    mv "$dest.partial" "$dest"
}

# --- Piper voice ---
PIPER_DIR="$TP/piper-voices"
mkdir -p "$PIPER_DIR"
MODEL_URL="https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/low/en_US-lessac-low.onnx"
JSON_URL="https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/low/en_US-lessac-low.onnx.json"
CARD_URL="https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/low/MODEL_CARD"
if [[ ! -f "$PIPER_DIR/en_US-lessac-low.onnx" ]]; then
    download "$MODEL_URL" "$CACHE/en_US-lessac-low.onnx"
    expect_sha "$CACHE/en_US-lessac-low.onnx" "en_US-lessac-low.onnx"
    cp "$CACHE/en_US-lessac-low.onnx" "$PIPER_DIR/en_US-lessac-low.onnx"
fi
expect_sha "$PIPER_DIR/en_US-lessac-low.onnx" "en_US-lessac-low.onnx"
if [[ ! -f "$PIPER_DIR/en_US-lessac-low.onnx.json" ]]; then
    download "$JSON_URL" "$CACHE/en_US-lessac-low.onnx.json"
    expect_sha "$CACHE/en_US-lessac-low.onnx.json" "en_US-lessac-low.onnx.json"
    cp "$CACHE/en_US-lessac-low.onnx.json" "$PIPER_DIR/en_US-lessac-low.onnx.json"
fi
expect_sha "$PIPER_DIR/en_US-lessac-low.onnx.json" "en_US-lessac-low.onnx.json"
if [[ ! -f "$PIPER_DIR/MODEL_CARD" ]]; then
    download "$CARD_URL" "$CACHE/en_US-lessac-low.MODEL_CARD" || true
    if [[ -f "$CACHE/en_US-lessac-low.MODEL_CARD" ]]; then
        cp "$CACHE/en_US-lessac-low.MODEL_CARD" "$PIPER_DIR/MODEL_CARD"
    fi
fi

# --- ONNX Runtime official osx-arm64 1.31.1 ---
ORT_VER="1.31.1"
ORT_DIR="$TP/onnxruntime"
ORT_TGZ="$CACHE/onnxruntime-osx-arm64-${ORT_VER}.tgz"
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VER}/onnxruntime-osx-arm64-${ORT_VER}.tgz"
if [[ ! -f "$ORT_DIR/lib/libonnxruntime.${ORT_VER}.dylib" ]]; then
    download "$ORT_URL" "$ORT_TGZ"
    rm -rf "$CACHE/onnxruntime-unpack"
    mkdir -p "$CACHE/onnxruntime-unpack"
    tar -xzf "$ORT_TGZ" -C "$CACHE/onnxruntime-unpack"
    src="$(find "$CACHE/onnxruntime-unpack" -name "libonnxruntime.${ORT_VER}.dylib" | head -n 1)"
    if [[ -z "$src" ]]; then
        echo "error: dylib missing from ONNX Runtime tarball" >&2
        exit 1
    fi
    expect_sha "$src" "libonnxruntime.${ORT_VER}.dylib"
    root="$(cd "$(dirname "$src")/.." && pwd)"
    rm -rf "$ORT_DIR"
    mkdir -p "$ORT_DIR"
    # Copy runtime pieces only; never the dSYM.
    cp -R "$root/include" "$ORT_DIR/include"
    mkdir -p "$ORT_DIR/lib"
    cp "$root/lib/libonnxruntime.${ORT_VER}.dylib" "$ORT_DIR/lib/"
    # Preserve ABI symlink names used by the official binary's LC_ID_DYLIB.
    (cd "$ORT_DIR/lib" && ln -sf "libonnxruntime.${ORT_VER}.dylib" libonnxruntime.1.dylib && ln -sf "libonnxruntime.${ORT_VER}.dylib" libonnxruntime.dylib)
    cp "$root/LICENSE" "$ORT_DIR/LICENSE" 2>/dev/null || true
    cp "$root/ThirdPartyNotices.txt" "$ORT_DIR/ThirdPartyNotices.txt" 2>/dev/null || true
fi
expect_sha "$ORT_DIR/lib/libonnxruntime.${ORT_VER}.dylib" "libonnxruntime.${ORT_VER}.dylib"

# --- espeak-ng source (built separately if no prebuilt static lib is given) ---
ESPEAK_SHA="688cb60368d6e0998f3632af8d1c3c2ad4191fc2"
ESPEAK_DIR="$TP/espeak-ng"
if [[ ! -f "$ESPEAK_DIR/src/include/espeak-ng/speak_lib.h" ]]; then
    zip="$CACHE/espeak-ng-${ESPEAK_SHA}.tar.gz"
    download "https://github.com/espeak-ng/espeak-ng/archive/${ESPEAK_SHA}.tar.gz" "$zip"
    rm -rf "$CACHE/espeak-ng-src"
    mkdir -p "$CACHE/espeak-ng-src"
    tar -xzf "$zip" -C "$CACHE/espeak-ng-src"
    srcdir="$(find "$CACHE/espeak-ng-src" -maxdepth 1 -type d -name 'espeak-ng-*' | head -n 1)"
    rm -rf "$ESPEAK_DIR"
    mkdir -p "$ESPEAK_DIR"
    # Copy the source without any .git metadata.
    rsync -a --exclude '.git' "$srcdir"/ "$ESPEAK_DIR"/
fi

echo "==> third-party artefacts ready in $TP"
