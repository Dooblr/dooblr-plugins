'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const S = DC.spice;

test('Spice curve goes through (0,0,0), the factory default (35,35,10), and (100,100,100)', () => {
  assert.strictEqual(S.colorFromSpice(0), 0);
  assert.strictEqual(S.chromaticFromSpice(0), 0);
  assert.strictEqual(S.colorFromSpice(35), 35);
  assert.strictEqual(S.chromaticFromSpice(35), 10);
  assert.strictEqual(S.colorFromSpice(100), 100);
  assert.strictEqual(S.chromaticFromSpice(100), 100);
  assert.strictEqual(S.spiceFromParts(35, 10), 35);
  assert.strictEqual(S.spiceFromParts(0, 0), 0);
  assert.strictEqual(S.spiceFromParts(100, 100), 100);
});

test('Spice is monotonic in both Color and Chromatic', () => {
  let prevC = -1, prevK = -1;
  for (let s = 0; s <= 100; s++) {
    const c = S.colorFromSpice(s);
    const k = S.chromaticFromSpice(s);
    assert.ok(c >= prevC, 'color at spice ' + s);
    assert.ok(k >= prevK, 'chromatic at spice ' + s);
    prevC = c;
    prevK = k;
    // On-curve pairs invert back to the same spice (allow 1 for rounding).
    assert.ok(Math.abs(S.spiceFromParts(c, k) - s) <= 1, 'invert spice ' + s + ' got ' + S.spiceFromParts(c, k));
  }
});

test('off-curve Color/Chromatic do not snap to the curve; spice is a blend', () => {
  const s = S.spiceFromParts(80, 10);
  assert.notStrictEqual(s, 80, 'not treated as on-curve');
  assert.ok(s > 35 && s < 80);
  // The inverse must not claim the pair is on the curve via color.
  assert.ok(Math.abs(S.chromaticFromSpice(80) - 10) > 2);
});

test('engine.normalize keeps spice in 0-100 and leaves Color/Chromatic independent', () => {
  const E = DC.engine;
  const o = E.normalize({ spice: 200, color: 12, chromatic: 3 });
  assert.strictEqual(o.spice, 100);
  assert.strictEqual(o.color, 12);
  assert.strictEqual(o.chromatic, 3);
  const d = E.normalize({});
  assert.strictEqual(d.spice, 35);
  assert.strictEqual(d.color, 35);
  assert.strictEqual(d.chromatic, 10);
});
