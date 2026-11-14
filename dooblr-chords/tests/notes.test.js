'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const { buildSmf, chordNotes } = require('./smf');
const DC = loadEngine();
const C = DC.chords, E = DC.engine, T = DC.theory;

function upperPcs(v) {
  return [...new Set(DC.plain(v.upper).map((p) => p % 12))];
}

test('default Notes is Triad (3), and 0.6.0 Auto/2 values fall back to a triad', () => {
  assert.equal(E.VERSION, '0.6.1');
  assert.equal(E.DEFAULTS.notes, 3);
  assert.equal(E.normalize({}).notes, 3);
  assert.equal(E.normalize({ notes: 0 }).notes, 3);
  assert.equal(E.normalize({ notes: 2 }).notes, 3);
  assert.equal(E.notesFromTab(0), 3);
  assert.equal(E.notesFromTab(1), 4);
  assert.equal(E.notesFromTab(2), 5);
  assert.equal(E.notesFromTab(3), 6);
  assert.equal(E.tabFromNotes(5), 2);
  assert.equal(E.notesLabel(3), 'TRI');
  assert.equal(E.notesLabel(6), '11th');
  assert.equal(E.clipName({ root: 0, mode: 1, length: 4, color: 35, chromatic: 10 }), 'C min \u00B7 4 \u00B7 TRI \u00B7 Col35 Chr10');
  assert.equal(E.clipName({ root: 0, mode: 1, length: 4, color: 35, chromatic: 10, notes: 5 }), 'C min \u00B7 4 \u00B7 9th \u00B7 Col35 Chr10');
});

test('each Notes setting yields exactly N distinct upper pitch classes', () => {
  const seeds = [1, 7, 42, 99, 333, 1000, 48213, 8888];
  for (const notes of [3, 4, 5, 6]) {
    for (const bass of [0, 1]) {
      for (const color of [0, 35, 100]) {
        for (const chromatic of [0, 10, 80]) {
          for (const mode of [0, 1, 2, 4, 6]) {
            for (const seed of seeds) {
              const b = E.build({
                root: seed % 12, mode, length: 8, color, chromatic,
                register: 60, spread: 25, bass, velocity: 100, humanize: 0, seed, notes
              });
              assert.equal(b.clipName.includes(E.notesLabel(notes)), true, b.clipName);
              b.voicings.forEach((v, i) => {
                const ch = b.chords[i];
                const pcs = upperPcs(v);
                assert.equal(pcs.length, notes, `${b.display[i].name} notes=${notes} color=${color} seed=${seed} pcs=${pcs}`);
                assert.equal(DC.plain(v.upper).length, notes, 'no octave doublings in the upper voicing');
                const rootPc = (b.settings.root + ch.root) % 12;
                const fifthPc = (rootPc + ch.fifth) % 12;
                assert.ok(pcs.includes(rootPc), 'root kept');
                assert.ok(pcs.includes(fifthPc), 'fifth never dropped: ' + b.display[i].name);
                const expect = notes + (bass || ch.slash ? 1 : 0);
                assert.equal(DC.plain(v.notes).length, expect, 'bass/slash are extra');
              });
            }
          }
        }
      }
    }
  }
});

test('Color and Spice do not change the Notes count', () => {
  for (const color of [0, 35, 100]) {
    for (const chromatic of [0, 100]) {
      const b = E.build({ root: 0, mode: 0, length: 8, color, chromatic, bass: 0, seed: 42, notes: 3 });
      b.voicings.forEach((v) => assert.equal(upperPcs(v).length, 3));
    }
  }
});

test('block names and applyNotes spelling match the chord type', () => {
  const maj = 0;
  const I = C.diatonic(maj, 0);
  C.applyNotes(I, 3);
  assert.equal(C.name(I, 0, maj), 'C');
  C.applyNotes(I, 4);
  assert.equal(C.name(I, 0, maj), 'Cmaj7');
  C.applyNotes(I, 5);
  assert.equal(C.name(I, 0, maj), 'Cmaj9');
  C.applyNotes(I, 6);
  assert.equal(C.name(I, 0, maj), 'Cmaj13', 'natural 11 clashes with a major 3rd, so I uses 13');

  const F = C.diatonic(maj, 3);
  C.applyNotes(F, 6);
  assert.equal(C.name(F, 0, maj), 'Fmaj9\u266F11', 'Lydian IV uses #11');

  const G = C.diatonic(maj, 4);
  C.applyNotes(G, 4);
  assert.equal(C.name(G, 0, maj), 'G7');
  C.applyNotes(G, 5);
  assert.equal(C.name(G, 0, maj), 'G9');
  C.applyNotes(G, 6);
  assert.equal(C.name(G, 0, maj), 'G13', 'natural 11 clashes on a dominant, so V uses 13');

  const Am = C.diatonic(maj, 5);
  C.applyNotes(Am, 5);
  assert.equal(C.name(Am, 0, maj), 'Am9');
  C.applyNotes(Am, 6);
  assert.equal(C.name(Am, 0, maj), 'Am11', 'natural 11 is fine over a minor 3rd');

  const b = E.build({ root: 0, mode: 0, length: 4, color: 0, chromatic: 0, seed: 1, notes: 4, bass: 0 });
  b.display.forEach((d) => assert.ok(/7|maj/.test(d.name), '7th setting names a seventh: ' + d.name));
  const b9 = E.build({ root: 0, mode: 0, length: 4, color: 0, chromatic: 0, seed: 1, notes: 5, bass: 0 });
  b9.display.forEach((d) => assert.ok(/9/.test(d.name), '9th setting names a ninth: ' + d.name));
  const b11 = E.build({ root: 0, mode: 0, length: 4, color: 0, chromatic: 0, seed: 1, notes: 6, bass: 0 });
  b11.display.forEach((d) => assert.ok(/11|13/.test(d.name), '11th setting names 11 or 13: ' + d.name));
  const b3 = E.build({ root: 0, mode: 0, length: 4, color: 100, chromatic: 80, seed: 1, notes: 3, bass: 0 });
  b3.display.forEach((d) => {
    assert.equal(/7|9|11|13/.test(d.name), false, 'triads stay triads even at Color 100: ' + d.name);
  });
});

test('Notes forces VAR/B derived chords to N pitch classes', () => {
  const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
  const an = E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff');
  const src = { analysis: an, keep: {} };
  for (const notes of [3, 4, 5, 6]) {
    const b = E.build({ root: 9, mode: 1, length: 4, color: 35, chromatic: 10, bass: 1, seed: 1, notes, kind: 0 }, null, src);
    assert.ok(b.clipName.includes(E.notesLabel(notes)), b.clipName);
    b.voicings.forEach((v) => {
      assert.equal(upperPcs(v).length, notes);
      assert.equal(DC.plain(v.upper).length, notes);
      assert.equal(DC.plain(v.notes).length, notes + 1);
    });
  }
});
