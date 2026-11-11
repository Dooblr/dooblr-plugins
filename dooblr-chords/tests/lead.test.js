'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const T = DC.theory, C = DC.chords, E = DC.engine, L = DC.lead;

const SEEDS = Array.from({ length: 80 }, (_, i) => ((i * 7919 + 17) % 99999) + 1);

function activeChord(chords, beats, slots, t) {
  if (slots) {
    for (let i = 0; i < slots.length; i++) {
      if (t >= slots[i].start - 1e-6 && t < slots[i].start + slots[i].duration - 1e-9) { return chords[i]; }
    }
    return chords[chords.length - 1];
  }
  const i = Math.min(chords.length - 1, Math.max(0, Math.floor(t / beats + 1e-9)));
  return chords[i];
}

test('lead output is deterministic for a given seed and settings', () => {
  const s = { lead: 1, root: 0, mode: 1, length: 8, color: 40, chromatic: 20, density: 60, seed: 4242, humanize: 30, register: 60 };
  assert.deepEqual(DC.plain(E.build(s).notes), DC.plain(E.build(s).notes));
  const a = E.build(s).notes;
  const b = E.build(Object.assign({}, s, { seed: 4243 })).notes;
  assert.notDeepEqual(DC.plain(a), DC.plain(b));
});

test('lead notes stay in the key when Spice is 0', () => {
  for (const seed of SEEDS.slice(0, 40)) {
    for (let mode = 0; mode < T.MODES.length; mode++) {
      const b = E.build({ lead: 1, root: 0, mode, length: 6, color: 50, chromatic: 0, density: 70, seed, register: 60 });
      const scale = L.scalePcs(0, mode);
      b.notes.forEach((n) => {
        assert.ok(DC.plain(scale).indexOf(((n.pitch % 12) + 12) % 12) >= 0,
          `pitch ${n.pitch} not in mode ${mode} seed ${seed}`);
      });
    }
  }
});

test('lead is monophonic: one note at a time, no overlaps', () => {
  for (const seed of SEEDS.slice(0, 40)) {
    const b = E.build({ lead: 1, length: 8, color: 35, chromatic: 10, density: 80, seed, humanize: 80, register: 60 });
    const notes = b.notes.slice().sort((a, c) => a.start_time - c.start_time);
    assert.ok(notes.length > 0, 'produced notes');
    for (let i = 1; i < notes.length; i++) {
      assert.ok(notes[i - 1].start_time + notes[i - 1].duration <= notes[i].start_time + 1e-6,
        `overlap at ${i}: ${notes[i - 1].start_time}+${notes[i - 1].duration} vs ${notes[i].start_time} seed ${seed}`);
    }
  }
});

test('lead lasts the full clip and does not run past the end', () => {
  for (const dur of [0, 1, 2]) {
    for (const len of [4, 8]) {
      const b = E.build({ lead: 1, length: len, duration: dur, density: 50, seed: 99, chromatic: 0 });
      const beats = E.DURATIONS[dur].beats * len;
      assert.equal(b.lengthBeats, beats);
      assert.ok(b.notes.length > 0);
      const end = Math.max(...b.notes.map((n) => n.start_time + n.duration));
      assert.ok(end <= beats + 1e-6, `end ${end} > ${beats}`);
      assert.ok(end >= beats * 0.5, `line ended too early at ${end} of ${beats}`);
    }
  }
});

test('strong beats are mostly chord tones', () => {
  let strong = 0, chordal = 0;
  for (const seed of SEEDS) {
    const s = { lead: 1, root: 0, mode: 1, length: 8, color: 20, chromatic: 10, density: 55, seed, register: 60 };
    const b = E.build(s);
    const beats = E.DURATIONS[s.duration || 1].beats;
    b.notes.forEach((n) => {
      if (L.strength(n.start_time) !== 'strong') { return; }
      strong++;
      const ch = activeChord(b.chords, beats, null, n.start_time);
      const tones = L.chordTonePcs(ch, 0);
      if (DC.plain(tones).indexOf(((n.pitch % 12) + 12) % 12) >= 0) { chordal++; }
    });
  }
  const ratio = strong ? chordal / strong : 0;
  assert.ok(strong > 50, `need a sample of strong beats, got ${strong}`);
  assert.ok(ratio >= 0.65, `strong-beat chord-tone ratio ${ratio.toFixed(3)} (${chordal}/${strong})`);
});

test('clip names and heading show Lead; default build is still a chord clip', () => {
  const chords = E.build({ seed: 9, length: 4 });
  assert.equal(chords.heading, 'PROGRESSION');
  assert.equal(chords.clipName, 'C min \u00B7 4 \u00B7 Col35 Chr10');
  assert.ok(chords.notes.some((n, i) => i > 0 && Math.abs(n.start_time - chords.notes[0].start_time) < 1e-6),
    'chord clip is polyphonic on the first hit');

  const lead = E.build({ seed: 9, length: 4, lead: 1 });
  assert.equal(lead.heading, 'LEAD');
  assert.equal(lead.clipName, 'Lead \u00B7 C min \u00B7 4 \u00B7 Col35 Chr10');
  assert.equal(lead.chords.length, chords.chords.length);
  assert.deepEqual(DC.plain(lead.display.map((d) => d.name)), DC.plain(chords.display.map((d) => d.name)));
});

test('Lead uses locked chords in the harmonic grid', () => {
  const first = E.build({ seed: 50, length: 4, lead: 0 });
  const lock = first.chords[1];
  const lead = E.build({ seed: 50, length: 4, lead: 1 }, [null, lock, null, null]);
  assert.equal(lead.chords[1].root, lock.root);
  assert.equal(lead.chords[1].degree, lock.degree);
});
