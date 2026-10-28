#!/usr/bin/env bash
#
# Configure and build the dooblr-vocals VST3, run the tests, and install the
# built bundle into the VST3 plug-in folder (after the tests pass).
#
# Environment variables:
#   BUILD_DIR        build directory (default: ~/dooblr-build/dooblr-vocals, outside the repo:
#                    the repo lives in Dropbox, whose extended attributes break codesign)
#   JUCE_SOURCE_DIR  JUCE 8.0.15 source to build against (default: ~/dooblr-build/_deps/juce-src
#                    when it exists; otherwise CMake fetches JUCE 8.0.15)
#   CONFIG           CMake configuration (default: Release)
#   RUN_TESTS        1 to build and run the tests, 0 to skip (default: 1)
#   INSTALL_VST3     1 to install the bundle into the VST3 folder after a successful build
#                    and test run, 0 to skip (default: 1 on macOS, 0 elsewhere).
#   INSTALL_VST3_DIR VST3 folder to install into. Default: /Library/Audio/Plug-Ins/VST3,
#                    unless dooblr-vocals.vst3 is already installed only in
#                    ~/Library/Audio/Plug-Ins/VST3 (then that copy is updated). Never uses sudo.
#   LOAD_CHECK       1 to load every delivered bundle like a host (dlopen + JUCE VST3
#                    hosting, see Tools/LoadCheck) and fail if it does not load (default: 1)
#   COPY_TO_PROJECT  1 to also copy the bundle to <repo>/dooblr-vocals.vst3 (default: 0)
#   JOBS             parallel build jobs (default: number of CPUs)
#   CMAKE_GENERATOR  honoured by CMake itself; defaults to Unix Makefiles.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$HOME/dooblr-build/dooblr-vocals"}"
JUCE_SOURCE_DIR="${JUCE_SOURCE_DIR:-"$HOME/dooblr-build/_deps/juce-src"}"
CONFIG="${CONFIG:-Release}"
RUN_TESTS="${RUN_TESTS:-1}"
COPY_TO_PROJECT="${COPY_TO_PROJECT:-0}"
LOAD_CHECK="${LOAD_CHECK:-1}"
BUNDLE_NAME="dooblr-vocals.vst3"

