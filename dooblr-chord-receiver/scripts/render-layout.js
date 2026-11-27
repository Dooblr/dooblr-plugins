#!/usr/bin/env node
// Prints the presentation layout as JSON for render-layout.py.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');
const patcher = JSON.parse(fs.readFileSync(path.join(ROOT, 'src/patcher/dooblr-chord-receiver.maxpat'), 'utf8')).patcher;
const UI = fs.readFileSync(path.join(ROOT, 'device/dooblr-chord-receiver/dooblr_chord_receiver_ui.js'), 'utf8');

function measure(s, pt) {
  let w = 0;
  for (const ch of s) {
    w += ch === ' ' ? 0.28 : /[A-Z]/.test(ch) ? 0.7 : /[il.,:'|]/.test(ch) ? 0.25 : /[0-9]/.test(ch) ? 0.56 : 0.53;
  }
  return w * pt;
}

function paintStatus(rect, state) {
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
  vm.runInContext(UI, ctx, { filename: 'dooblr_chord_receiver_ui.js' });
  if (typeof ctx.setstate === 'function') { ctx.setstate(JSON.stringify(state)); }
  ops.length = 0;
  ctx.paint();
  return ops;
}

function boxesOf(p) {
  return p.boxes.map((w) => {
    const b = w.box;
    const v = (b.saved_attribute_attributes && b.saved_attribute_attributes.valueof) || null;
    return Object.assign({}, b, { param: v || null });
  }).filter((b) => b.presentation && b.presentation_rect);
}

const statusBox = boxesOf(patcher).find((b) => b.varname === 'status');
const states = [
  {
    title: 'Connected — Am9 on channel A, TONES / NEAREST',
    values: { channel: 0, role: 0, snapdir: 0, retrigger: 1, refroot: 0, range: 0, register: 48 },
    jsui: statusBox ? [{ varname: 'status', rect: statusBox.presentation_rect, ops: paintStatus(statusBox.presentation_rect, { connected: true, name: 'Am9', channel: 0, role: 0 }) }] : []
  },
  {
    title: 'No sender',
    values: { channel: 0, role: 0, snapdir: 0, retrigger: 1, refroot: 0, range: 0, register: 48 },
    jsui: statusBox ? [{ varname: 'status', rect: statusBox.presentation_rect, ops: paintStatus(statusBox.presentation_rect, { connected: false, name: 'NO SENDER', channel: 0, role: 0 }) }] : []
  }
];

const boxes = boxesOf(patcher);
const out = {
  width: patcher.devicewidth,
  height: 149,
  boxes,
  states: states.map((s) => {
    const js = (s.jsui && s.jsui[0]) ? s.jsui[0] : (s.jsui || { rect: [0,0,0,0], ops: [] });
    return Object.assign({}, s, { jsui: { rect: js.rect, ops: js.ops || [] } });
  })
};
process.stdout.write(JSON.stringify(out));
