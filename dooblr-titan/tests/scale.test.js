'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { n } = require('./smf');

const DT = loadEngine();
const T = DT.theory;
const K = DT.keydetect;
const S = DT.scalesnap;
const E = DT.engine;

test('C major scale melody detects C Major', () => {
  const notes = [];
  [0, 2, 4, 5, 7, 9, 11, 12].forEach((d, i) => notes.push(n(60 + d, i, 0.9, 90)));
  notes.push(n(60, 8, 2, 120));
  const key = K.detect(notes);
  assert.strictEqual(key.root, 0, 'root ' + key.root + ' ' + key.name);
  assert.strictEqual(T.MODES[key.mode].family, 'major', key.name);
  assert.ok(key.score > 0.5, 'score ' + key.score);
});

test('A natural minor melody detects A Natural Minor (or minor family)', () => {
  const notes = [];
  [0, 2, 3, 5, 7, 8, 10, 12].forEach((d, i) => notes.push(n(57 + d, i, 0.9, 90)));
  notes.push(n(57, 8, 2, 120));
  const key = K.detect(notes);
  assert.strictEqual(key.root, 9, 'root ' + key.root + ' ' + key.name);
  assert.strictEqual(T.MODES[key.mode].family, 'minor', key.name);
});

test('scale snap moves F# to F in C major (tie prefers down) and leaves C', () => {
  const notes = [n(60, 0, 0.5, 100), n(66, 0.5, 0.5, 100), n(64, 1, 0.5, 100)];
  const out = S.apply(notes, 0, 0);
  assert.strictEqual(out[0].pitch, 60);
  assert.strictEqual(out[1].pitch, 65);
  assert.strictEqual(out[2].pitch, 64);
});

test('scale snap is a no-op when every note is already in the scale', () => {
  const notes = [n(60, 0, 0.5, 90), n(64, 0.5, 0.5, 90), n(67, 1, 0.5, 90)];
  const out = S.apply(notes, 0, 0);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(out.map((x) => x.pitch))), [60, 64, 67]);
});

test('nearestInScale stays inside 0..127 and lands on the scale', () => {
  for (let p = 0; p <= 127; p++) {
    const s = T.nearestInScale(p, 0, 0);
    assert.ok(s >= 0 && s <= 127);
    assert.ok(T.inScale(s, 0, 0), 'pitch ' + p + ' snapped to ' + s);
    if (T.inScale(p, 0, 0)) { assert.strictEqual(s, p); }
  }
});

test('engine.tighten with scalesnap on rewrites out-of-scale notes', () => {
  const src = E.loadNotes([n(60, 0, 1, 100), n(66, 1, 1, 100)], { name: 'x', lengthBeats: 4 });
  const off = E.tighten(src, Object.assign({}, E.DEFAULTS, { qstrength: 0, humanize: 0, velcomp: 0, scalesnap: 0, minlen: 0, minvel: 1 }));
  assert.strictEqual(off.notes[1].pitch, 66);
  const on = E.tighten(src, Object.assign({}, E.DEFAULTS, { qstrength: 0, humanize: 0, velcomp: 0, scalesnap: 1, root: 0, mode: 0, minlen: 0, minvel: 1 }));
  assert.ok(T.inScale(on.notes[1].pitch, 0, 0));
  assert.notStrictEqual(on.notes[1].pitch, 66);
});

test('detectKey throws on an empty clip', () => {
  assert.throws(() => K.detect([]), /no notes/);
});
