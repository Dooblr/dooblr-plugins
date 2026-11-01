#!/usr/bin/env bash
# Configures, builds and tests the dooblr installer in ~/dooblr-build/dooblr-installer.
# It never installs anything. Environment:
#   DOOBLR_BUILD_ROOT  (default ~/dooblr-build)
#   JUCE_SOURCE_DIR    a local JUCE 8.0.15 checkout (default: $DOOBLR_BUILD_ROOT/_deps/juce-src when present)
#   DOOBLRINSTALLER_MANIFEST_URL  bake a different default manifest URL into the app
#   UNIVERSAL=1        build arm64 + x86_64
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
root="${DOOBLR_BUILD_ROOT:-$HOME/dooblr-build}"
build="$root/dooblr-installer"
mkdir -p "$build"

args=(-S "$here" -B "$build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release)
juce="${JUCE_SOURCE_DIR:-$root/_deps/juce-src}"
[ -d "$juce" ] && args+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juce")
[ -n "${DOOBLRINSTALLER_MANIFEST_URL:-}" ] && args+=("-DDOOBLRINSTALLER_MANIFEST_URL=$DOOBLRINSTALLER_MANIFEST_URL")
[ "${UNIVERSAL:-0}" = 1 ] && args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")

cmake "${args[@]}"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
cmake --build "$build" --config Release -j "$jobs"

tests="$(find "$build" -type f -name DooblrInstallerTests -perm -u+x | head -1)"
echo "== tests: $tests"
"$tests"

app="$(find "$build" -type d -name 'dooblr installer.app' | head -1)"
echo "== built: $app"
