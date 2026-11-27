#!/usr/bin/env node
// Dev tool: prints the presentation layout as JSON for render-layout.py.
//   node scripts/render-layout.js            main view
//   node scripts/render-layout.js --advanced  Advanced window
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');
const root = JSON.parse(fs.readFileSync(path.join(ROOT, 'src/patcher/dooblr-chords.maxpat'), 'utf8')).patcher;
const ADVANCED = process.argv.indexOf('--advanced') >= 0;
const BLOCKS = fs.readFileSync(path.join(ROOT, 'device/dooblr-chords/dooblr_blocks.js'), 'utf8');

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

function boxesOf(p) {
  return p.boxes.map((w) => {
    const b = w.box;
    const v = (b.saved_attribute_attributes && b.saved_attribute_attributes.valueof) || null;
    return Object.assign({}, b, { param: v || null, panel: null, covered: false });
  }).filter((b) => b.presentation && b.presentation_rect);
}

const chord = (name, roman, extra) => Object.assign({ name, roman, func: 'T', source: 'diatonic', locked: false }, extra || {});
const advBox = root.boxes.find((w) => w.box.varname === 'advanced');
const adv = advBox && advBox.box.patcher;

const MAIN_STATES = [
  {
    title: 'Main view: CLIP, two hero knobs, one chord locked',
    inactive: ['density', 'commit'],
    values: { lead: 0, notes: 0, playmode: 0, spice: 35, length: 4, root: 9, mode: 1 },
    badge: '',
    blocks: { heading: 'PROGRESSION', status: 'Wrote A min \u00B7 TRI \u00B7 Col35 Chr10 to slot 2.', level: 'ok', locked: 1,
      chords: [chord('Am', 'i'), chord('F', 'VI', { locked: true }), chord('C', 'III'), chord('G', 'VII')] }
  },
  {
    title: 'Main view: LIVE mode, playhead on the second chord, TO CLIP available',
    inactive: ['write', 'density'],
    values: { lead: 0, notes: 0, playmode: 1, spice: 35, length: 4, root: 9, mode: 1 },
    badge: '',
    blocks: { heading: 'PROGRESSION', status: 'LIVE: playing.', level: 'ok', locked: 0,
      playIndex: 1, playmode: 1, lead: 0,
      chords: [chord('Am', 'i'), chord('F', 'VI'), chord('C', 'III'), chord('G', 'VII')] }
  },
  {
    title: 'Main view: Lead on, source badge visible',
    inactive: ['notes', 'commit'],
    values: { lead: 1, notes: 2, playmode: 0, spice: 35, length: 4, root: 9, mode: 1 },
    badge: 'VAR \u00B7 my riff',
    blocks: { heading: 'LEAD', status: 'Wrote Lead \u00B7 A min \u00B7 9th \u00B7 Col35 Chr10 to slot 2.', level: 'ok', locked: 1,
      chords: [chord('Am9', 'i9'), chord('Fmaj9', 'VImaj9', { locked: true }), chord('Cmaj9', 'IIImaj9'), chord('G9', 'VII9')] }
  }
];

const ADV_STATES = [
  {
    title: 'Advanced window: Color, Chromatic, voicing, Evolve, Channel and Source',
    inactive: ['density'],
    values: { lead: 0, notes: 0, seed: 48213, density: 50, evolve: 0, buschan: 0, kind: 0, color: 35, chromatic: 10 },
    dropped: '',
    srcinfo: 'Drop a .mid file or press USE CLIP.',
    blocks: null
  },
  {
    title: 'Advanced window: source loaded as VAR, Lead density available',
    inactive: ['notes'],
    values: { lead: 1, notes: 2, seed: 48213, density: 70, evolve: 1, buschan: 0, kind: 0, color: 35, chromatic: 10 },
    dropped: 'my riff.mid',
    srcinfo: 'my riff \u00B7 A Natural Minor \u00B7 4 chords, 4 bars',
    blocks: null
  }
];

if (ADVANCED) {
  const boxes = boxesOf(adv);
  const out = {
    width: 560,
    height: 300,
    boxes,
    states: ADV_STATES.map((s) => Object.assign({}, s, { jsui: { rect: [0, 0, 0, 0], ops: [] } }))
  };
  process.stdout.write(JSON.stringify(out));
} else {
  const boxes = boxesOf(root);
  const jsui = boxes.find((b) => b.maxclass === 'jsui');
  const out = {
    width: root.devicewidth,
    height: 169,
    boxes,
    states: MAIN_STATES.map((s) => Object.assign({}, s, {
      jsui: { rect: jsui.presentation_rect, ops: paintBlocks(jsui.presentation_rect, s.blocks) }
    }))
  };
  process.stdout.write(JSON.stringify(out));
}