if [[ -z "${INSTALL_VST3:-}" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
        INSTALL_VST3=1
    else
        INSTALL_VST3=0
    fi
fi

if [[ -z "${JOBS:-}" ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

configure_args=(-S "$ROOT_DIR" -B "$BUILD_DIR" "-DCMAKE_BUILD_TYPE=$CONFIG")

if [[ -f "$JUCE_SOURCE_DIR/CMakeLists.txt" ]]; then
    configure_args+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$JUCE_SOURCE_DIR")
fi

if [[ "$RUN_TESTS" == "1" ]]; then
    configure_args+=(-DDOOBLRVOCALS_BUILD_TESTS=ON)
else
    configure_args+=(-DDOOBLRVOCALS_BUILD_TESTS=OFF)
fi

# Only choose a generator for a fresh build directory; an existing cache keeps its own.
# Default is Unix Makefiles (the suite convention). CMAKE_GENERATOR overrides.
if [[ -z "${CMAKE_GENERATOR:-}" && ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    configure_args+=(-G "Unix Makefiles")
fi

echo "==> Configuring ($CONFIG) in $BUILD_DIR"
cmake "${configure_args[@]}"

echo "==> Building DooblrVocals_VST3"
cmake --build "$BUILD_DIR" --config "$CONFIG" --target DooblrVocals_VST3 --parallel "$JOBS"

if [[ "$LOAD_CHECK" == "1" ]]; then
    echo "==> Building DooblrVocalsLoadCheck"
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target DooblrVocalsLoadCheck --parallel "$JOBS"
    loadcheck="$(head -n 1 "$BUILD_DIR/artefact-paths/DooblrVocalsLoadCheck-$CONFIG.txt" 2>/dev/null || true)"

    if [[ -z "$loadcheck" || ! -x "$loadcheck" ]]; then
        echo "error: DooblrVocalsLoadCheck was not built" >&2
        exit 1
    fi
fi

project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")"

if [[ "$RUN_TESTS" == "1" ]]; then
    echo "==> Building DooblrVocalsTests"
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target DooblrVocalsTests --parallel "$JOBS"

    echo "==> Running tests"
    if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
    else
        ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
    fi
fi

# CMakeLists.txt writes the real artefact path for each configuration at generate time.
path_file="$BUILD_DIR/artefact-paths/DooblrVocals_VST3-$CONFIG.txt"
artefact=""

if [[ -f "$path_file" ]]; then
    artefact="$(head -n 1 "$path_file")"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    artefact="$(find "$BUILD_DIR" -path "$BUILD_DIR/_deps" -prune -o -name "$BUNDLE_NAME" -print | head -n 1)"
fi

if [[ -z "$artefact" || ! -e "$artefact" ]]; then
    echo "error: build finished but $BUNDLE_NAME was not found under $BUILD_DIR" >&2
    exit 1
fi

echo
echo "VST3 built successfully:"
echo "  $artefact"

if [[ "$LOAD_CHECK" == "1" && "$INSTALL_VST3" != "1" ]]; then
    echo "==> Loading built bundle like a host"
    if ! "$loadcheck" "$artefact" "$project_version"; then
        echo "error: $artefact does not load; hosts will reject it" >&2
        exit 1
    fi
fi

# Replaces <dir>/dooblr-vocals.vst3 with the freshly built bundle and verifies it.
# Only that one bundle is touched; nothing else in <dir> is modified.
deliver_bundle() {
    local dir="$1"
    local destination="$dir/$BUNDLE_NAME"

    if [[ ! -d "$dir" ]]; then
        mkdir -p "$dir"
    fi

    if [[ ! -w "$dir" ]]; then
        echo "error: $dir is not writable without elevated permissions; not installing." >&2
        echo "       Choose another folder with INSTALL_VST3_DIR=... or install manually." >&2
        exit 1
    fi

    echo "==> Installing to $destination"

    if [[ -e "$destination" ]]; then
        rm -rf -- "$destination"
    fi

    if command -v ditto >/dev/null 2>&1; then
        # Preserves the bundle's code signature. --noqtn drops any quarantine flag and
        # --noextattr drops Finder info that a File Provider synced folder (Dropbox, iCloud) adds (which
        # `codesign --verify --strict` rejects as "detritus").
        ditto --noqtn --noextattr "$artefact" "$destination"
    else
        cp -R "$artefact" "$destination"
    fi

    # A quarantined bundle passes codesign but Gatekeeper refuses to let hosts load it
    # ("library load disallowed by system policy"), so make sure none is left anywhere.
    if command -v xattr >/dev/null 2>&1; then
        xattr -r -d com.apple.quarantine "$destination" 2>/dev/null || true
        xattr -r -d com.apple.FinderInfo "$destination" 2>/dev/null || true

        if xattr -r "$destination" 2>/dev/null | grep -q "com.apple.quarantine"; then
            echo "error: $destination still carries com.apple.quarantine" >&2
            exit 1
        fi
    fi

    if ! diff -r -q "$artefact" "$destination" >/dev/null; then
        echo "error: $destination differs from the build output after copying" >&2
        exit 1
    fi

    if command -v codesign >/dev/null 2>&1; then
        codesign --verify --deep --strict "$destination"
        echo "  identical to build output; no quarantine; signature verified (codesign --verify --deep --strict)"
    else
        echo "  identical to build output"
    fi

    if [[ "$LOAD_CHECK" == "1" ]]; then
        echo "==> Loading $destination like a host"
        if ! "$loadcheck" "$destination" "$project_version"; then
            echo "error: $destination does not load; hosts will reject it" >&2
            exit 1
        fi
    fi

    echo "  $destination"
}

if [[ "$INSTALL_VST3" == "1" ]]; then
    system_vst3_dir="/Library/Audio/Plug-Ins/VST3"
    user_vst3_dir="$HOME/Library/Audio/Plug-Ins/VST3"

    if [[ -z "${INSTALL_VST3_DIR:-}" ]]; then
        # /Library is where plug-ins live on this machine. Only update a per-user copy
        # when that is the sole existing install, so hosts never scan two copies.
        if [[ -d "$user_vst3_dir/$BUNDLE_NAME" && ! -d "$system_vst3_dir/$BUNDLE_NAME" ]]; then
            INSTALL_VST3_DIR="$user_vst3_dir"
        else
            INSTALL_VST3_DIR="$system_vst3_dir"
        fi
    fi

    for other in "$system_vst3_dir" "$user_vst3_dir"; do
        if [[ "$other" != "$INSTALL_VST3_DIR" && -d "$other/$BUNDLE_NAME" ]]; then
            echo "warning: another copy exists at $other/$BUNDLE_NAME; hosts may see both." >&2
        fi
    done

    deliver_bundle "$INSTALL_VST3_DIR"
    echo "Rescan plug-ins in your DAW (or restart it) to load the new build."
fi

if [[ "$COPY_TO_PROJECT" == "1" ]]; then
    deliver_bundle "$ROOT_DIR"
fi
