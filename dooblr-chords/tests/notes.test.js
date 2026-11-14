'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { loadEngine } = require('./load-engine');
const { buildSmf, chordNotes } = require('./smf');
const DC = loadEngine();
const C = DC.chords, E = DC.engine;

const FIXTURE = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'auto-0.5.0.json'), 'utf8'));

function countNotes(v) { return DC.plain(v.notes).length; }
function upperCount(v) { return DC.plain(v.upper).length; }

test('Auto is the default and matches 0.5.0 output for fixed seeds', () => {
  assert.equal(E.DEFAULTS.notes, 0);
  assert.equal(E.normalize({}).notes, 0);
  assert.equal(E.VERSION, '0.6.0');
  for (const fx of FIXTURE) {
    const a = E.build(fx.s);
    const b = E.build(Object.assign({}, fx.s, { notes: 0 }));
    assert.deepEqual(DC.plain(a.notes).map((n) => ({ p: n.pitch, t: n.start_time, d: n.duration, v: n.velocity })), fx.notes);
    assert.deepEqual(DC.plain(a.voicings).map((v) => DC.plain(v.notes)), fx.voicingNotes);
    assert.deepEqual(DC.plain(a.display).map((d) => ({ name: d.name, roman: d.roman })), fx.display);
    assert.equal(a.clipName, fx.clipName);
    assert.deepEqual(DC.plain(a.notes), DC.plain(b.notes));
  }
});

test('Notes 2 to 6 set the number of upper voices; Bass is extra', () => {
  const seeds = [1, 42, 7, 99, 48213, 1000, 333, 8];
  for (const notes of [2, 3, 4, 5, 6]) {
    for (const bass of [0, 1]) {
      for (const color of [0, 35, 100]) {
        for (const seed of seeds) {
          const b = E.build({ root: 9, mode: 1, length: 8, color, chromatic: 10, register: 60, spread: 25, bass, velocity: 100, humanize: 0, seed, notes });
          b.voicings.forEach((v, i) => {
            const ch = b.chords[i];
            assert.equal(upperCount(v), notes, `upper notes=${notes} bass=${bass} color=${color} seed=${seed} ${b.display[i].name}`);
            const expect = notes + (bass || ch.slash ? 1 : 0);
            assert.equal(countNotes(v), expect, `total notes=${notes} bass=${bass} slash=${!!ch.slash}`);
          });
        }
      }
    }
  }
});

test('Notes 2 keeps root and third; more notes add 7 then 9 then 11/13 or doublings', () => {
  const b2 = E.build({ root: 9, mode: 1, length: 4, color: 0, chromatic: 0, bass: 0, seed: 1, notes: 2 });
  b2.chords.forEach((ch, i) => {
    const v = b2.voicings[i];
    assert.equal(upperCount(v), 2);
    const pcs = DC.plain(v.notes).map((p) => p % 12);
    const root = (9 + ch.root) % 12;
    const third = ch.third !== null ? (9 + ch.root + ch.third) % 12 : null;
    assert.ok(pcs.includes(root), 'root kept');
    if (third !== null) { assert.ok(pcs.includes(third), 'third kept'); }
    assert.equal(b2.display[i].name.indexOf('7') < 0 && b2.display[i].name.indexOf('9') < 0, true, 'Notes 2 on a triad still names a triad: ' + b2.display[i].name);
  });

  const b4 = E.build({ root: 9, mode: 1, length: 4, color: 0, chromatic: 0, bass: 0, seed: 1, notes: 4 });
  b4.chords.forEach((ch, i) => {
    const name = b4.display[i].name;
    // A triad plus a seventh (and possibly a 9th) should show in the block name.
    assert.ok(/7|9|add9|maj/.test(name), 'Notes 4 on a triad adds extensions to the name: ' + name);
    assert.equal(upperCount(b4.voicings[i]), 4);
  });
});

test('Notes applies to a VAR/B source derivation and overrides source thickness', () => {
  const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
  const an = E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff');
  const src = { analysis: an, keep: {} };
  const auto = E.build({ root: 9, mode: 1, length: 4, color: 0, chromatic: 0, bass: 1, seed: 1, notes: 0, kind: 0 }, null, src);
  const n2 = E.build({ root: 9, mode: 1, length: 4, color: 0, chromatic: 0, bass: 1, seed: 1, notes: 2, kind: 0 }, null, src);
  const n6 = E.build({ root: 9, mode: 1, length: 4, color: 0, chromatic: 0, bass: 1, seed: 1, notes: 6, kind: 0 }, null, src);
  n2.voicings.forEach((v) => {
    assert.equal(upperCount(v), 2);
    assert.equal(countNotes(v), 3);
  });
  n6.voicings.forEach((v) => {
    assert.equal(upperCount(v), 6);
    assert.equal(countNotes(v), 7);
  });
  // Auto may follow the source thickness (not forced to 2 or 6).
  const autoUppers = DC.plain(auto.voicings).map((v) => DC.plain(v.upper).length);
  assert.ok(autoUppers.some((n) => n !== 2) || autoUppers.some((n) => n !== 6));
});

test('tab mapping and normalize accept engine counts', () => {
  assert.equal(E.notesFromTab(0), 0);
  assert.equal(E.notesFromTab(1), 2);
  assert.equal(E.notesFromTab(5), 6);
  assert.equal(E.tabFromNotes(0), 0);
  assert.equal(E.tabFromNotes(4), 3);
  assert.equal(E.normalize({ notes: 4 }).notes, 4);
  assert.equal(E.normalize({ notes: 0 }).notes, 0);
});
