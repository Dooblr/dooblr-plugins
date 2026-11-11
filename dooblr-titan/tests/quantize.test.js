'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { n } = require('./smf');

const DT = loadEngine();
const Q = DT.quantize;
const E = DT.engine;

test('grid table includes straight values and triplet divisions', () => {
  const ids = [];
  for (let i = 0; i < Q.GRIDS.length; i++) { ids.push(Q.GRIDS[i].id); }
  assert.deepStrictEqual(ids, ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T']);
  assert.strictEqual(Q.gridBeats(3), 0.25);
  assert.ok(Math.abs(Q.gridBeats(2) - 1 / 3) < 1e-12);
  assert.ok(Math.abs(Q.gridBeats(4) - 1 / 6) < 1e-12);
});

test('strength 100 snaps a late 16th onto the grid; strength 0 leaves it', () => {
  const notes = [n(60, 0.07, 0.2, 100), n(64, 0.31, 0.2, 90)];
  const hard = Q.apply(notes, { strength: 100, grid: 3, swing: 0, groove: 0 });
  assert.strictEqual(hard[0].start, 0);
  assert.strictEqual(hard[1].start, 0.25);
  const none = Q.apply(notes, { strength: 0, grid: 3, swing: 0, groove: 0 });
  assert.strictEqual(none[0].start, 0.07);
  assert.strictEqual(none[1].start, 0.31);
});

test('strength 50 blends halfway to the grid', () => {
  const notes = [n(60, 0.10, 0.2, 100)];
  const mid = Q.apply(notes, { strength: 50, grid: 3, swing: 0, groove: 0 });
  assert.ok(Math.abs(mid[0].start - 0.05) < 1e-4);
});

test('swing 100 delays the offbeat 8th by a third of the grid (triplet feel)', () => {
  const g = Q.gridBeats(1); // 1/8 = 0.5
  const notes = [n(60, 0, 0.4, 100), n(62, 0.5, 0.4, 100)];
  const swung = Q.apply(notes, { strength: 100, grid: 1, swing: 100, groove: 0 });
  assert.strictEqual(swung[0].start, 0);
  assert.ok(Math.abs(swung[1].start - (0.5 + g / 3)) < 1e-4);
  const straight = Q.apply(notes, { strength: 100, grid: 1, swing: 0, groove: 0 });
  assert.strictEqual(straight[1].start, 0.5);
});

test('triplet grid 1/8T snaps onto thirds of a beat', () => {
  const notes = [n(60, 0.40, 0.2, 100)];
  const out = Q.apply(notes, { strength: 100, grid: 2, swing: 0, groove: 0 });
  assert.ok(Math.abs(out[0].start - 1 / 3) < 1e-4);
});

test('groove-preserving quantize keeps a consistent late feel and pulls outliers in', () => {
  // Three notes ~30 ms late of 16ths, plus one wild 90 ms late note.
  const groove = 0.03;
  const notes = [
    n(60, 0.00 + groove, 0.2, 100),
    n(62, 0.25 + groove, 0.2, 100),
    n(64, 0.50 + groove, 0.2, 100),
    n(65, 0.75 + 0.09, 0.2, 100)
  ];
  const out = Q.apply(notes, { strength: 100, grid: 3, swing: 0, groove: 1 });
  out.forEach((x, i) => {
    if (i < 3) { assert.ok(Math.abs(x.start - (i * 0.25 + groove)) < 1e-3, 'kept groove on ' + i + ': ' + x.start); }
  });
  assert.ok(Math.abs(out[3].start - (0.75 + groove)) < 1e-3, 'outlier pulled to the groove, got ' + out[3].start);
  const flat = Q.apply(notes, { strength: 100, grid: 3, swing: 0, groove: 0 });
  assert.strictEqual(flat[0].start, 0);
  assert.strictEqual(flat[3].start, 0.75);
});

test('engine.tighten with default qstrength 100 quantizes and does not mutate the source', () => {
  const src = E.loadNotes([n(60, 0.07, 0.5, 100), n(64, 1.07, 0.5, 90)], { name: 'riff', lengthBeats: 4 });
  const copy = JSON.parse(JSON.stringify(src.notes.map((x) => x.start)));
  const out = E.tighten(src, Object.assign({}, E.DEFAULTS, { groove: 0, humanize: 0 }));
  const after = JSON.parse(JSON.stringify(out.notes.map((x) => x.start)));
  assert.notDeepStrictEqual(after, copy);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(src.notes.map((x) => x.start))), copy);
  assert.match(out.clipName, /TIGHT/);
  assert.ok(out.liveNotes[0].start_time !== undefined);
});
