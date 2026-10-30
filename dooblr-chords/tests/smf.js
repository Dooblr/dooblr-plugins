// Builds Standard MIDI File bytes for tests.
'use strict';

function vlq(n) {
  const out = [n & 0x7f];
  n = Math.floor(n / 128);
  while (n > 0) { out.unshift((n & 0x7f) | 0x80); n = Math.floor(n / 128); }
  return out;
}

// notes: [{ pitch, start, duration, velocity }] in beats.
// opts: { division, name, format, runningStatus, zeroVelOff, extraTrack }
function buildSmf(notes, opts) {
  opts = opts || {};
  const div = opts.division || 480;
  const evs = [];
  notes.forEach((n) => {
    evs.push({ t: Math.round(n.start * div), on: true, p: n.pitch, v: n.velocity || 100 });
    evs.push({ t: Math.round((n.start + n.duration) * div), on: false, p: n.pitch, v: 0 });
  });
  evs.sort((a, b) => a.t - b.t || (a.on === b.on ? 0 : a.on ? 1 : -1));
  const trk = [];
  if (opts.name) {
    const nm = Buffer.from(opts.name, 'latin1');
    trk.push(0, 0xff, 0x03, ...vlq(nm.length), ...nm);
  }
  trk.push(0, 0xff, 0x58, 4, 4, 2, 24, 8);                 // 4/4
  trk.push(0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20);             // tempo
  let last = 0, status = -1;
  evs.forEach((e) => {
    trk.push(...vlq(e.t - last)); last = e.t;
    const st = (e.on || opts.zeroVelOff) ? 0x90 : 0x80;
    if (!(opts.runningStatus && st === status)) { trk.push(st); }
    status = st;
    trk.push(e.p, e.on ? e.v : (opts.zeroVelOff ? 0 : 64));
  });
  if (opts.endTick !== undefined) { trk.push(...vlq(Math.max(0, opts.endTick - last))); } else { trk.push(0); }
  trk.push(0xff, 0x2f, 0);
  const tracks = opts.extraTrack ? [[0, 0xff, 0x2f, 0], trk] : [trk];
  const format = opts.format !== undefined ? opts.format : (tracks.length > 1 ? 1 : 0);
  const out = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, format, 0, tracks.length, (div >> 8) & 0xff, div & 0xff];
  tracks.forEach((t) => {
    const n = t.length;
    out.push(0x4d, 0x54, 0x72, 0x6b, (n >>> 24) & 0xff, (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff, ...t);
  });
  return out;
}

// Block chords, one per `beats`: [[pitches], ...]
function chordNotes(chords, beats) {
  const notes = [];
  chords.forEach((ps, i) => ps.forEach((p) => notes.push({ pitch: p, start: i * beats, duration: beats, velocity: 90 })));
  return notes;
}

module.exports = { buildSmf, chordNotes, vlq };
