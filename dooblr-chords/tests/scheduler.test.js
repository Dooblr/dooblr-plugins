'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const E = DC.engine, S = DC.scheduler;

function makeBuild(opts) {
  opts = opts || {};
  return E.build({
    root: opts.root != null ? opts.root : 0,
    mode: opts.mode != null ? opts.mode : 1,
    length: opts.length != null ? opts.length : 4,
    duration: opts.duration != null ? opts.duration : 1,
    color: opts.color != null ? opts.color : 35,
    chromatic: opts.chromatic != null ? opts.chromatic : 10,
    register: opts.register != null ? opts.register : 60,
    spread: opts.spread != null ? opts.spread : 25,
    bass: opts.bass != null ? opts.bass : 1,
    velocity: opts.velocity != null ? opts.velocity : 100,
    humanize: opts.humanize != null ? opts.humanize : 0,
    seed: opts.seed != null ? opts.seed : 1,
    kind: 0,
    lead: opts.lead ? 1 : 0,
    density: opts.density != null ? opts.density : 50,
    notes: opts.notes != null ? opts.notes : 3
  }, opts.locks || []);
}

function clock(playing, beats, barBeats) {
  return { playing: !!playing, beats: beats, barBeats: barBeats || 4, tempo: 120 };
}

function run(s, playing, beats, hooks) {
  return S.process(s, clock(playing, beats, 4), hooks || {});
}

function bookkeep() {
  const held = new Map(); // pitch -> on-count
  const ons = [];
  const offs = [];
  return {
    apply(out) {
      // Prefer the time-ordered event list so a poll that crosses two
      // chord boundaries offs-then-ons at the first, then the second.
      const evs = (out.events && out.events.length)
        ? out.events
        : (out.offs || []).map((n) => ({ type: 'off', pitch: n.pitch })).concat(
            (out.ons || []).map((n) => ({ type: 'on', pitch: n.pitch, velocity: n.velocity }))
          );
      evs.forEach((n) => {
        if (n.type === 'off') {
          offs.push(n.pitch);
          const c = held.get(n.pitch) || 0;
          assert.ok(c > 0, `note-off for ${n.pitch} with no matching note-on (held=${[...held.keys()]})`);
          if (c === 1) { held.delete(n.pitch); }
          else { held.set(n.pitch, c - 1); }
        } else {
          ons.push(n.pitch);
          const c = held.get(n.pitch) || 0;
          assert.equal(c, 0, `note-on for ${n.pitch} while already held`);
          held.set(n.pitch, 1);
        }
      });
      const schedHeld = DC.plain(S.heldPitches(this.state)).sort((a, b) => a - b);
      assert.deepEqual([...held.keys()].sort((a, b) => a - b), schedHeld,
        'bookkeeping held set must match scheduler');
    },
    get held() { return [...held.keys()].sort((a, b) => a - b); },
    get ons() { return ons; },
    get offs() { return offs; },
    countOn: () => ons.length,
    countOff: () => offs.length,
    assertClear(label) {
      assert.deepEqual(this.held, [], `${label}: held pitches ${this.held}`);
      assert.equal(this.countOn(), this.countOff(), `${label}: ons ${this.countOn()} offs ${this.countOff()}`);
    },
    state: null
  };
}

test('nextBar snaps to the current bar line or the next one', () => {
  assert.equal(S.nextBar(0, 4), 0);
  assert.equal(S.nextBar(4, 4), 4);
  assert.equal(S.nextBar(8, 4), 8);
  assert.ok(S.nextBar(0.0000001, 4) === 0 || S.nextBar(0.0000001, 4) === 4);
  assert.equal(S.nextBar(0.01, 4), 4);
  assert.equal(S.nextBar(3.99, 4), 4);
  assert.equal(S.nextBar(4.01, 4), 8);
  assert.equal(S.nextBar(5, 3), 6);
});

