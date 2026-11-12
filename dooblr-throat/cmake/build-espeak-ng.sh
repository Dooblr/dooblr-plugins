#!/usr/bin/env bash
# Build a static, audio-less espeak-ng (klatt on) into $1, from the fetched source in $2.
set -euo pipefail
DEST="${1:?build dir}"
SRC="${2:?espeak-ng source}"
if [[ -f "$DEST/src/libespeak-ng/libespeak-ng.a" && -d "$DEST/espeak-ng-data" ]]; then
    exit 0
fi
echo "==> Building static espeak-ng in $DEST"
cmake -S "$SRC" -B "$DEST" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-11.0}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DUSE_ASYNC=ON \
    -DUSE_MBROLA=OFF \
    -DUSE_LIBSONIC=OFF \
    -DUSE_LIBPCAUDIO=OFF \
    -DUSE_KLATT=ON \
    -DUSE_SPEECHPLAYER=ON \
    -DBUILD_ESPEAK_NG_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="$DEST/install"
cmake --build "$DEST" --config Release --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
# Some trees put compiled data under $DEST/espeak-ng-data after the build.
if [[ ! -d "$DEST/espeak-ng-data" ]]; then
    if [[ -d "$DEST/install/share/espeak-ng-data" ]]; then
        ln -sfn "$DEST/install/share/espeak-ng-data" "$DEST/espeak-ng-data"
    elif [[ -d "$SRC/espeak-ng-data" ]]; then
        # Data may still be source-form; prefer the compiled tree if present.
        cmake --install "$DEST"
        ln -sfn "$DEST/install/share/espeak-ng-data" "$DEST/espeak-ng-data"
    fi
fi
test -f "$DEST/src/libespeak-ng/libespeak-ng.a"
