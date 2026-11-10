#!/usr/bin/env python3
"""Packages the dooblr suite for the installer.

  make-release.py package  --platform mac|windows --out DIR [--vst3 PATH ...] [--build-root DIR] [--m4l]
      Zips each .vst3 bundle (top folder = bundle name) and, with --m4l, the Max for Live
      devices (dooblr-prophet panel, dooblr-chords built in a temp copy). Writes
      DIR/partial-<platform>.json with name, version, sha256 and size per zip.

  make-release.py manifest --out DIR [--installer-mac FILE] [--installer-windows FILE] [--base-url URL] PARTIAL...
      Merges partial files into DIR/dooblr-manifest.json (the file the installer reads).
      Asset URLs are relative to the manifest unless --base-url is given, so the same files work
      from a GitHub release, any other web folder, or a local file:// folder.

Versions come from each plug-in's CMakeLists.txt project(... VERSION ...) (chords: package.json,
prophet panel: UI_VERSION in m4l/src/dooblr-prophet-ui.js) and are checked against the built bundle when it has an Info.plist.
"""
import argparse, datetime, glob, hashlib, json, os, plistlib, re, shutil, subprocess, sys, tempfile, zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# id -> (display name, description, kind, bundle, folder, parent)
SUITE = [
    ('dooblr-q',        'dooblr-q',        'Dynamic parametric EQ',                                 'vst3', 'dooblr-q.vst3',        'dooblr-q',        None),
    ('dooblr-kick',     'dooblr-kick',     'Layered kick drum designer (instrument)',               'vst3', 'dooblr-kick.vst3',     'dooblr-kick',     None),
    ('dooblr-vocals',   'dooblr-vocals',   'Pitch correction and vocal chain',                      'vst3', 'dooblr-vocals.vst3',   'dooblr-vocals',   None),
    ('dooblr-prophet',  'dooblr-prophet',  'Analog-modelled polyphonic synth (instrument)',         'vst3', 'dooblr-prophet.vst3',  'dooblr-prophet',  None),
    ('dooblr-bass',     'dooblr-bass',     'Analog-style monophonic bass synth (instrument)',       'vst3', 'dooblr-bass.vst3',     'dooblr-bass',     None),
    ('dooblr-m',        'dooblr-m',        'All-purpose Model D-style monophonic synth (instrument)','vst3', 'dooblr-m.vst3',        'dooblr-m',        None),
    ('dooblr-prophet-panel', 'dooblr-prophet panel', 'Max for Live panel for dooblr-prophet (optional, Ableton Live only)',
                                                                                                    'm4l',  'dooblr-prophet',       'dooblr-prophet',  'dooblr-prophet'),
    ('dooblr-soothe',   'dooblr-soothe',   'Dynamic resonance reducer',                             'vst3', 'dooblr-soothe.vst3',   'dooblr-soothe',   None),
    ('dooblr-feedback', 'dooblr-feedback', 'Automatic live feedback suppressor',                    'vst3', 'dooblr-feedback.vst3', 'dooblr-feedback', None),
    ('dooblr-distressor', 'dooblr-distressor', 'EL8-style compressor',                              'vst3', 'dooblr-distressor.vst3', 'dooblr-distressor', None),
    ('dooblr-chords',   'dooblr-chords',   'Chord progression generator (Max for Live MIDI effect, Ableton Live only)',
                                                                                                    'm4l',  'dooblr-chords',        'dooblr-chords',   None),
]

def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(1)

def cmake_version(folder):
    text = open(os.path.join(REPO, folder, 'CMakeLists.txt'), encoding='utf-8').read()
    m = re.search(r'project\(\s*[\w-]+\s+VERSION\s+([0-9.]+)', text)
    if not m: die('no project VERSION in %s/CMakeLists.txt' % folder)
    return m.group(1)

def item_version(item_id, folder):
    if item_id == 'dooblr-chords':
        return json.load(open(os.path.join(REPO, folder, 'package.json')))['version']
    if item_id == 'dooblr-prophet-panel':
        text = open(os.path.join(REPO, folder, 'm4l', 'src', 'dooblr-prophet-ui.js'), encoding='utf-8').read()
        m = re.search(r'UI_VERSION\s*=\s*"([0-9.]+)"', text)
        if not m: die('no UI_VERSION in dooblr-prophet/m4l/src/dooblr-prophet-ui.js')
        return m.group(1)
    return cmake_version(folder)

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()

def zip_folder(src, top, dest):
    """Zips src as top/..., sorted, keeping Unix modes (the installer restores +x itself too)."""
    with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for name in sorted(files):
                if name == '.DS_Store' or name.startswith('._'):
                    continue
                full = os.path.join(root, name)
                if os.path.islink(full):
                    die('symlink in bundle, not supported: ' + full)
                rel = os.path.relpath(full, src).replace(os.sep, '/')
                info = zipfile.ZipInfo(top + '/' + rel, date_time=(2026, 1, 1, 0, 0, 0))
                info.external_attr = (os.stat(full).st_mode & 0o777 | 0o100000) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, 'rb') as f:
                    z.writestr(info, f.read())

def bundle_plist_version(bundle):
    p = os.path.join(bundle, 'Contents', 'Info.plist')
    if os.path.exists(p):
        with open(p, 'rb') as f:
            return plistlib.load(f).get('CFBundleShortVersionString')
    return None

