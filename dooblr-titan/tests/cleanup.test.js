'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { n } = require('./smf');

const DT = loadEngine();
const C = DT.cleanup;
const E = DT.engine;

test('fixFlams keeps the louder of two same-pitch hits 20 ms apart', () => {
  const notes = [n(36, 0, 0.1, 40), n(36, 0.02, 0.1, 110), n(38, 0, 0.2, 90)];
  const out = C.fixFlams(notes);
  assert.strictEqual(out.length, 2);
  const kick = out.filter((x) => x.pitch === 36);
  assert.strictEqual(kick.length, 1);
  assert.strictEqual(kick[0].velocity, 110);
  assert.strictEqual(kick[0].start, 0.02);
});

test('trimOverlaps shortens the first of two overlapping same-pitch notes', () => {
  const notes = [n(60, 0, 1.2, 100), n(60, 1, 0.5, 90), n(64, 0, 2, 80)];
  const out = C.trimOverlaps(notes);
  const c4 = out.filter((x) => x.pitch === 60);
  assert.ok(c4[0].start + c4[0].duration <= 1 + 1e-4);
  assert.ok(c4[0].duration > 0);
  const e4 = out.filter((x) => x.pitch === 64);
  assert.strictEqual(e4[0].duration, 2, 'other pitches are not trimmed');
});

test('removeTiny drops notes below the length or velocity floor', () => {
  const notes = [n(60, 0, 0.01, 100), n(62, 0.5, 0.5, 4), n(64, 1, 0.5, 90)];
  const out = C.removeTiny(notes, 0.05, 8);
  assert.strictEqual(out.length, 1);
  assert.strictEqual(out[0].pitch, 64);
});

test('closeGaps lengthens a note across a small same-pitch hole, not a large one', () => {
  const notes = [n(60, 0, 0.4, 100), n(60, 0.5, 0.4, 100), n(60, 2.0, 0.4, 100)];
  const out = C.closeGaps(notes, 0.15);
  assert.ok(Math.abs(out[0].duration - 0.499) < 0.01, 'small gap closed, dur=' + out[0].duration);
  assert.ok(Math.abs(out[1].duration - 0.4) < 1e-6, 'large gap left alone');
});

test('legato off leaves overlaps and gaps; legato on tidies both', () => {
  const notes = [n(60, 0, 1.2, 100), n(60, 1, 0.4, 90)];
  const off = C.apply(notes, { minlen: 0, minvel: 1, flams: 0, legato: 0, gapclose: 100 });
  assert.ok(off[0].duration > 1.1);
  const on = C.apply(notes, { minlen: 0, minvel: 1, flams: 0, legato: 1, gapclose: 0 });
  assert.ok(on[0].start + on[0].duration <= 1 + 1e-3);
});

test('engine.tighten with high minlen/minvel removes the accident', () => {
  const src = E.loadNotes([
    n(60, 0, 1, 100),
    n(61, 0.01, 0.02, 5),
    n(64, 1, 1, 90)
  ], { name: 'clip', lengthBeats: 4 });
  const out = E.tighten(src, Object.assign({}, E.DEFAULTS, { qstrength: 0, humanize: 0, velcomp: 0, minlen: 40, minvel: 20, scalesnap: 0 }));
  assert.strictEqual(out.notes.length, 2);
  assert.strictEqual(out.removed, 1);
  assert.ok(out.notes.every((x) => x.pitch !== 61));
});