test('chordAt follows song position around the loop', () => {
  const b = makeBuild({ length: 4, duration: 1, humanize: 0, seed: 7 });
  assert.equal(b.lengthBeats, 16);
  assert.equal(S.chordAt(b, 0), 0);
  assert.equal(S.chordAt(b, 3.99), 0);
  assert.equal(S.chordAt(b, 4), 1);
  assert.equal(S.chordAt(b, 8), 2);
  assert.equal(S.chordAt(b, 12), 3);
  assert.equal(S.chordAt(b, 16), 0);
  assert.equal(S.chordAt(b, 20), 1);
  assert.equal(S.chordAt(b, 32), 0);
});

test('LIVE waits for the next bar, then attacks the chord for that position', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 3, notes: 3 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;

  let out = run(s, true, 1.5);
  bk.apply(out);
  assert.equal(out.started, false);
  assert.equal(out.chordIndex, -1);
  assert.deepEqual(bk.held, []);

  out = run(s, true, 3.9);
  bk.apply(out);
  assert.equal(out.started, false);
  assert.deepEqual(bk.held, []);

  out = run(s, true, 4.0);
  bk.apply(out);
  assert.equal(out.started, true);
  assert.equal(out.chordIndex, 1); // bar 2 = chord 1 of a 1-bar-each progression starting at beat 0
  assert.equal(bk.held.length, 3, `voices at first attack: ${bk.held}`);
  assert.equal(new Set(bk.held).size, 3);
});

test('starts immediately when transport is already on a bar line', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 3, notes: 3 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;
  const out = run(s, true, 0);
  bk.apply(out);
  assert.equal(out.started, true);
  assert.equal(out.chordIndex, 0);
  assert.equal(bk.held.length, 3);
});

test('every note-on gets exactly one note-off: loop, stop, no overlaps', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 11, notes: 4 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;
  // 10ms-like steps over two full cycles plus a bit, starting on a bar.
  for (let t = 0; t <= 32.25; t += 0.25) {
    const out = run(s, true, t);
    // offs must be listed before ons in the same tick (scheduler contract)
    const firstOn = out.ons.length ? 1 : 0;
    // just apply in the order the host will send: offs then ons
    bk.apply(out);
    if (out.started && out.ons.length && out.offs.length) {
      // After a chord change, held should be the new chord only.
      assert.equal(bk.held.length, 4, `at t=${t} held=${bk.held} ons=${out.ons.map((n) => n.pitch)} offs=${out.offs.map((n) => n.pitch)}`);
    }
    if (out.started && t % 4 < 0.01) {
      // On a chord boundary after humanize=0 attack, exact N unique voices.
      assert.equal(bk.held.length, 4, `voices at t=${t}: ${bk.held}`);
      assert.equal(new Set(bk.held).size, 4);
    }
  }
  const afterLoopOns = bk.countOn();
  const afterLoopOffs = bk.countOff();
  assert.ok(afterLoopOns > 0);
  assert.ok(afterLoopOns >= afterLoopOffs);
  // Stop: remaining notes must be released.
  const stop = run(s, false, 32.25);
  bk.apply(stop);
  bk.assertClear('transport stop');
  assert.equal(s.started, false);
  assert.equal(s.chordIndex, -1);
});

test('mode switch to CLIP releases held notes', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 5, notes: 3 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;
  bk.apply(run(s, true, 0));
  assert.ok(bk.held.length > 0);
  S.setLive(s, false);
  bk.apply(run(s, true, 0.25));
  bk.assertClear('CLIP mode switch');
});