def find_bundle(name, explicit, build_root):
    for p in explicit:
        if os.path.basename(os.path.normpath(p)) == name:
            return p
    if build_root:
        hits = sorted(glob.glob(os.path.join(build_root, '**', name), recursive=True))
        hits = [h for h in hits if os.path.isdir(h) and '/_deps/' not in h.replace(os.sep, '/')]
        if hits:
            # Prefer a Release artefact.
            hits.sort(key=lambda h: (0 if 'Release' in h else 1, len(h)))
            return hits[0]
    return None

def m4l_source(item_id, tmp):
    if item_id == 'dooblr-prophet-panel':
        src = os.path.join(REPO, 'dooblr-prophet', 'm4l', 'device')
        if not os.path.exists(os.path.join(src, 'dooblr-prophet.amxd')): die('dooblr-prophet/m4l/device/dooblr-prophet.amxd missing')
        return src
    if item_id == 'dooblr-chords':
        # Build in a temp copy so the repo folder is never written to.
        copy = os.path.join(tmp, 'chords-src')
        shutil.copytree(os.path.join(REPO, 'dooblr-chords'), copy, ignore=shutil.ignore_patterns('node_modules', 'device', 'dist'))
        subprocess.run(['node', 'scripts/build.js'], cwd=copy, check=True)
        out = os.path.join(copy, 'device', 'dooblr-chords')
        if not os.path.exists(os.path.join(out, 'dooblr-chords.amxd')): die('chords build produced no amxd')
        return out
    die('unknown m4l item ' + item_id)

def cmd_package(a):
    os.makedirs(a.out, exist_ok=True)
    items = []
    with tempfile.TemporaryDirectory() as tmp:
        for item_id, name, desc, kind, bundle, folder, parent in SUITE:
            version = item_version(item_id, folder)
            if kind == 'vst3':
                src = find_bundle(bundle, a.vst3 or [], a.build_root)
                if not src:
                    if a.require_all: die('no built %s found' % bundle)
                    print('skip %s (no built bundle found)' % bundle)
                    continue
                pv = bundle_plist_version(src)
                if pv and pv != version: die('%s is %s but %s/CMakeLists.txt says %s; rebuild it' % (src, pv, folder, version))
                key = a.platform
            else:
                if not a.m4l:
                    continue
                src = m4l_source(item_id, tmp)
                key = 'any'
            zname = '%s-%s-%s.zip' % (item_id, version, a.platform if kind == 'vst3' else 'm4l')
            zpath = os.path.join(a.out, zname)
            zip_folder(src, bundle, zpath)
            entry = {'id': item_id, 'name': name, 'version': version, 'description': desc, 'kind': kind, 'bundle': bundle,
                     key: {'url': zname, 'sha256': sha256(zpath), 'size': os.path.getsize(zpath)}}
            if parent: entry.update({'parent': parent, 'optional': True})
            items.append(entry)
            print('%-44s %8d bytes  %s  (from %s)' % (zname, entry[key]['size'], entry[key]['sha256'][:12], src))
    with open(os.path.join(a.out, 'partial-%s.json' % a.platform), 'w') as f:
        json.dump({'platform': a.platform, 'items': items}, f, indent=2)

def installer_asset(path, base):
    if not path: return None
    n = os.path.basename(path)
    return {'url': base + n if base else n, 'sha256': sha256(path), 'size': os.path.getsize(path)}

def cmd_manifest(a):
    base = a.base_url.rstrip('/') + '/' if a.base_url else ''
    merged = {}
    order = [s[0] for s in SUITE]
    for p in a.partials:
        for it in json.load(open(p))['items']:
            cur = merged.setdefault(it['id'], {k: v for k, v in it.items() if k not in ('mac', 'windows', 'any')})
            if cur['version'] != it['version']: die('%s: version %s vs %s across platforms' % (it['id'], cur['version'], it['version']))
            for k in ('mac', 'windows', 'any'):
                if k in it:
                    asset = dict(it[k])
                    if base: asset['url'] = base + asset['url']
                    cur[k] = asset
    items = sorted(merged.values(), key=lambda it: order.index(it['id']) if it['id'] in order else 99)
    installer_version = cmake_version('dooblr-installer')
    man = {'schema': 1, 'suite': 'dooblr',
           'generated': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
           'installer': {'version': installer_version}, 'items': items}
    for k, p in (('mac', a.installer_mac), ('windows', a.installer_windows)):
        asset = installer_asset(p, base)
        if asset: man['installer'][k] = asset
    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, 'dooblr-manifest.json')
    with open(out, 'w') as f:
        json.dump(man, f, indent=2)
    print('wrote %s (%d items)' % (out, len(items)))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('package')
    p.add_argument('--platform', choices=['mac', 'windows'], required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--vst3', action='append', help='a built .vst3 bundle (repeatable)')
    p.add_argument('--build-root', help='search this folder for built bundles')
    p.add_argument('--m4l', action='store_true', help='also package the Max for Live devices')
    p.add_argument('--require-all', action='store_true', help='fail when a plug-in bundle is missing')
    m = sub.add_parser('manifest')
    m.add_argument('--out', required=True)
    m.add_argument('--installer-mac')
    m.add_argument('--installer-windows')
    m.add_argument('--base-url', help='absolute URL prefix for assets (default: relative to the manifest)')
    m.add_argument('partials', nargs='+')
    a = ap.parse_args()
    cmd_package(a) if a.cmd == 'package' else cmd_manifest(a)

if __name__ == '__main__':
    main()
