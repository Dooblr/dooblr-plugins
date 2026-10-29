'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const T = DC.theory, C = DC.chords;

const mi = (id) => T.modeIndexById(id);

test('modes have the expected interval sets', () => {
  const want = {
    major: [0, 2, 4, 5, 7, 9, 11], minor: [0, 2, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10],
    phrygian: [0, 1, 3, 5, 7, 8, 10], lydian: [0, 2, 4, 6, 7, 9, 11], mixolydian: [0, 2, 4, 5, 7, 9, 10],
    harmonicMinor: [0, 2, 3, 5, 7, 8, 11]
  };
  assert.equal(T.MODES.length, 7);
  for (const id of Object.keys(want)) assert.deepEqual(DC.plain(T.MODES[mi(id)].intervals), want[id], id);
  // Church modes are rotations of the major scale at their parent offset.
  for (const id of ['minor', 'dorian', 'phrygian', 'lydian', 'mixolydian']) {
    const m = T.MODES[mi(id)];
    const pcs = DC.plain(m.intervals).map((x) => (x + m.parentOffset) % 12).sort((a, b) => a - b);
    assert.deepEqual(pcs, want.major, id + ' is a rotation of major');
  }
});

test('diatonic triad qualities per mode', () => {
  const q = (modeId) => [0, 1, 2, 3, 4, 5, 6].map((d) => C.qualitySymbol(C.diatonic(mi(modeId), d)));
  assert.deepEqual(q('major'), ['', 'm', 'm', '', '', 'm', 'dim']);
  assert.deepEqual(q('minor'), ['m', 'dim', '', 'm', 'm', '', '']);
  assert.deepEqual(q('dorian'), ['m', 'm', '', '', 'm', 'dim', '']);
  assert.deepEqual(q('phrygian'), ['m', '', '', 'm', 'dim', '', 'm']);
  assert.deepEqual(q('lydian'), ['', '', 'm', 'dim', '', 'm', 'm']);
  assert.deepEqual(q('mixolydian'), ['', 'm', 'dim', '', 'm', 'm', '']);
  assert.deepEqual(q('harmonicMinor'), ['m', 'dim', '+', 'm', '', '', 'dim']);
});

test('key spelling follows the key signature', () => {
  T.setAsciiAccidentals(true);
  try {
    assert.equal(T.keyName(3, mi('major')), 'Eb');
    assert.equal(T.keyName(6, mi('major')), 'F#');
    assert.equal(T.keyName(10, mi('minor')), 'Bb');
    assert.equal(T.keyName(1, mi('major')), 'Db');
    assert.equal(T.keyName(8, mi('minor')), 'G#');
    // Chord roots are spelled on their scale letter: bVI in C major is Ab, not G#.
    const bVI = C.diatonic(mi('minor'), 5);
    assert.equal(C.name(bVI, 0, mi('major')), 'Ab');
    // F major's IV is Bb.
    assert.equal(C.name(C.diatonic(mi('major'), 3), 5, mi('major')), 'Bb');
  } finally { T.setAsciiAccidentals(false); }
});

test('chord building and naming', () => {
  const maj = mi('major');
  const seventh = (d) => C.applyColor(C.diatonic(maj, d), 1, 0); // u=0 picks the seventh variant
  assert.deepEqual([0, 1, 2, 3, 4, 5, 6].map((d) => C.name(seventh(d), 0, maj)),
    ['Cmaj7', 'Dm7', 'Em7', 'Fmaj7', 'G7', 'Am7', 'Bm7\u266D5']);
  const f = seventh(3); f.slash = 'third';
  assert.equal(C.name(f, 0, maj), 'Fmaj7/A');
  assert.equal(C.name(C.applyColor(C.diatonic(maj, 5), 2, 0), 0, maj), 'Am9');
  assert.equal(C.name(C.applyColor(C.diatonic(maj, 0), 4, 0), 0, maj), 'Cmaj13');
  assert.equal(C.name(C.applyColor(C.diatonic(maj, 3), 3, 0), 0, maj), 'Fmaj9\u266F11');
  assert.equal(C.name(C.applyColor(C.diatonic(maj, 4), 4, 0), 0, maj), 'G13');
  // A harmonic minor: the V chord's chord scale gives a b9 naturally.
  const hm = mi('harmonicMinor');
  assert.equal(C.name(C.applyColor(C.diatonic(hm, 4), 2, 0), 9, hm), 'E7\u266D9');
  assert.equal(C.name(C.applyColor(C.diatonic(hm, 6), 1, 0), 9, hm), 'G\u266Fdim7');
  // Suspended and add9 variants.
  const sus = C.applyColor(C.diatonic(maj, 4), 1, 0.99);
  assert.ok(['G7sus4', 'Gadd9', 'G7'].indexOf(C.name(sus, 0, maj)) >= 0);
  const tones = DC.plain(C.tones(C.applyColor(C.diatonic(maj, 1), 2, 0)));
  assert.deepEqual(tones, [0, 3, 7, 10, 14]);
});

test('extensions avoid minor-9th clashes except b9/b13 on dominants', () => {
  for (let m = 0; m < T.MODES.length; m++) {
    for (let d = 0; d < 7; d++) {
      for (let level = 2; level <= 4; level++) {
        const c = C.applyColor(C.diatonic(m, d), level, 0);
        const chordTones = [0, c.third, c.fifth, c.seventh].filter((x) => x !== null).map((x) => x % 12);
        const dom = c.third === 4 && c.seventh === 10;
        for (const e of [c.nine, c.eleven, c.thirteen].filter((x) => x !== null)) {
          const pc = e % 12;
          assert.ok(!chordTones.includes(pc), 'no doubled tone');
          for (const t of chordTones) {
            if ((pc - t + 12) % 12 === 1) assert.ok(dom && (pc === 1 || pc === 8), `mode ${m} degree ${d}: ${C.name(c, 0, m)}`);
          }
          // Extensions stay inside the chord scale.
          const scalePcs = Array.from(c.scale);
          assert.ok(scalePcs.includes(pc), `extension in scale: ${C.name(c, 0, m)}`);
        }
      }
    }
  }
});