test('knob changes apply at the next chord boundary, not immediately', () => {
  const a = makeBuild({ length: 4, humanize: 0, seed: 2, notes: 3, color: 0 });
  const c = makeBuild({ length: 4, humanize: 0, seed: 2, notes: 5, color: 80 });
  const s = S.create({ live: true, build: a });
  const bk = bookkeep(); bk.state = s;
  bk.apply(run(s, true, 0));
  assert.equal(bk.held.length, 3);
  const midHeld = bk.held.slice();
  S.queueBuild(s, c, 'chord');
  // Still inside chord 0: voices stay at 3.
  bk.apply(run(s, true, 2));
  assert.deepEqual(bk.held, midHeld, 'pending notes param must not change mid-chord');
  assert.equal(bk.held.length, 3);
  // Cross the chord 1 boundary at beat 4.
  bk.apply(run(s, true, 4));
  assert.equal(s.chordIndex, 1);
  assert.equal(bk.held.length, 5, `after boundary held=${bk.held}`);
  assert.equal(new Set(bk.held).size, 5);
});

test('Generate/Reroll in LIVE applies at the next bar', () => {
  const a = makeBuild({ length: 4, humanize: 0, seed: 1, notes: 3 });
  const c = makeBuild({ length: 4, humanize: 0, seed: 99, notes: 3 });
  const s = S.create({ live: true, build: a });
  const bk = bookkeep(); bk.state = s;
  bk.apply(run(s, true, 0));
  const first = bk.held.slice();
  S.queueBuild(s, c, 'bar');
  bk.apply(run(s, true, 2)); // mid-bar: still the old progression
  assert.deepEqual(bk.held, first);
  bk.apply(run(s, true, 4));
  // New seed should (very likely) be a different voicing at chord 1.
  // Even if pitches coincide, voice count stays exact and bookkeeping is clean.
  assert.equal(bk.held.length, 3);
  assert.equal(new Set(bk.held).size, 3);
  const namesA = a.display.map((d) => d.name).join(' ');
  const namesC = c.display.map((d) => d.name).join(' ');
  assert.ok(namesA !== namesC || JSON.stringify(a.notes) !== JSON.stringify(c.notes),
    'test seeds should produce different builds');
});

test('playhead jump releases then attacks the chord at the new position', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 8, notes: 3 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;
  bk.apply(run(s, true, 0));
  assert.equal(s.chordIndex, 0);
  bk.apply(run(s, true, 12)); // jump to chord 3
  assert.equal(s.chordIndex, 3);
  assert.equal(bk.held.length, 3);
  bk.apply(run(s, false, 12));
  bk.assertClear('stop after jump');
});

test('LEAD is monophonic live, still offs-before-ons', () => {
  const b = makeBuild({ length: 4, humanize: 0, seed: 4, lead: true, density: 80 });
  const s = S.create({ live: true, build: b });
  const bk = bookkeep(); bk.state = s;
  for (let t = 0; t <= 16; t += 0.25) {
    const out = run(s, true, t);
    bk.apply(out);
    assert.ok(bk.held.length <= 1, `lead polyphony ${bk.held.length} at t=${t}: ${bk.held}`);
  }
  bk.apply(run(s, false, 16));
  bk.assertClear('lead stop');
});

test('EVOLVE mutates inner chords deterministically with seed and cycle', () => {
  const base = makeBuild({ length: 8, humanize: 0, seed: 21, notes: 3, color: 40, chromatic: 40 });
  const a = E.evolveBuild(base, 1, []);
  const b = E.evolveBuild(base, 1, []);
  const c = E.evolveBuild(base, 2, []);
  const names = (x) => x.display.map((d) => d.name).join(' ');
  assert.equal(names(a), names(b), 'same seed+cycle must be deterministic');
  // First and last chords stay put.
  assert.equal(a.display[0].name, base.display[0].name);
  assert.equal(a.display[a.display.length - 1].name, base.display[base.display.length - 1].name);
  const changedA = a.display.filter((d) => d.changed).length;
  assert.ok(changedA >= 1 && changedA <= 2, `evolve should change 1-2 inner chords, got ${changedA} (${names(base)} -> ${names(a)})`);
  // Cycle 2 is allowed to match cycle 1, but the call is still deterministic.
  const d = E.evolveBuild(base, 2, []);
  assert.equal(names(c), names(d));
  // Locked inner chords are not mutated.
  const locks = [];
  for (let i = 0; i < 8; i++) { locks[i] = i !== 0 && i !== 7; }
  const locked = E.evolveBuild(base, 1, locks);
  assert.equal(names(locked), names(base), 'all-inner-locked evolve is a no-op');
});

