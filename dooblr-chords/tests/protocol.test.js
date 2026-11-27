'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const DC = loadEngine();
const E = DC.engine, P = DC.protocol, S = DC.scheduler;

function makeBuild(opts) {
  opts = opts || {};
  return E.build({
    root: opts.root != null ? opts.root : 0,
    mode: opts.mode != null ? opts.mode : 1,
    length: opts.length != null ? opts.length : 4,
    duration: 1, color: 35, chromatic: 10, register: 60, spread: 25,
    bass: 1, velocity: 100, humanize: 0, seed: opts.seed != null ? opts.seed : 7,
    kind: 0, lead: 0, density: 50, notes: opts.notes != null ? opts.notes : 3
  }, []);
}

test('protocol encode/decode round-trips harmony and hello', () => {
  const b = makeBuild();
  const p = P.fromBuild(b, { playing: false, beats: 0, channel: 2 });
  assert.equal(p.type, 'harmony');
  assert.equal(p.ch, 2);
  assert.equal(p.index, 0);
  assert.equal(p.root, 0);
  assert.ok(p.chord.length >= 3);
  assert.ok(p.scale.length === 7);
  assert.equal(P.decode(P.encode(p)).name, p.name);
  const hello = P.decode(P.encode({ type: 'hello', ch: 5 }));
  assert.equal(hello.type, 'hello');
  assert.equal(hello.ch, 3);
  assert.equal(P.decode('not-json'), null);
});

test('CLIP/LIVE position: chord index follows song beats around the loop', () => {
  const b = makeBuild({ length: 4 });
  assert.equal(b.lengthBeats, 16);
  assert.equal(P.fromBuild(b, { playing: true, beats: 0 }).index, 0);
  assert.equal(P.fromBuild(b, { playing: true, beats: 4 }).index, 1);
  assert.equal(P.fromBuild(b, { playing: true, beats: 12 }).index, 3);
  assert.equal(P.fromBuild(b, { playing: true, beats: 16 }).index, 0);
  assert.equal(P.fromBuild(b, { playing: false, beats: 12 }).index, 0, 'stopped advertises chord 0');
});

test('payload includes key, scale, chord pcs, bass and a name', () => {
  const b = makeBuild({ root: 0, mode: 1, notes: 5, seed: 3 }); // C minor, 9th
  const p = P.fromBuild(b, { playing: true, beats: 0, channel: 0 });
  assert.deepEqual(DC.plain(P.scalePcs(0, 1)).sort((a, c) => a - c), [0, 2, 3, 5, 7, 8, 10]);
  p.chord.forEach((pc) => assert.ok(pc >= 0 && pc <= 11));
  assert.equal(typeof p.bass, 'number');
  assert.ok(p.name && p.name.length > 0);
  const ev = E.evolveBuild(b, 1, []);
  const q = P.fromBuild(ev, { playing: true, beats: 4, channel: 0 });
  assert.equal(q.index, 1);
  assert.ok(q.name);
});

test('identity ignores beat so keep-alives can be skipped', () => {
  const b = makeBuild();
  const a = P.fromBuild(b, { playing: true, beats: 0 });
  const c = P.fromBuild(b, { playing: true, beats: 1.5 });
  assert.equal(P.identity(a), P.identity(c));
  const d = P.fromBuild(b, { playing: true, beats: 4 });
  assert.notEqual(P.identity(a), P.identity(d));
});
