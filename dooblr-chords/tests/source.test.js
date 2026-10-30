'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const { buildSmf, chordNotes } = require('./smf');
const DC = loadEngine();
const T = DC.theory, C = DC.chords, E = DC.engine, A = DC.analysis, MF = DC.midifile;

// Am  F  C  G  (one bar each), with a bass note under each chord.
const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
// Cmaj7 Am7 Dm7 G7
const JAZZ = [[48, 52, 55, 59], [45, 52, 55, 60], [50, 53, 57, 60], [43, 53, 59, 62]];

function pcsOf(chord, key) { return DC.plain(C.tones(chord)).map((t) => (key + chord.root + t) % 12).sort((a, b) => a - b); }

// ---- midifile -------------------------------------------------------------------

test('midifile reads notes in beats, names, running status and note-on 0 offs', () => {
  const bytes = buildSmf(chordNotes(AM_F_C_G, 4), { name: 'Verse', runningStatus: true, zeroVelOff: true });
  const smf = MF.parse(bytes);
  assert.equal(smf.division, 480);
  assert.equal(smf.name, 'Verse');
  assert.equal(smf.notes.length, 16);
  assert.equal(smf.lengthBeats, 16);
  const first = DC.plain(smf.notes).filter((n) => n.start === 0).map((n) => n.pitch).sort((a, b) => a - b);
  assert.deepEqual(first, [45, 57, 60, 64]);
  smf.notes.forEach((n) => assert.ok(n.duration > 3.99 && n.duration < 4.01));
});

test('midifile reads format 1, other divisions and rounds the length up to whole bars', () => {
  const smf = MF.parse(buildSmf([{ pitch: 60, start: 0, duration: 1 }, { pitch: 64, start: 4.5, duration: 1 }], { division: 96, extraTrack: true }));
  assert.equal(smf.format, 1);
  assert.equal(smf.tracks, 2);
  assert.equal(smf.notes.length, 2);
  assert.equal(smf.notes[1].start, 4.5);
  assert.equal(smf.lengthBeats, 8);
});

test('midifile rejects files that are not SMF with readable errors', () => {
  assert.throws(() => MF.parse([1, 2, 3, 4, 5]), /MIDI/);
  const smpte = buildSmf([{ pitch: 60, start: 0, duration: 1 }]);
  smpte[12] = 0xe7; smpte[13] = 40;
  assert.throws(() => MF.parse(smpte), /SMPTE/i);
  const cut = buildSmf(chordNotes(AM_F_C_G, 4)).slice(0, 40);
  assert.throws(() => MF.parse(cut), /MIDI file/);
  assert.equal(MF.isMidiPath('Macintosh HD:/Users/d/riff.MID'), true);
  assert.equal(MF.isMidiPath('/x/y.midi'), true);
  assert.equal(MF.isMidiPath('/x/y.wav'), false);
  assert.equal(MF.baseName('Macintosh HD:/Users/d/My Riff.mid'), 'My Riff');
});

// ---- analysis -------------------------------------------------------------------

test('analysis finds the chords and key of a block-chord clip', () => {
  const an = E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff');
  assert.equal(an.name, 'riff');
  assert.equal(an.segments.length, 4);
  assert.deepEqual(DC.plain(an.segments).map((s) => [s.start, s.duration]), [[0, 4], [4, 4], [8, 4], [12, 4]]);
  assert.equal(an.key.root, 9, 'A');
  assert.equal(T.MODES[an.key.mode].family, 'minor');
  assert.deepEqual(DC.plain(A.symbols(an, an.key.root, an.key.mode)), ['Am', 'F', 'C', 'G']);
  const jazz = E.analyzeSource(DC.plain(chordNotes(JAZZ, 4)).map((n) => ({ pitch: n.pitch, start_time: n.start, duration: n.duration, velocity: 90 })), { lengthBeats: 16 });
  assert.deepEqual(DC.plain(A.symbols(jazz, jazz.key.root, jazz.key.mode)), ['Cmaj7', 'Am7', 'Dm7', 'G7']);
  assert.equal(jazz.key.root, 0);
  assert.equal(jazz.key.mode, 0);
});

test('analysis merges repeated chords, ignores muted notes and reports empty clips', () => {
  const notes = chordNotes([[57, 60, 64], [57, 60, 64], [53, 57, 60], [53, 57, 60]], 2);
  notes.push({ pitch: 61, start: 0, duration: 8, velocity: 90, mute: 1 });
  const an = E.analyzeSource(notes.map((n) => ({ pitch: n.pitch, start: n.start, duration: n.duration, velocity: 90, mute: n.mute || 0 })), { lengthBeats: 8 });
  assert.equal(an.segments.length, 2);
  assert.equal(an.segments[0].duration, 4);
  assert.throws(() => E.analyzeSource([], { lengthBeats: 4 }), /no notes/);
  assert.throws(() => E.analyzeSource([{ pitch: 60, start: 0, duration: 4, velocity: 90, mute: 1 }], { lengthBeats: 4 }), /no notes|No chords/);
});

test('analysis round-trips generated progressions (pitch classes match)', () => {
  let same = 0, total = 0;
  for (let seed = 1; seed <= 60; seed++) {
    const b = E.build({ seed, mode: seed % 2, root: seed % 12, color: 30, chromatic: 0, length: 4 });
    const an = E.analyzeSource(DC.plain(b.notes), { lengthBeats: b.lengthBeats });
    const got = DC.plain(A.toChords(an, b.settings.root, b.settings.mode));
    total += 4;
    for (let i = 0; i < 4 && i < got.length; i++) {
      // Compare triad pitch classes (root, third, fifth): extensions may be dropped by voicing.
      const want = new Set(pcsOf(b.chords[i], b.settings.root));
      const tri = [0, got[i].third, got[i].fifth].filter((x) => x !== null && x !== undefined).map((t) => (b.settings.root + got[i].root + t) % 12);
      if (tri.every((p) => want.has(p))) { same++; }
    }
  }
  assert.ok(same / total >= 0.9, 'triads recovered ' + same + '/' + total);
});