test('EVOLVE fires at cycle wrap, not mid-cycle, and keeps bookkeeping clean', () => {
  const base = makeBuild({ length: 4, humanize: 0, seed: 21, notes: 3, color: 50, chromatic: 50 });
  const s = S.create({ live: true, build: base, evolveEvery: 1 });
  const bk = bookkeep(); bk.state = s;
  const hooks = {
    evolve: function (prev, cycle) { return E.evolveBuild(prev, cycle, []); }
  };
  const mid = run(s, true, 0);
  bk.apply(mid);
  const firstNames = base.display.map((d) => d.name).join(' ');
  // Mid first cycle: no evolve yet.
  bk.apply(S.process(s, clock(true, 8), hooks));
  assert.equal(s.build.display.map((d) => d.name).join(' '), firstNames);
  assert.equal(s.cycle, 0);
  // Wrap at beat 16: evolve of cycle 1.
  bk.apply(S.process(s, clock(true, 16), hooks));
  assert.equal(s.cycle, 1);
  const evolved = s.build.display.map((d) => d.name).join(' ');
  const changed = s.build.display.filter((d) => d.changed).length;
  assert.ok(changed >= 1, `expected a mutation at wrap, still ${evolved}`);
  assert.equal(bk.held.length, 3);
  // Every-2-cycles: a fresh scheduler should not evolve at cycle 1.
  const s2 = S.create({ live: true, build: base, evolveEvery: 2 });
  const bk2 = bookkeep(); bk2.state = s2;
  bk2.apply(S.process(s2, clock(true, 0), hooks));
  bk2.apply(S.process(s2, clock(true, 16), hooks));
  assert.equal(s2.cycle, 1);
  assert.equal(s2.build.display.map((d) => d.name).join(' '), firstNames, 'every-2 skips cycle 1');
  bk2.apply(S.process(s2, clock(true, 32), hooks));
  assert.equal(s2.cycle, 2);
  assert.ok(s2.build.display.filter((d) => d.changed).length >= 1, 'every-2 evolves at cycle 2');
  bk.apply(run(s, false, 16));
  bk.assertClear('evolve stop');
  bk2.apply(run(s2, false, 32));
  bk2.assertClear('evolve/2 stop');
});

test('defaults add playmode CLIP and evolve Off without changing Notes', () => {
  assert.equal(E.VERSION, '0.8.0');
  assert.equal(E.DEFAULTS.playmode, 0);
  assert.equal(E.DEFAULTS.evolve, 0);
  assert.equal(E.DEFAULTS.notes, 3);
  assert.equal(E.normalize({}).playmode, 0);
  assert.equal(E.normalize({ playmode: 1 }).playmode, 1);
  assert.equal(E.evolveEvery(0), 0);
  assert.equal(E.evolveEvery(1), 1);
  assert.equal(E.evolveEvery(2), 2);
  assert.equal(E.evolveEvery(3), 4);
});

test('reroll mid-play releases then attacks, no leftover pitches', () => {
  const a = makeBuild({ length: 4, humanize: 20, seed: 1, notes: 6 });
  const c = makeBuild({ length: 4, humanize: 20, seed: 2, notes: 6 });
  const s = S.create({ live: true, build: a });
  const bk = bookkeep(); bk.state = s;
  for (let t = 0; t <= 6; t += 0.5) { bk.apply(run(s, true, t)); }
  S.queueBuild(s, c, 'bar');
  for (let t = 6.5; t <= 12; t += 0.5) { bk.apply(run(s, true, t)); }
  bk.apply(run(s, false, 12));
  bk.assertClear('reroll then stop');
});
