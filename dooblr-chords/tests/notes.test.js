'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { loadEngine } = require('./load-engine');
const { buildSmf, chordNotes } = require('./smf');
const DC = loadEngine();
const C = DC.chords, E = DC.engine;

function soundingAt(notes, t) {
  return DC.plain(notes).filter((n) => n.start_time <= t + 1e-9 && n.start_time + n.duration > t + 1e-9);
}

function clusterOnsets(clip) {
  const starts = [...new Set(DC.plain(clip).map((n) => n.start_time))].sort((a, b) => a - b);
  const clusters = [];
  starts.forEach((s) => {
    const last = clusters[clusters.length - 1];
    if (!last || s - last.rep > 0.12) { clusters.push({ rep: s, times: [s] }); }
    else { last.times.push(s); }
  });
  return clusters.map((c) => Math.min(...c.times));
}

function assertClipVoices(b, n, label) {
  const clip = DC.plain(b.notes);
  const onsets = clusterOnsets(clip);
  assert.equal(onsets.length, b.chords.length, `${label} expected ${b.chords.length} chord onsets, got ${onsets.length}: ${onsets}`);
  const ends = onsets.slice(1).concat([b.lengthBeats]);
  onsets.forEach((t0, i) => {
    const t1 = ends[i];
    const group = clip.filter((x) => x.start_time >= t0 - 1e-9 && x.start_time < t1 - 1e-6);
    const groupPitches = group.map((x) => x.pitch);
    assert.equal(group.length, n, `${label} slot ${i} wrote ${group.length} notes pitches=${groupPitches} ${b.display.map((d) => d.name).join(' ')}`);
    assert.equal(new Set(groupPitches).size, n, `${label} duplicate pitch written in slot ${i}: ${groupPitches}`);
    // Query once every voice of this chord has attacked (humanize may delay a start slightly).
    const tQuery = Math.max(...group.map((x) => x.start_time));
    const sounding = soundingAt(clip, tQuery);
    const pitches = sounding.map((x) => x.pitch);
    assert.equal(sounding.length, n, `${label} at ${tQuery} (slot ${t0}) count=${sounding.length} pitches=${pitches} ${b.display.map((d) => d.name).join(' ')}`);
    assert.equal(new Set(pitches).size, n, `${label} duplicate sounding pitch at ${tQuery}: ${pitches}`);
    group.forEach((note) => {
      assert.ok(note.start_time + note.duration <= t1 + 1e-6,
        `${label} note ${note.pitch} at ${note.start_time} dur ${note.duration} overlaps next onset ${t1}`);
    });
  });
}

test('default Notes is Triad (3), and 0.6.0 Auto/2 values fall back to a triad', () => {
  assert.equal(E.VERSION, '0.6.2');
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

test('each Notes setting writes exactly N unique MIDI notes, including the bass', () => {
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
                const pitches = DC.plain(v.notes);
                assert.equal(pitches.length, notes, `${b.display[i].name} notes=${notes} bass=${bass} seed=${seed} pitches=${pitches}`);
                assert.equal(new Set(pitches).size, notes, 'no duplicate pitches');
                if (bass || ch.slash) {
                  assert.equal(DC.plain(v.upper).length, notes - 1, 'bass is one of the N');
                  assert.ok(v.bass !== null && v.bass !== undefined);
                } else {
                  assert.equal(DC.plain(v.upper).length, notes);
                }
                const allPcs = pitches.map((p) => p % 12);
                const rootPc = (b.settings.root + ch.root) % 12;
                const fifthPc = (rootPc + ch.fifth) % 12;
                assert.ok(allPcs.includes(fifthPc), 'fifth is in the voicing: ' + b.display[i].name);
              });
            }
          }
        }
      }
    }
  }
});

test('the clip note list has exactly N unique sounding voices at each chord onset', () => {
  const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
  const an = E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff');
  const seeds = [1, 7, 42, 99, 48213, 8888];
  const combos = [];
  for (const notes of [3, 4, 5, 6]) {
    for (const bass of [0, 1]) {
      for (const color of [0, 35, 100]) {
        for (const chromatic of [0, 80]) {
          for (const spread of [0, 25, 100]) {
            for (const register of [40, 60, 72]) {
              for (const humanize of [0, 80]) {
                for (const duration of [0, 1, 2]) {
                  for (const mode of [0, 1, 6]) {
                    for (const seed of seeds) {
                      combos.push({ notes, bass, color, chromatic, spread, register, humanize, duration, mode, seed });
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  const selected = combos.filter((c, i) => i % 17 === 0 || (c.notes === 3 && c.bass === 1 && c.humanize === 80 && c.spread === 100));
  for (const c of selected) {
    const b = E.build({
      root: c.seed % 12, mode: c.mode, length: 6, color: c.color, chromatic: c.chromatic,
      register: c.register, spread: c.spread, bass: c.bass, velocity: 100,
      humanize: c.humanize, seed: c.seed, notes: c.notes, duration: c.duration
    });
    assertClipVoices(b, c.notes, `gen notes=${c.notes} bass=${c.bass} hum=${c.humanize} spr=${c.spread} seed=${c.seed}`);
  }
  for (const notes of [3, 4, 5, 6]) {
    for (const bass of [0, 1]) {
      for (const kind of [0, 1]) {
        for (const humanize of [0, 80]) {
          const b = E.build({
            root: 9, mode: 1, length: 4, color: 35, chromatic: 10, bass, notes, kind,
            register: 60, spread: 50, humanize, seed: 42, duration: 1
          }, null, { analysis: an, keep: {} });
          assertClipVoices(b, notes, `src kind=${kind} notes=${notes} bass=${bass} hum=${humanize} rhythm=${b.rhythm}`);
        }
      }
    }
  }
});

test('Color and Spice do not change the Notes count', () => {
  for (const color of [0, 35, 100]) {
    for (const chromatic of [0, 100]) {
      const b = E.build({ root: 0, mode: 0, length: 8, color, chromatic, bass: 1, seed: 42, notes: 3 });
      b.voicings.forEach((v) => assert.equal(DC.plain(v.notes).length, 3));
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

test('Notes forces VAR/B derived chords to N sounding voices', () => {
  const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
  const an = E.sourceFromMidi(buildSmf(chordNotes(AM_F_C_G, 4)), 'riff');
  const src = { analysis: an, keep: {} };
  for (const notes of [3, 4, 5, 6]) {
    const b = E.build({ root: 9, mode: 1, length: 4, color: 35, chromatic: 10, bass: 1, seed: 1, notes, kind: 0 }, null, src);
    assert.ok(b.clipName.includes(E.notesLabel(notes)), b.clipName);
    b.voicings.forEach((v) => {
      assert.equal(DC.plain(v.notes).length, notes);
      assert.equal(DC.plain(v.upper).length, notes - 1);
    });
    assertClipVoices(b, notes, `var notes=${notes}`);
  }
});
