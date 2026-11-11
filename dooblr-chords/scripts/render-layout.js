#!/usr/bin/env node
// Dev tool: prints the presentation layout as JSON for render-layout.py.
// It reads the source patcher and runs the built jsui (dooblr_blocks.js)
// against a recording mgraphics, so the chord display in the mockup is
// drawn by the real paint code. Usage:
//   node scripts/render-layout.js | python3 scripts/render-layout.py out.png
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');
const patcher = JSON.parse(fs.readFileSync(path.join(ROOT, 'src/patcher/dooblr-chords.maxpat'), 'utf8')).patcher;
const BLOCKS = fs.readFileSync(path.join(ROOT, 'device/dooblr-chords/dooblr_blocks.js'), 'utf8');

// Rough Arial advance widths (fraction of the point size) so the jsui's
// ellipsizing and spacing behave about as they do in Max.
function measure(s, pt) {
  let w = 0;
  for (const ch of s) {
    w += ch === ' ' ? 0.28 : /[A-Z]/.test(ch) ? 0.7 : /[il.,:'|]/.test(ch) ? 0.25 : /[0-9]/.test(ch) ? 0.56 : 0.53;
  }
  return w * pt;
}

function paintBlocks(rect, state) {
  const ops = [];
  let pt = 10;
  const ctx = {};
  vm.createContext(ctx);
  const mg = new Proxy({}, {
    get: (o, k) => {
      if (k === 'text_measure') { return (s) => [measure(String(s), pt), pt]; }
      if (k in o) { return o[k]; }
      return function () {
        const args = Array.prototype.slice.call(arguments);
        if (k === 'set_font_size') { pt = args[0]; }
        ops.push([k].concat(args));
      };
    },
    set: (o, k, v) => { o[k] = v; return true; }
  });
  ctx.mgraphics = mg;
  ctx.box = { rect: [rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3]], message: () => {} };
  ctx.post = () => {};
  ctx.outlet = () => {};
  ctx.setinletassist = ctx.setoutletassist = () => {};
  vm.runInContext(BLOCKS, ctx, { filename: 'dooblr_blocks.js' });
  ctx.setstate(JSON.stringify(state));
  ops.length = 0;
  ctx.paint();
  return ops;
}

const chord = (name, roman, extra) => Object.assign({ name, roman, func: 'T', source: 'diatonic', locked: false }, extra || {});
const STATES = [
  {
    title: 'Main view: no source, one chord locked',
    panel: null,
    inactive: ['density'],
    values: { open_advanced: 0, open_source: 0, lead: 0 },
    badge: '',
    srcinfo: 'Drop a .mid file or press USE CLIP.',
    blocks: { heading: 'PROGRESSION', status: 'Wrote A min \u00B7 Col35 Chr10 to slot 2.', level: 'ok', locked: 1,
      chords: [chord('Am7', 'i7'), chord('Fmaj9', 'VImaj9', { locked: true }), chord('C', 'III'), chord('G6', 'VII6')] }
  },
  {
    title: 'Main view: Lead on',
    panel: null,
    inactive: [],
    values: { open_advanced: 0, open_source: 0, lead: 1 },
    badge: '',
    srcinfo: 'Drop a .mid file or press USE CLIP.',
    blocks: { heading: 'LEAD', status: 'Wrote Lead \u00B7 A min \u00B7 Col35 Chr10 to slot 2.', level: 'ok', locked: 1,
      chords: [chord('Am7', 'i7'), chord('Fmaj9', 'VImaj9', { locked: true }), chord('C', 'III'), chord('G6', 'VII6')] }
  },
  {
    title: 'ADVANCED panel open, Lead on (Density available)',
    panel: 'advanced',
    inactive: [],
    values: { open_advanced: 1, open_source: 0, lead: 1, seed: 48213, density: 50 },
    badge: '',
    srcinfo: 'Drop a .mid file or press USE CLIP.',
    blocks: { heading: 'LEAD', status: 'Wrote Lead \u00B7 A min \u00B7 Col35 Chr10 to slot 2.', level: 'ok', locked: 1,
      chords: [chord('Am7', 'i7'), chord('Fmaj9', 'VImaj9', { locked: true }), chord('C', 'III'), chord('G6', 'VII6')] }
  },
  {
    title: 'SOURCE panel open, a clip loaded as VAR',
    panel: 'source',
    inactive: ['reroll', 'density'],
    values: { open_advanced: 0, open_source: 1, kind: 0, lead: 0 },
    badge: 'VAR \u00B7 my riff',
    dropped: 'my riff.mid',
    srcinfo: 'my riff \u00B7 A Natural Minor \u00B7 4 chords, 4 bars',
    blocks: { heading: 'VARIATION', status: 'Loaded my riff: A Natural Minor, 4 chords.', level: 'ok', locked: 0,
      source: { name: 'my riff', detectedKey: 'A Natural Minor', chords: ['Am', 'F', 'C', 'G'] },
      chords: [chord('Am', 'i'), chord('F', 'VI'), chord('Dm7', 'iv7', { changed: true }), chord('G', 'VII')] }
  }
];