// ---- derive ---------------------------------------------------------------------

function source() { return E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff'); }
function settingsFor(an, extra) { return Object.assign({ root: an.key.root, mode: an.key.mode, length: an.segments.length, color: 35, chromatic: 30, seed: 11 }, extra || {}); }

test('VAR keeps the key, length, rhythm, first and last chords, and is deterministic', () => {
  const an = source();
  let anyChange = false;
  for (let seed = 1; seed <= 40; seed++) {
    const s = settingsFor(an, { seed, kind: 0 });
    const a = E.build(s, [], { analysis: an });
    const b = E.build(s, [], { analysis: an });
    assert.deepEqual(DC.plain(a.notes), DC.plain(b.notes), 'deterministic');
    assert.equal(a.chords.length, 4);
    assert.equal(a.lengthBeats, 16);
    assert.deepEqual(DC.plain(a.display).map((d) => d.beats), [4, 4, 4, 4]);
    assert.equal(a.display[0].changed, false);
    assert.equal(a.chords[0].root, 0, 'first chord is the tonic');
    assert.ok(a.clipName.indexOf('riff \u00B7 VAR \u00B7 A min') === 0, a.clipName);
    assert.equal(a.derived.kind, 'var');
    assert.deepEqual(DC.plain(a.derived.source.chords), ['Am', 'F', 'C', 'G']);
    if (a.display.some((d) => d.changed)) { anyChange = true; }
    a.notes.forEach((n) => assert.ok(n.start_time >= 0 && n.start_time + n.duration <= 16 + 1e-9));
  }
  assert.ok(anyChange, 'some variation changes a chord');
});

test('VAR with Chromatic 0 stays in the key; other lengths use uniform slots', () => {
  const an = source();
  for (let seed = 1; seed <= 40; seed++) {
    const a = E.build(settingsFor(an, { seed, chromatic: 0, kind: 0 }), [], { analysis: an });
    a.chords.forEach((c) => assert.equal(c.source, 'diatonic', 'seed ' + seed + ' ' + a.display.map((d) => d.name).join(' ')));
  }
  const six = E.build(settingsFor(an, { length: 6, duration: 0, kind: 0 }), [], { analysis: an });
  assert.equal(six.chords.length, 6);
  assert.equal(six.lengthBeats, 12);
});

test('VAR keeps locked chords', () => {
  const an = source();
  const s = settingsFor(an, { kind: 0, chromatic: 80 });
  const first = E.build(s, [], { analysis: an });
  const locks = [null, DC.chords.copy(first.chords[1]), null, null];
  for (let seed = 1; seed <= 20; seed++) {
    const b = E.build(Object.assign({}, s, { seed }), locks, { analysis: an });
    assert.equal(b.display[1].name, first.display[1].name);
    assert.ok(b.display[1].locked);
  }
});

test('B section moves away, ends on a chord that leads home, and fills the clip', () => {
  const an = source();
  const rhythms = {}, regions = {};
  for (let seed = 1; seed <= 60; seed++) {
    const b = E.build(settingsFor(an, { seed, kind: 1 }), [], { analysis: an });
    rhythms[b.derived.rhythm] = 1;
    regions[b.derived.region] = 1;
    const total = b.display.reduce((x, d) => x + d.beats, 0);
    assert.equal(total, b.lengthBeats);
    assert.ok(b.chords.length >= 2 && b.chords.length <= 16);
    assert.notEqual(b.chords[0].root === 0 && b.chords[0].third === 3 ? 'tonic' : 'away', 'tonic', 'B does not start on the home tonic');
    const last = b.chords[b.chords.length - 1];
    assert.ok(['D', 'PD'].indexOf(last.func) >= 0 || last.root !== 0, 'B ends away from the tonic: ' + b.display.map((d) => d.name).join(' '));
    assert.ok(/^riff \u00B7 B /.test(b.clipName), b.clipName);
    assert.match(b.derived.label, /^B section/);
  }
  assert.ok(Object.keys(rhythms).length >= 3, JSON.stringify(rhythms));
  assert.ok(Object.keys(regions).length >= 2, JSON.stringify(regions));
});

test('B section keeps the rhythm plan when asked (locks during Reroll)', () => {
  const an = source();
  const a = E.build(settingsFor(an, { seed: 5, kind: 1 }), [], { analysis: an });
  for (let seed = 6; seed < 30; seed++) {
    const b = E.build(settingsFor(an, { seed, kind: 1 }), [C.copy(a.chords[0])], { analysis: an, keep: { rhythm: a.rhythm } });
    assert.equal(b.rhythm, a.rhythm);
    assert.equal(b.chords.length, a.chords.length);
    assert.equal(b.display[0].name, a.display[0].name);
  }
});

test('engine without a source is unchanged by the kind setting', () => {
  const a = E.build({ seed: 9 });
  const b = E.build({ seed: 9, kind: 1 });
  assert.deepEqual(DC.plain(a.notes), DC.plain(b.notes));
  assert.equal(a.derived, null);
  assert.equal(a.clipName, 'C min \u00B7 4 \u00B7 Col35 Chr10');
});
