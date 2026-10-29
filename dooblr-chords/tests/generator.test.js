'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const T = DC.theory, C = DC.chords, E = DC.engine;

const MODE_COUNT = T.MODES.length;
const SEEDS = Array.from({ length: 150 }, (_, i) => ((i * 7919 + 13) % 99999) + 1);

function absPcs(chord, key) {
  return DC.plain(C.tones(chord)).map((t) => (key + chord.root + t) % 12);
}

test('progression length matches the Length setting (2..16)', () => {
  for (let len = 2; len <= 16; len++) {
    const b = E.build({ length: len, seed: len * 31 });
    assert.equal(b.chords.length, len);
    assert.equal(b.display.length, len);
    assert.equal(b.voicings.length, len);
  }
  assert.equal(E.build({ length: 1 }).chords.length, 2, 'clamped low');
  assert.equal(E.build({ length: 40 }).chords.length, 16, 'clamped high');
});

test('same seed and settings give identical output; different seeds differ', () => {
  const s = { root: 4, mode: 2, length: 8, color: 60, chromatic: 40, seed: 1234, humanize: 50 };
  assert.deepEqual(DC.plain(E.build(s)), DC.plain(E.build(s)));
  let differing = 0;
  for (const seed of SEEDS.slice(0, 40)) {
    const a = E.build(Object.assign({}, s, { seed })).display.map((d) => d.name).join(' ');
    const b = E.build(Object.assign({}, s, { seed: seed + 1 })).display.map((d) => d.name).join(' ');
    if (a !== b) differing++;
  }
  assert.ok(differing >= 35, `different seeds should usually differ (${differing}/40)`);
});

test('chromatic 0 is strictly diatonic in every mode and colour', () => {
  for (let mode = 0; mode < MODE_COUNT; mode++) {
    const scale = DC.plain(T.MODES[mode].intervals);
    for (const seed of SEEDS.slice(0, 60)) {
      const key = seed % 12;
      const b = E.build({ root: key, mode, length: 8, color: (seed % 5) * 25, chromatic: 0, seed });
      for (const ch of b.chords) {
        assert.equal(ch.source, 'diatonic');
        for (const pc of absPcs(ch, key)) {
          assert.ok(scale.includes((pc - key + 12) % 12), `${b.clipName}: ${b.display.map((d) => d.name).join(' ')}`);
        }
      }
    }
  }
});

test('more chromatic means more substitutions, and all four kinds appear', () => {
  const kinds = {};
  const count = (chromatic) => {
    let n = 0;
    for (const seed of SEEDS) {
      for (const mode of [0, 1]) {
        const b = E.build({ mode, length: 8, chromatic, seed });
        for (const ch of b.chords) { if (ch.source !== 'diatonic') { n++; kinds[ch.source] = true; } }
      }
    }
    return n;
  };
  const c25 = count(25), c50 = count(50), c100 = count(100);
  assert.ok(c25 > 0);
  assert.ok(c25 < c50 && c50 < c100, `${c25} < ${c50} < ${c100}`);
  for (const k of ['borrowed', 'secondary', 'tritone', 'neapolitan']) assert.ok(kinds[k], 'saw ' + k);
});

test('raising Color only adds tones and keeps the roots (chromatic 0)', () => {
  for (const seed of SEEDS.slice(0, 50)) {
    let prev = null;
    for (const color of [0, 20, 40, 60, 80, 100]) {
      const b = E.build({ mode: seed % MODE_COUNT, length: 8, color, chromatic: 0, seed });
      if (prev) {
        for (let i = 0; i < 8; i++) {
          assert.equal(b.chords[i].root, prev.chords[i].root, 'root stable');
          assert.ok(b.chords[i].level >= prev.chords[i].level, 'level monotonic');
        }
      }
      prev = b;
    }
    const triads = E.build({ length: 8, color: 0, seed });
    for (const ch of triads.chords) assert.equal(C.tones(ch).length, 3, 'colour 0 gives triads');
    const rich = E.build({ length: 8, color: 100, seed });
    for (const ch of rich.chords) assert.ok(ch.seventh !== null, 'colour 100 gives sevenths or more');
  }
});

