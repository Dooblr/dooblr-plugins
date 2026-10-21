#!/usr/bin/env bash
#
# Configure and build the dooblr-q VST3, optionally run the smoke tests, copy the
# built bundle into the project root, and print the location of the plug-in.
#
# Environment variables:
#   BUILD_DIR        build directory (default: <repo>/build)
#   CONFIG           CMake configuration (default: Release)
#   RUN_TESTS        1 to build and run the smoke tests, 0 to skip (default: 1)
#   COPY_TO_PROJECT  1 to copy the bundle to <repo>/dooblr-q.vst3, 0 to skip (default: 1)
#   JOBS             parallel build jobs (default: number of CPUs)
#   CMAKE_GENERATOR  honoured by CMake itself; defaults to Ninja when available,
#                    otherwise CMake's platform default (Unix Makefiles on macOS/Linux).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT_DIR/build"}"
CONFIG="${CONFIG:-Release}"
RUN_TESTS="${RUN_TESTS:-1}"
COPY_TO_PROJECT="${COPY_TO_PROJECT:-1}"
BUNDLE_NAME="dooblr-q.vst3"

if [[ -z "${JOBS:-}" ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

configure_args=(-S "$ROOT_DIR" -B "$BUILD_DIR" "-DCMAKE_BUILD_TYPE=$CONFIG")

if [[ "$RUN_TESTS" == "1" ]]; then
    configure_args+=(-DDOOBLRQ_BUILD_TESTS=ON)
else
    configure_args+=(-DDOOBLRQ_BUILD_TESTS=OFF)
fi

# Only choose a generator for a fresh build directory; an existing cache keeps its own.
if [[ -z "${CMAKE_GENERATOR:-}" && ! -f "$BUILD_DIR/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    configure_args+=(-G Ninja)
fi

echo "==> Configuring ($CONFIG) in $BUILD_DIR"
cmake "${configure_args[@]}"

echo "==> Building DooblrQ_VST3"
cmake --build "$BUILD_DIR" --config "$CONFIG" --target DooblrQ_VST3 --parallel "$JOBS"

if [[ "$RUN_TESTS" == "1" ]]; then
    echo "==> Building DooblrQTests"
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target DooblrQTests --parallel "$JOBS"

    echo "==> Running tests"
    ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

# CMakeLists.txt writes the real artefact path for each configuration at generate time.
path_file="$BUILD_DIR/artefact-paths/DooblrQ_VST3-$CONFIG.txt"
artefact=""

if [[ -f "$path_file" ]]; then
    artefact="$(head -n 1 "$path_file")"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    # Fallback: search the build tree, skipping the fetched JUCE sources.
    artefact="$(find "$BUILD_DIR" -path "$BUILD_DIR/_deps" -prune -o -name "$BUNDLE_NAME" -print | head -n 1)"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    echo "error: build finished but $BUNDLE_NAME was not found under $BUILD_DIR" >&2
    exit 1
fi

echo
echo "VST3 built successfully:"
echo "  $artefact"

if [[ "$COPY_TO_PROJECT" == "1" ]]; then
    destination="$ROOT_DIR/$BUNDLE_NAME"
    echo "==> Copying to $destination"

    # Replace only our own previous copy (a directory with this exact bundle name).
    if [[ -e "$destination" ]]; then
        rm -rf -- "$destination"
    fi

    if command -v ditto >/dev/null 2>&1; then
        ditto "$artefact" "$destination"   # preserves the bundle's code signature
    else
        cp -R "$artefact" "$destination"
    fi

    if command -v codesign >/dev/null 2>&1; then
        codesign --verify --deep --strict "$destination"
        echo "  signature verified (codesign --verify --deep --strict)"
    fi

    echo "Copied VST3:"
    echo "  $destination"
fi
