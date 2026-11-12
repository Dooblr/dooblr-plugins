#!/usr/bin/env bash
# Copy ONNX Runtime, the Piper voice and a trimmed English espeak-ng-data tree
# into a .vst3 bundle, fix rpaths, and leave codesign to sign-bundle.sh.
set -euo pipefail

BUNDLE="${1:?vst3 bundle}"
ORT_LIB="${2:?onnxruntime lib dir}"
PIPER_ONNX="${3:?piper onnx}"
ESPEAK_DATA="${4:?espeak-ng-data dir}"

CONTENTS="$BUNDLE/Contents"
MACOS="$CONTENTS/MacOS"
FW="$CONTENTS/Frameworks"
RES="$CONTENTS/Resources"
mkdir -p "$FW" "$RES"

# dylib (no dSYM)
dylib=""
for n in libonnxruntime.1.31.1.dylib libonnxruntime.1.dylib libonnxruntime.dylib; do
    if [[ -f "$ORT_LIB/$n" ]]; then
        dylib="$ORT_LIB/$n"
        break
    fi
done
if [[ -z "$dylib" ]]; then
    echo "error: libonnxruntime dylib not found in $ORT_LIB" >&2
    exit 1
fi
# Resolve to the real file so we copy bytes, then recreate short aliases.
real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$dylib")"
base="$(basename "$real")"
cp -f "$real" "$FW/$base"
(cd "$FW" && ln -sf "$base" libonnxruntime.1.dylib && ln -sf "$base" libonnxruntime.dylib)
# Never copy dSYM
rm -rf "$FW/"*.dSYM "$CONTENTS/"*.dSYM "$BUNDLE/"*.dSYM || true

# Piper model + config
cp -f "$PIPER_ONNX" "$RES/en_US-lessac-low.onnx"
if [[ -f "${PIPER_ONNX}.json" ]]; then
    cp -f "${PIPER_ONNX}.json" "$RES/en_US-lessac-low.onnx.json"
fi
if [[ -f "$(dirname "$PIPER_ONNX")/MODEL_CARD" ]]; then
    cp -f "$(dirname "$PIPER_ONNX")/MODEL_CARD" "$RES/MODEL_CARD"
fi

# Trimmed English espeak-ng-data
DATA_SRC="$ESPEAK_DATA"
if [[ -d "$ESPEAK_DATA/espeak-ng-data" ]]; then
    DATA_SRC="$ESPEAK_DATA/espeak-ng-data"
fi
DEST="$RES/espeak-ng-data"
rm -rf "$DEST"
mkdir -p "$DEST/lang/gmw"
for f in phondata phonindex phontab intonations phondata-manifest en_dict; do
    if [[ -f "$DATA_SRC/$f" ]]; then
        cp -f "$DATA_SRC/$f" "$DEST/$f"
    fi
done
for lang in en en-US; do
    if [[ -f "$DATA_SRC/lang/gmw/$lang" ]]; then
        cp -f "$DATA_SRC/lang/gmw/$lang" "$DEST/lang/gmw/$lang"
    elif [[ -d "$DATA_SRC/lang/gmw/$lang" ]]; then
        rm -rf "$DEST/lang/gmw/$lang"
        cp -R "$DATA_SRC/lang/gmw/$lang" "$DEST/lang/gmw/$lang"
    fi
done

# rpath on every Mach-O in MacOS (POSIX-friendly; CMake may invoke this with sh)
if [ -d "$MACOS" ]; then
    find "$MACOS" -type f | while IFS= read -r bin; do
        if file "$bin" | grep -q 'Mach-O'; then
            install_name_tool -delete_rpath "$ORT_LIB" "$bin" 2>/dev/null || true
            install_name_tool -add_rpath "@loader_path/../Frameworks" "$bin" 2>/dev/null || true
        fi
    done
fi

echo "bundled runtime into $BUNDLE"