test('progressions usually start on the tonic and end with a cadence', () => {
  let tonicStart = 0, goodEnd = 0, total = 0;
  for (let mode = 0; mode < MODE_COUNT; mode++) {
    const cad = DC.generator.MODE_RULES[T.MODES[mode].id].cadence;
    for (const seed of SEEDS) {
      const b = E.build({ mode, length: 4 + (seed % 5), seed, chromatic: 0 });
      total++;
      if (b.chords[0].degree === 0) tonicStart++;
      const last = b.chords[b.chords.length - 1];
      const beforeLast = b.chords[b.chords.length - 2];
      const ok = b.ending === 'authentic' ? (last.degree === 0 && cad[beforeLast.degree] > 0)
        : cad[last.degree] > 0;
      if (ok) goodEnd++;
    }
  }
  assert.ok(tonicStart / total > 0.75, `tonic start ${tonicStart}/${total}`);
  assert.ok(goodEnd / total > 0.75, `cadence ending ${goodEnd}/${total}`);
});

test('no diminished opening chord and no immediate repeats of a degree', () => {
  for (let mode = 0; mode < MODE_COUNT; mode++) {
    for (const seed of SEEDS) {
      const b = E.build({ mode, length: 8, seed, chromatic: 0 });
      assert.ok(!C.isDiminished(b.chords[0]), 'opening chord not diminished');
      for (let i = 1; i < 8; i++) assert.notEqual(b.chords[i].degree, b.chords[i - 1].degree);
    }
  }
});

test('lock and reroll keeps locked chords exactly', () => {
  const s = { root: 2, mode: 1, length: 8, color: 70, chromatic: 50, seed: 777 };
  const first = E.build(s);
  const locks = new Array(8).fill(null);
  locks[1] = first.chords[1]; locks[4] = first.chords[4]; locks[7] = first.chords[7];
  for (const seed of SEEDS.slice(0, 30)) {
    const again = E.build(Object.assign({}, s, { seed }), locks);
    for (const i of [1, 4, 7]) {
      assert.deepEqual(DC.plain(again.chords[i]), DC.plain(first.chords[i]));
      assert.equal(again.display[i].name, first.display[i].name);
      assert.equal(again.display[i].locked, true);
    }
    assert.equal(again.display[0].locked, false);
  }
  // Unlocked chords do change across rerolls.
  const names = new Set(SEEDS.slice(0, 30).map((seed) => E.build(Object.assign({}, s, { seed }), locks).display.map((d) => d.name).join(' ')));
  assert.ok(names.size > 10);
});

test('locked chords are stored relative to the key, so they follow a key change', () => {
  const s = { root: 0, mode: 0, length: 4, color: 30, seed: 99 };
  const a = E.build(s);
  const locks = [a.chords[0], a.chords[1], a.chords[2], a.chords[3]];
  const b = E.build(Object.assign({}, s, { root: 7 }), locks);
  for (let i = 0; i < 4; i++) assert.equal(b.chords[i].root, a.chords[i].root);
  assert.notEqual(b.display[0].name, a.display[0].name);
});

test('clip name format', () => {
  assert.equal(E.clipName({ root: 0, mode: 1, length: 4, color: 35, chromatic: 10 }), 'C min \u00B7 4 \u00B7 Col35 Chr10');
  assert.equal(E.clipName({ root: 3, mode: 0, length: 8, color: 0, chromatic: 100 }), 'Eb maj \u00B7 8 \u00B7 Col0 Chr100');
});

test('newSeed stays in range', () => {
  assert.equal(E.newSeed(0), 1);
  assert.equal(E.newSeed(0.999999999), E.SEED_MAX);
});
