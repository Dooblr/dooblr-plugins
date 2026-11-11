'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { n } = require('./smf');

const DT = loadEngine();
const V = DT.velocity;

test('compression 100 maps the clip into the target range; 0 leaves velocities', () => {
  const notes = [n(60, 0, 0.5, 20), n(62, 0.5, 0.5, 40), n(64, 1, 0.5, 100)];
  const hard = V.apply(notes, { velmin: 60, velmax: 100, velcomp: 100, humanize: 0, seed: 1 });
  const vels = hard.map((x) => x.velocity).sort((a, b) => a - b);
  assert.strictEqual(vels[0], 60);
  assert.strictEqual(vels[vels.length - 1], 100);
  const none = V.apply(notes, { velmin: 60, velmax: 100, velcomp: 0, humanize: 0, seed: 1 });
  assert.deepStrictEqual(JSON.parse(JSON.stringify(none.map((x) => x.velocity))), [20, 40, 100]);
});

test('humanize is seeded, bounded, and off at 0', () => {
  const notes = [n(60, 0, 0.5, 80), n(62, 0.5, 0.5, 80), n(64, 1, 0.5, 80)];
  const a = V.apply(notes, { velmin: 1, velmax: 127, velcomp: 0, humanize: 80, seed: 4 });
  const b = V.apply(notes, { velmin: 1, velmax: 127, velcomp: 0, humanize: 80, seed: 4 });
  const c = V.apply(notes, { velmin: 1, velmax: 127, velcomp: 0, humanize: 80, seed: 5 });
  assert.deepStrictEqual(a.map((x) => x.velocity), b.map((x) => x.velocity));
  assert.ok(a.some((x, i) => x.velocity !== notes[i].velocity), 'humanize should move at least one velocity');
  assert.ok(a.some((x, i) => x.velocity !== c[i].velocity), 'different seeds differ');
  a.forEach((x) => {
    assert.ok(x.velocity >= 1 && x.velocity <= 127);
    assert.ok(Math.abs(x.velocity - 80) <= 16, 'humanize 80 stays within ±16, got ' + x.velocity);
  });
  const off = V.apply(notes, { velmin: 1, velmax: 127, velcomp: 0, humanize: 0, seed: 4 });
  assert.deepStrictEqual(JSON.parse(JSON.stringify(off.map((x) => x.velocity))), [80, 80, 80]);
});
