'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { n } = require('./smf');

const DT = loadEngine();
const E = DT.engine;
const VR = DT.vary;
const N = DT.notes;

function riff() {
  return [
    n(60, 0.00, 0.45, 100),
    n(62, 0.50, 0.45, 80),
    n(64, 1.00, 0.45, 90),
    n(65, 1.50, 0.45, 70),
    n(67, 2.00, 0.90, 110),
    n(64, 3.00, 0.45, 85),
    n(62, 3.50, 0.40, 75)
  ];
}

function src() {
  return E.loadNotes(riff(), { name: 'riff', lengthBeats: 4 });
}

const FULL = { amount: 80, rhythm: 80, melody: 80, ghosts: 80, accents: 80, seed: 7, root: 0, mode: 0, grid: 3 };

test('same seed and settings give identical variations; different seeds differ', () => {
  const s = src();
  const a = E.variations(s, FULL);
  const b = E.variations(s, FULL);
  assert.strictEqual(a.length, 4);
  a.forEach((v, i) => {
    assert.strictEqual(N.fingerprint(v.notes), N.fingerprint(b[i].notes), 'slot ' + i);
  });
  const c = E.variations(s, Object.assign({}, FULL, { seed: 8 }));
  const differed = a.some((v, i) => N.fingerprint(v.notes) !== N.fingerprint(c[i].notes));
  assert.ok(differed, 'changing the seed must change at least one variation');
});

test('amount 0 leaves the source notes unchanged in every slot', () => {
  const s = src();
  const fp = N.fingerprint(s.notes);
  const vars = E.variations(s, Object.assign({}, FULL, { amount: 0 }));
  vars.forEach((v) => {
    assert.strictEqual(N.fingerprint(v.notes), fp, v.label + ' changed the clip at amount 0');
  });
});

test('ApplySlot is deterministic across independent engine loads', () => {
  const DT2 = require('./load-engine').loadEngine();
  const notes = riff();
  const A = DT.vary.applySlot(notes, FULL, 1);
  const B = DT2.vary.applySlot(notes, FULL, 1);
  assert.strictEqual(DT.notes.fingerprint(A.notes), DT2.notes.fingerprint(B.notes));
});

test('melody amount 0 never changes existing pitches; melody 100 does in Substitute', () => {
  const s = src();
  const origP = s.notes.map((x) => x.pitch).join(',');
  const none = E.variations(s, Object.assign({}, FULL, { melody: 0, ghosts: 0, rhythm: 0, accents: 0, amount: 100 }));
  none.forEach((v) => {
    const existing = v.notes.slice(0, s.notes.length);
    assert.strictEqual(existing.map((x) => x.pitch).join(','), origP, v.label);
  });
  const sub = VR.applySlot(s.notes, Object.assign({}, FULL, { amount: 100, melody: 100, rhythm: 0, ghosts: 0, accents: 0 }), 1);
  const changed = sub.notes.some((x, i) => s.notes[i] && x.pitch !== s.notes[i].pitch);
  assert.ok(changed, 'Substitute with melody 100 should change at least one pitch');
  sub.notes.forEach((x) => assert.ok(DT.theory.inScale(x.pitch, 0, 0), 'out of scale ' + x.pitch));
});

test('rhythm amount 100 Displace moves at least one start; rhythm 0 does not', () => {
  const s = src();
  const origT = s.notes.map((x) => x.start).join(',');
  const still = VR.applySlot(s.notes, Object.assign({}, FULL, { amount: 100, rhythm: 0, melody: 0, ghosts: 0, accents: 0 }), 0);
  assert.strictEqual(still.notes.map((x) => x.start).join(','), origT);
  const moved = VR.applySlot(s.notes, Object.assign({}, FULL, { amount: 100, rhythm: 100, melody: 0, ghosts: 0, accents: 0 }), 0);
  assert.ok(moved.notes.some((x, i) => s.notes[i] && Math.abs(x.start - s.notes[i].start) > 1e-6), 'Displace should shift a start');
});

test('Fill adds ghost notes at the end of a phrase', () => {
  const notes = [n(60, 0, 0.4, 100), n(62, 0.5, 0.4, 90), n(64, 1.0, 0.4, 90)]; // gap after 1.4 until bar 4
  const out = VR.applySlot(notes, { amount: 100, rhythm: 0, melody: 0, ghosts: 100, accents: 0, seed: 3, root: 0, mode: 0, grid: 3, lengthBeats: 4 }, 2);
  assert.ok(out.notes.length > notes.length, 'ghosts should add notes, got ' + out.notes.length);
  const added = out.notes.filter((x) => x.start >= 1.4 - 1e-6);
  assert.ok(added.length >= 1, 'ghosts belong at the phrase end');
  added.forEach((x) => assert.ok(x.velocity < 50, 'ghosts are quiet, vel=' + x.velocity));
});

test('Invert reflects pitches around the mean and snaps to the scale', () => {
  const notes = [n(60, 0, 0.5, 100), n(64, 0.5, 0.5, 100), n(67, 1, 0.5, 100)];
  const out = VR.applySlot(notes, { amount: 100, rhythm: 0, melody: 100, ghosts: 0, accents: 0, seed: 1, root: 0, mode: 0, grid: 3, lengthBeats: 4 }, 3);
  out.notes.forEach((x) => assert.ok(DT.theory.inScale(x.pitch, 0, 0)));
  assert.notDeepStrictEqual(out.notes.map((x) => x.pitch), notes.map((x) => x.pitch));
});

test('variation labels are the four characters and clip names include VAR', () => {
  const vars = E.variations(src(), FULL);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(vars.map((v) => v.label))), ['Displace', 'Substitute', 'Fill', 'Invert']);
  assert.match(vars[1].clipName, /VAR Substitute/);
});
