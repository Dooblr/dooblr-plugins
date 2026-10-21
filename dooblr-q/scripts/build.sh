#!/usr/bin/env bash
#
# Configure and build the OctaEQ VST3, optionally run the smoke tests, and print
# the location of the built plug-in.
#
# Environment variables:
#   BUILD_DIR   build directory (default: <repo>/build)
#   CONFIG      CMake configuration (default: Release)
#   RUN_TESTS   1 to build and run the smoke tests, 0 to skip (default: 1)
#   JOBS        parallel build jobs (default: number of CPUs)
#   CMAKE_GENERATOR  honoured by CMake itself; defaults to Ninja when available,
#                    otherwise CMake's platform default (Unix Makefiles on macOS/Linux).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT_DIR/build"}"
CONFIG="${CONFIG:-Release}"
RUN_TESTS="${RUN_TESTS:-1}"

if [[ -z "${JOBS:-}" ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

configure_args=(-S "$ROOT_DIR" -B "$BUILD_DIR" "-DCMAKE_BUILD_TYPE=$CONFIG")

if [[ "$RUN_TESTS" == "1" ]]; then
    configure_args+=(-DOCTAEQ_BUILD_TESTS=ON)
else
    configure_args+=(-DOCTAEQ_BUILD_TESTS=OFF)
fi

# Only choose a generator for a fresh build directory; an existing cache keeps its own.
if [[ -z "${CMAKE_GENERATOR:-}" && ! -f "$BUILD_DIR/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    configure_args+=(-G Ninja)
fi

echo "==> Configuring ($CONFIG) in $BUILD_DIR"
cmake "${configure_args[@]}"

echo "==> Building OctaEQ_VST3"
cmake --build "$BUILD_DIR" --config "$CONFIG" --target OctaEQ_VST3 --parallel "$JOBS"

if [[ "$RUN_TESTS" == "1" ]]; then
    echo "==> Building OctaEQTests"
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target OctaEQTests --parallel "$JOBS"

    echo "==> Running tests"
    ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

# CMakeLists.txt writes the real artefact path for each configuration at generate time.
path_file="$BUILD_DIR/artefact-paths/OctaEQ_VST3-$CONFIG.txt"
artefact=""

if [[ -f "$path_file" ]]; then
    artefact="$(head -n 1 "$path_file")"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    # Fallback: search the build tree, skipping the fetched JUCE sources.
    artefact="$(find "$BUILD_DIR" -path "$BUILD_DIR/_deps" -prune -o -name 'OctaEQ.vst3' -print | head -n 1)"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    echo "error: build finished but OctaEQ.vst3 was not found under $BUILD_DIR" >&2
    exit 1
fi

echo
echo "VST3 built successfully:"
echo "  $artefact"