function parsePanels() {
  const src = fs.readFileSync(path.join(ROOT, 'src/max/main.js'), 'utf8');
  const block = /var PANELS = \{([\s\S]*?)\};/.exec(src);
  const panels = {};
  if (!block) { return panels; }
  const re = /([a-z]+):\s*\[([\s\S]*?)\]/g;
  let m;
  while ((m = re.exec(block[1]))) {
    const names = m[2].match(/'([^']+)'/g);
    panels[m[1]] = names ? names.map((x) => x.slice(1, -1)) : [];
  }
  return panels;
}

const memberOf = {};
const panels = parsePanels();
Object.keys(panels).forEach((name) => (panels[name] || []).forEach((vn) => { memberOf[vn] = name; }));

function parseRectMap(src, name) {
  const map = {};
  const block = new RegExp('var ' + name + ' = \\{([\\s\\S]*?)\\};').exec(src);
  if (!block) { return map; }
  const re = /([A-Za-z0-9_]+):\s*\[([^\]]+)\]/g;
  let m;
  while ((m = re.exec(block[1]))) {
    map[m[1]] = m[2].split(',').map((n) => Number(n.trim()));
  }
  return map;
}
function parseNamedRect(src, name) {
  const m = new RegExp('var ' + name + ' = \\[([^\\]]+)\\];').exec(src);
  return m ? m[1].split(',').map((n) => Number(n.trim())) : null;
}
const MAIN_JS = fs.readFileSync(path.join(ROOT, 'src/max/main.js'), 'utf8');
const openRects = Object.assign({}, parseRectMap(MAIN_JS, 'COVERED_RECTS'), parseRectMap(MAIN_JS, 'PANEL_RECTS'));
const badgeRect = parseNamedRect(MAIN_JS, 'BADGE_RECT');
if (badgeRect) { openRects.srcbadge = badgeRect; }

const boxes = patcher.boxes.map((w) => w.box).filter((b) => b.presentation).map((b) => {
  const v = b.saved_attribute_attributes && b.saved_attribute_attributes.valueof;
  const rect = (b.varname && openRects[b.varname]) || b.presentation_rect;
  return Object.assign({}, b, { param: v || null, panel: (b.varname && memberOf[b.varname]) || null, presentation_rect: rect });
});
const jsui = boxes.find((b) => b.maxclass === 'jsui');

const out = {
  width: patcher.devicewidth,
  height: 169,
  boxes,
  states: STATES.map((s) => Object.assign({}, s, { jsui: { rect: jsui.presentation_rect, ops: paintBlocks(jsui.presentation_rect, s.blocks) } }))
};
process.stdout.write(JSON.stringify(out));
