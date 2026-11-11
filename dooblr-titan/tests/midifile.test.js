'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { loadEngine } = require('./load-engine');
const { buildSmf, n } = require('./smf');

const DT = loadEngine();
const MF = DT.midifile;
const E = DT.engine;

test('midifile reads notes in beats, names, running status and note-on 0 offs', () => {
  const notes = [n(60, 0, 1, 100), n(64, 1, 0.5, 80)];
  const bytes = buildSmf(notes, { name: 'riff', runningStatus: true, zeroVelOff: true });
  const smf = MF.parse(bytes);
  assert.strictEqual(smf.name, 'riff');
  assert.strictEqual(smf.notes.length, 2);
  assert.strictEqual(smf.notes[0].pitch, 60);
  assert.ok(Math.abs(smf.notes[0].start - 0) < 1e-4);
  assert.ok(Math.abs(smf.notes[0].duration - 1) < 1e-3);
  assert.strictEqual(smf.notes[0].velocity, 100);
});

test('midifile write then parse round-trips pitches, times and velocities', () => {
  const notes = [n(36, 0, 0.25, 110), n(38, 0.5, 0.12, 70), n(60, 1, 1.5, 90)];
  const bytes = MF.write(notes, { name: 'loop', lengthBeats: 4, division: 480 });
  assert.strictEqual(String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]), 'MThd');
  const back = MF.parse(bytes);
  assert.strictEqual(back.name, 'loop');
  assert.strictEqual(back.notes.length, 3);
  back.notes.forEach((x, i) => {
    assert.strictEqual(x.pitch, notes[i].pitch);
    assert.ok(Math.abs(x.start - notes[i].start) < 1e-3, 'start ' + x.start);
    assert.ok(Math.abs(x.duration - notes[i].duration) < 1e-3, 'dur ' + x.duration);
    assert.strictEqual(x.velocity, notes[i].velocity);
  });
  assert.ok(back.lengthBeats >= 4);
});

test('midifile rejects files that are not SMF with readable errors', () => {
  assert.throws(() => MF.parse([1, 2, 3, 4]), /too short/);
  const wav = [0x52, 0x49, 0x46, 0x46, 0, 0, 0, 0, 0x57, 0x41, 0x56, 0x45];
  while (wav.length < 20) { wav.push(0); }
  assert.throws(() => MF.parse(wav), /not a Standard MIDI File/);
});

test('engine.sourceFromMidi loads a file and tighten never mutates it', () => {
  const notes = [n(60, 0.06, 1, 40), n(64, 1.06, 1, 120)];
  const src = E.sourceFromMidi(buildSmf(notes, { name: 'take' }), 'take');
  assert.strictEqual(src.name, 'take');
  assert.strictEqual(src.noteCount, 2);
  const before = src.notes[0].start;
  const t = E.tighten(src, E.DEFAULTS);
  assert.strictEqual(src.notes[0].start, before);
  assert.ok(t.notes.length >= 1);
  const bytes = E.midiBytes(t.notes, { name: t.clipName, lengthBeats: t.lengthBeats });
  const again = MF.parse(bytes);
  assert.strictEqual(again.notes.length, t.notes.length);
});

test('isMidiPath and baseName', () => {
  assert.ok(E.isMidiPath('Macintosh HD:/x/y.mid'));
  assert.ok(E.isMidiPath('C:\\clips\\a.midi'));
  assert.ok(!E.isMidiPath('loop.wav'));
  assert.strictEqual(E.baseName('Macintosh HD:/Users/dan/MIDI/my riff.mid'), 'my riff');
});
