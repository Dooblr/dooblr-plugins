#!/usr/bin/env bash
# Local end-to-end check of the installer command line with file:// zips (temp folders only).
set -euo pipefail
repo=~/Dropbox/dooblr_plugins
b=~/dooblr-build
app="$b/dooblr-installer/DooblrInstaller_artefacts/Release/dooblr installer.app/Contents/MacOS/dooblr installer"
rel="$b/dooblr-installer/release"
rm -rf "$rel"; mkdir -p "$rel"
args=()
for p in q kick vocals prophet soothe feedback; do
  args+=(--vst3 "$(cat $b/dooblr-$p/artefact-paths/*_VST3-Release.txt 2>/dev/null || ls -d $b/dooblr-$p/*_artefacts/Release/VST3/dooblr-$p.vst3)")
done
python3 "$repo/dooblr-installer/scripts/make-release.py" package --platform mac --out "$rel" --m4l --require-all "${args[@]}"
python3 "$repo/dooblr-installer/scripts/make-release.py" manifest --out "$rel" "$rel/partial-mac.json"
echo "== --list against the real folders (read only)"
"$app" --list --manifest "file://$rel/dooblr-manifest.json"
t="$(mktemp -d /tmp/dooblr-e2e.XXXX)"
export DOOBLR_INSTALLER_RECEIPTS="$t/receipts.json"
echo "== install all into $t"
"$app" --install all --manifest "file://$rel/dooblr-manifest.json" --vst3-dir "$t/VST3" --m4l-dir "$t/UserLibrary/dooblr"
ls "$t/VST3" "$t/UserLibrary/dooblr"
for v in "$t"/VST3/*.vst3; do codesign --verify --deep --strict "$v" && echo "codesign OK $(basename "$v")"; done
for p in q kick vocals prophet soothe feedback; do
  cmp -s "$t/VST3/dooblr-$p.vst3/Contents/MacOS/dooblr-$p" "/Library/Audio/Plug-Ins/VST3/dooblr-$p.vst3/Contents/MacOS/dooblr-$p" && echo "binary identical to /Library: dooblr-$p" || echo "binary differs from /Library: dooblr-$p"
done
echo "== list after install"
"$app" --list --manifest "file://$rel/dooblr-manifest.json" --vst3-dir "$t/VST3" --m4l-dir "$t/UserLibrary/dooblr"
echo "== corrupted zip"
c="$t/corrupt"; mkdir -p "$c"; cp "$rel"/* "$c/"
z="$(ls "$c"/dooblr-soothe-*-mac.zip)"
python3 - "$z" <<'PY'
import sys; p=sys.argv[1]; b=bytearray(open(p,'rb').read()); b[len(b)//2]^=0x5a; open(p,'wb').write(b)
PY
before="$(shasum -a 256 "$t/VST3/dooblr-soothe.vst3/Contents/MacOS/dooblr-soothe")"
set +e
"$app" --install dooblr-soothe --manifest "file://$c/dooblr-manifest.json" --vst3-dir "$t/VST3" --m4l-dir "$t/UserLibrary/dooblr"
echo "exit code $?"
set -e
after="$(shasum -a 256 "$t/VST3/dooblr-soothe.vst3/Contents/MacOS/dooblr-soothe")"
[ "$before" = "$after" ] && echo "installed soothe unchanged after the rejected download"
echo "== missing manifest"
set +e; "$app" --list --manifest "file://$t/nope.json"; echo "exit code $?"; set -e
echo "temp: $t"
