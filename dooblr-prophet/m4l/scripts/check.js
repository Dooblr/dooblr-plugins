// Headless checks (no Max needed): patcher JSON is valid and consistent, the jsui script parses
// and stays within the ES5 that Max 8's js engine accepts, and a built device round-trips.
//   node scripts/check.js [path/to/built.amxd]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const amxd = require('./amxd');

const root = path.join(__dirname, '..');
let failures = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) ++failures; };

function checkPatcher(p, where) {
  const pt = p.patcher;
  check(pt && Array.isArray(pt.boxes) && Array.isArray(pt.lines), where + ': has boxes and lines');
  const ids = new Map(pt.boxes.map((b) => [b.box.id, b.box]));
  check(ids.size === pt.boxes.length, where + ': box ids are unique');
  let linesOk = true;
  for (const l of pt.lines) {
    const s = ids.get(l.patchline.source[0]), d = ids.get(l.patchline.destination[0]);
    if (!s || !d || l.patchline.source[1] >= s.numoutlets || l.patchline.destination[1] >= d.numinlets) linesOk = false;
  }
  check(linesOk, where + ': every patch cord joins existing outlets and inlets');
  const ui = pt.boxes.map((b) => b.box).find((b) => b.maxclass === 'jsui');
  check(!!ui && fs.existsSync(path.join(root, 'src', ui.filename)), where + ': jsui script exists beside the patcher');
  check(!!ui && ui.presentation_rect[2] === pt.devicewidth && ui.presentation_rect[3] === 169, where + ': jsui fills the device (' + pt.devicewidth + ' x 169)');
  check(pt.openinpresentation === 1, where + ': opens in presentation');

  // live.thisdevice reaches the jsui only as "liveready" (the jsui makes no Live API calls before it).
  const into = (id) => pt.lines.filter((l) => l.patchline.destination[0] === id).map((l) => ids.get(l.patchline.source[0]));
  const thisDevice = pt.boxes.map((b) => b.box).find((b) => b.text === 'live.thisdevice');
  const feeds = ui ? into(ui.id) : [];
  check(!!thisDevice && !feeds.includes(thisDevice), where + ': live.thisdevice is not wired straight to the jsui');
  const ready = feeds.find((b) => b.maxclass === 'message' && b.text === 'liveready');
  check(!!ready && into(ready.id).includes(thisDevice), where + ': live.thisdevice -> [liveready( -> jsui');

  // No patch path from the jsui's outlets back to its inlet, except through udpreceive, which only
  // outputs what arrives from the network (asynchronously).
  let loop = false;
  if (ui) {
    const seen = new Set(), queue = [ui.id];
    while (queue.length) {
      const id = queue.shift();
      for (const l of pt.lines.filter((x) => x.patchline.source[0] === id)) {
        const d = l.patchline.destination[0];
        if (d === ui.id) { loop = true; continue; }
        const box = ids.get(d);
        if (seen.has(d) || /^udpreceive\b/.test(box.text || '')) continue;
        seen.add(d); queue.push(d);
      }
    }
  }
  check(!loop, where + ': no synchronous feedback loop from the jsui back into itself');
}

const patchText = fs.readFileSync(path.join(root, 'src', 'dooblr-prophet.maxpat'), 'utf8');
let patcher = null;
try { patcher = JSON.parse(patchText); check(true, 'maxpat is valid JSON'); } catch (e) { check(false, 'maxpat is valid JSON: ' + e.message); }
if (patcher) checkPatcher(patcher, 'maxpat');

const jsPath = path.join(root, 'src', 'dooblr-prophet-ui.js');
const js = fs.readFileSync(jsPath, 'utf8');
try { new vm.Script(js, { filename: jsPath }); check(true, 'jsui script parses'); } catch (e) { check(false, 'jsui script parses: ' + e.message); }

// Strip comments, strings and regex-free code paths crudely, then look for post-ES5 syntax.
const code = js.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '').replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, '""');
const banned = [[/\blet\s/, 'let'], [/\bconst\s/, 'const'], [/=>/, 'arrow function'], [/\bclass\s/, 'class'], [/`/, 'template literal'],
  [/\.\.\.[A-Za-z_]/, 'spread'], [/\bfor\s*\([^)]*\bof\b/, 'for...of'], [/\basync\b|\bawait\b/, 'async']];
for (const [re, name] of banned) check(!re.test(code), 'jsui script has no ' + name + ' (ES5 only)');

const built = process.argv[2];
if (built) {
  try {
    const dev = amxd.read(fs.readFileSync(built));
    check(dev.type === 'midi', 'device is a MIDI effect');
    check(JSON.stringify(dev.patcher) === JSON.stringify(patcher), 'device patcher round-trips to the source');
    checkPatcher(dev.patcher, 'device');
    const beside = path.join(path.dirname(built), 'dooblr-prophet-ui.js');
    check(fs.existsSync(beside) && fs.readFileSync(beside, 'utf8') === js, 'jsui script is copied beside the device');
  } catch (e) { check(false, 'device reads back: ' + e.message); }
}

console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
process.exit(failures ? 1 : 0);
