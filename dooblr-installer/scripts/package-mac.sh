#!/usr/bin/env bash
# Wraps the built app in a disk image: dooblr-installer-mac.dmg in the given folder.
# The app is ad-hoc signed only (no Apple Developer ID), so the first launch needs
# right-click > Open (see dooblr-installer/README.md).
#   package-mac.sh <path/to/dooblr installer.app> <out dir>
set -euo pipefail
app="$1"; out="$2"
[ -d "$app" ] || { echo "no app at $app" >&2; exit 1; }
mkdir -p "$out"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
ditto "$app" "$stage/dooblr installer.app"
codesign --force --deep --sign - "$stage/dooblr installer.app"
codesign --verify --deep --strict "$stage/dooblr installer.app"
cat > "$stage/Read me first.txt" <<'TXT'
dooblr installer

1. Drag "dooblr installer" anywhere (or run it from here).
2. The first time: right-click (or Control-click) "dooblr installer" and choose Open, then Open
   again. macOS asks because the app is not notarised by Apple. After that it opens normally.
   On macOS 15 (Sequoia) and later: double-click it once, close the warning, then go to
   System Settings > Privacy & Security, scroll down and click "Open Anyway".
3. Pick the plug-ins to install or update and click Install. Installing for everyone asks for
   your Mac password once (to write to /Library/Audio/Plug-Ins/VST3).
TXT
rm -f "$out/dooblr-installer-mac.dmg"
hdiutil create -volname "dooblr installer" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out/dooblr-installer-mac.dmg" >/dev/null
echo "$out/dooblr-installer-mac.dmg"
