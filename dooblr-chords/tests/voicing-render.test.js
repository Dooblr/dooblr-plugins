'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const T = DC.theory, C = DC.chords, E = DC.engine, V = DC.voicing;

const SEEDS = Array.from({ length: 80 }, (_, i) => ((i * 104729 + 7) % 99999) + 1);

test('every note is a valid MIDI note (0..127) at extreme settings', () => {
  for (const register of [36, 72]) {
    for (const spread of [0, 50, 100]) {
      for (const bass of [0, 1]) {
        for (const seed of SEEDS.slice(0, 20)) {
          const b = E.build({ root: seed % 12, mode: seed % 7, length: 16, color: 100, chromatic: 100, register, spread, bass, velocity: 127, humanize: 100, seed });
          for (const n of b.notes) {
            assert.ok(Number.isInteger(n.pitch) && n.pitch >= 0 && n.pitch <= 127, 'pitch ' + n.pitch);
            assert.ok(n.velocity >= 1 && n.velocity <= 127, 'velocity ' + n.velocity);
          }
        }
      }
    }
  }
});

test('voicings contain the right pitch classes, and slash basses are lowest', () => {
  for (const bass of [0, 1]) {
    for (const seed of SEEDS) {
      const key = seed % 12;
      const b = E.build({ root: key, mode: seed % 7, length: 8, color: (seed % 4) * 30, chromatic: 40, bass, seed });
      b.chords.forEach((ch, i) => {
        const v = b.voicings[i];
        const allowed = DC.plain(C.tones(ch)).map((t) => (key + ch.root + t) % 12);
        for (const p of v.notes) assert.ok(allowed.includes(p % 12), 'only chord tones');
        const bassPc = (key + ch.root + C.bassInterval(ch)) % 12;
        if (bass || ch.slash) assert.equal(v.notes[0] % 12, bassPc, `${b.display[i].name} bass`);
        // The third (when present) is always voiced: it defines the quality.
        if (ch.third !== null) assert.ok(v.notes.some((p) => p % 12 === (key + ch.root + ch.third) % 12), 'third voiced');
      });
    }
  }
});

test('voice leading moves upper voices less than naive root position', () => {
  let led = 0, naive = 0;
  for (const seed of SEEDS) {
    const b = E.build({ mode: seed % 7, length: 8, color: 0, chromatic: 0, bass: 1, spread: 0, seed });
    const rootPos = b.chords.map((ch) => {
      const iv = DC.plain(C.tones(ch));
      return iv.map((t) => 60 + ch.root + t);
    });
    for (let i = 1; i < 8; i++) {
      const move = (a, c) => a.reduce((s, p) => s + Math.min(...c.map((q) => Math.abs(p - q))), 0);
      led += move(DC.plain(b.voicings[i].upper), DC.plain(b.voicings[i - 1].upper));
      naive += move(rootPos[i], rootPos[i - 1]);
    }
  }
  assert.ok(led < naive * 0.75, `voice-led ${led} vs naive ${naive}`);
});

test('register keeps the upper voicing near the chosen note', () => {
  for (const register of [40, 60, 72]) {
    for (const seed of SEEDS.slice(0, 30)) {
      const b = E.build({ length: 8, register, seed, color: 50 });
      for (const v of b.voicings) assert.ok(Math.abs(v.upper[0] - register) <= 12, `low ${v.upper[0]} vs ${register}`);
    }
  }
});

test('spread opens the voicing', () => {
  let close = 0, open = 0;
  for (const seed of SEEDS) {
    const span = (spread) => E.build({ length: 8, color: 40, spread, seed, bass: 1 }).voicings
      .reduce((s, v) => s + v.upper[v.upper.length - 1] - v.upper[0], 0);
    close += span(0); open += span(100);
  }
  assert.ok(open > close * 1.3, `open ${open} close ${close}`);
});

test('notes fill the clip: starts, durations and total length', () => {
  for (const duration of [0, 1, 2]) {
    const beats = E.DURATIONS[duration].beats;
    const b = E.build({ length: 5, duration, seed: 3 });
    assert.equal(b.lengthBeats, 5 * beats);
    for (const n of b.notes) {
      assert.ok(n.start_time >= 0 && n.start_time + n.duration <= b.lengthBeats + 1e-9);
      assert.equal(n.start_time % beats, 0, 'no humanize: on the grid');
      assert.equal(n.duration, beats);
      assert.equal(n.velocity, 100);
      assert.equal(n.mute, 0);
    }
  }
});

test('humanize is off by default, seeded, and bounded', () => {
  const s = { length: 8, seed: 42, humanize: 100, velocity: 90 };
  const a = E.build(s), b = E.build(s);
  assert.deepEqual(DC.plain(a.notes), DC.plain(b.notes));
  let moved = 0;
  for (const n of a.notes) {
    const slot = Math.round(n.start_time / 4) * 4;
    assert.ok(Math.abs(n.start_time - slot) <= 0.05 + 1e-9);
    assert.ok(n.start_time >= 0);
    assert.ok(Math.abs(n.velocity - 90) <= 14);
    if (n.start_time !== slot || n.velocity !== 90) moved++;
  }
  assert.ok(moved > a.notes.length / 2);
  assert.equal(E.DEFAULTS.humanize, 0);
});

test('rhythm pattern registry is an extension point', () => {
  const R = DC.render;
  assert.equal(typeof R.PATTERNS.sustain, 'function');
  const v = [{ notes: [60, 64, 67] }];
  const strum = DC.plain(R.notes(v, { chordBeats: 4, velocity: 100, humanize: 0, seed: 1, strum: 0.25 }));
  assert.deepEqual(strum.map((n) => n.start_time), [0, 0.25, 0.5]);
});

test('rng: uniform range and stable streams', () => {
  const s = DC.rng.create(123);
  for (let i = 0; i < 1000; i++) { const x = s.next(); assert.ok(x >= 0 && x < 1); }
  assert.equal(DC.rng.stream(5, 2, 1).next(), DC.rng.stream(5, 2, 1).next());
  assert.notEqual(DC.rng.stream(5, 2, 1).next(), DC.rng.stream(5, 3, 1).next());
  assert.equal(DC.rng.pickIndex([0, 0, 0], 0.5), -1);
  assert.equal(DC.rng.pickIndex([0, 1, 0], 0.99), 1);
});
