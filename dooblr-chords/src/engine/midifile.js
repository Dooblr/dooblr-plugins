// dooblr-chords engine: Standard MIDI File reader (ES5, host independent).
//
// parse(bytes) takes an array of byte values (0..255) and returns
//   { format, tracks, division, name, timeSig: [num, den],
//     notes: [{ pitch, start, duration, velocity, channel }], lengthBeats }
// Times are in beats (quarter notes): ticks / division. Tempo changes are
// ignored on purpose, because a clip is laid out in beats, not seconds.
// Supports format 0 and 1 (format 2 is read as if it were 1), running status,
// meta and sysex events, note-on with velocity 0 as note-off, and overlapping
// notes on the same pitch (first in, first out). Throws an Error with a
// readable message when the file is not a usable SMF.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.midifile = (function () {
  function fail(msg) { throw new Error(msg); }

  function Reader(bytes, start, end) {
    this.b = bytes; this.p = start; this.end = end;
  }
  Reader.prototype.left = function () { return this.end - this.p; };
  Reader.prototype.u8 = function () {
    if (this.p >= this.end) { fail('The MIDI file ends in the middle of an event.'); }
    return this.b[this.p++] & 0xff;
  };
  Reader.prototype.vlq = function () {
    var v = 0, c, n = 0;
    do {
      c = this.u8();
      v = v * 128 + (c & 0x7f);
      if (++n > 4) { fail('The MIDI file has a malformed variable-length number.'); }
    } while (c & 0x80);
    return v;
  };
  Reader.prototype.skip = function (n) {
    if (n > this.left()) { fail('The MIDI file ends in the middle of an event.'); }
    this.p += n;
  };

  function u32(b, p) { return ((b[p] & 0xff) * 16777216) + ((b[p + 1] & 0xff) << 16) + ((b[p + 2] & 0xff) << 8) + (b[p + 3] & 0xff); }
  function u16(b, p) { return ((b[p] & 0xff) << 8) + (b[p + 1] & 0xff); }
  function tag(b, p) { return String.fromCharCode(b[p] & 0xff, b[p + 1] & 0xff, b[p + 2] & 0xff, b[p + 3] & 0xff); }

  function text(b, p, n) {
    var s = '';
    for (var i = 0; i < n; i++) {
      var c = b[p + i] & 0xff;
      s += (c >= 32 && c < 127) ? String.fromCharCode(c) : (c >= 160 ? String.fromCharCode(c) : '');
    }
    return s.replace(/^\s+|\s+$/g, '');
  }

  // Read one MTrk chunk into note events (ticks). Returns { notes, name, timeSig, endTick }.
  function readTrack(bytes, start, end) {
    var r = new Reader(bytes, start, end);
    var tick = 0, status = 0, notes = [], open = {}, name = '', timeSig = null;
    while (r.left() > 0) {
      tick += r.vlq();
      var first = r.u8(), data1;
      if (first === 0xff) {
        var type = r.u8(), len = r.vlq(), at = r.p;
        r.skip(len);
        if (type === 0x03 && !name) { name = text(bytes, at, len); }
        if (type === 0x58 && len >= 2 && !timeSig) { timeSig = [bytes[at] & 0xff, Math.pow(2, bytes[at + 1] & 0xff)]; }
        if (type === 0x2f) { break; }
        continue; // meta events do not change running status in practice
      }
      if (first === 0xf0 || first === 0xf7) { r.skip(r.vlq()); status = 0; continue; }
      if (first & 0x80) { status = first; data1 = r.u8(); }
      else {
        if (!status) { fail('The MIDI file uses running status before any status byte.'); }
        data1 = first;
      }
      var kind = status & 0xf0, ch = status & 0x0f;
      if (kind === 0xc0 || kind === 0xd0) { continue; } // one data byte
      if (kind < 0x80 || kind > 0xe0) { fail('The MIDI file has an unknown event.'); }
      var data2 = r.u8();
      if (kind === 0x90 && data2 > 0) {
        var key = ch * 128 + data1;
        if (!open[key]) { open[key] = []; }
        open[key].push({ pitch: data1, startTick: tick, velocity: data2, channel: ch });
      } else if (kind === 0x80 || kind === 0x90) {
        var q = open[ch * 128 + data1];
        if (q && q.length) {
          var n = q.shift();
          n.endTick = tick;
          notes.push(n);
        }
      }
    }
    // Notes still held at the end of the track end there.
    for (var k in open) {
      if (open.hasOwnProperty(k)) {
        for (var j = 0; j < open[k].length; j++) { open[k][j].endTick = tick; notes.push(open[k][j]); }
      }
    }
    return { notes: notes, name: name, timeSig: timeSig, endTick: tick };
  }

  function round4(x) { return Math.round(x * 10000) / 10000; }

  function parse(bytes) {
    if (!bytes || bytes.length < 14) { fail('This file is too short to be a MIDI file.'); }
    var p = 0;
    // Some files carry a RIFF RMID wrapper; look for MThd in the first 64 bytes.
    while (p + 4 <= bytes.length && p < 64 && tag(bytes, p) !== 'MThd') { p++; }
    if (tag(bytes, p) !== 'MThd') { fail('This is not a Standard MIDI File (no MThd header).'); }
    var hlen = u32(bytes, p + 4);
    if (hlen < 6) { fail('The MIDI header is too short.'); }
    var format = u16(bytes, p + 8), ntracks = u16(bytes, p + 10), division = u16(bytes, p + 12);
    if (division & 0x8000) { fail('This MIDI file uses SMPTE time, which has no beats. Export it with bars and beats.'); }
    if (division === 0) { fail('The MIDI header has a zero time division.'); }
    p += 8 + hlen;

    var notes = [], name = '', timeSig = null, endTick = 0, found = 0;
    while (p + 8 <= bytes.length) {
      var t = tag(bytes, p), len = u32(bytes, p + 4), start = p + 8, end = Math.min(bytes.length, start + len);
      if (t === 'MTrk') {
        var tr = readTrack(bytes, start, end);
        found++;
        for (var i = 0; i < tr.notes.length; i++) { notes.push(tr.notes[i]); }
        if (!name && tr.name) { name = tr.name; }
        if (!timeSig && tr.timeSig) { timeSig = tr.timeSig; }
        if (tr.endTick > endTick) { endTick = tr.endTick; }
      }
      p = start + len;
    }
    if (!found) { fail('The MIDI file has no tracks.'); }

    var out = [], maxEnd = 0;
    for (var k = 0; k < notes.length; k++) {
      var n = notes[k];
      var s = n.startTick / division, e = n.endTick / division;
      if (e <= s) { e = s + 0.0625; }
      if (e > maxEnd) { maxEnd = e; }
      out.push({ pitch: n.pitch, start: round4(s), duration: round4(e - s), velocity: n.velocity, channel: n.channel });
    }
    out.sort(function (a, b) { return a.start - b.start || a.pitch - b.pitch; });
    var ts = timeSig || [4, 4];
    var barBeats = ts[0] * 4 / ts[1];
    if (!(barBeats > 0)) { barBeats = 4; }
    // Clip length: whole bars covering the notes. The end-of-track time is only
    // trusted when it lands within a bar of the last note (some writers pad).
    var lastBeat = Math.max(maxEnd, Math.min(endTick / division, maxEnd + barBeats - 0.001));
    var lengthBeats = Math.max(barBeats, Math.ceil(lastBeat / barBeats - 1e-6) * barBeats);
    return { format: format, tracks: found, division: division, name: name, timeSig: ts,
      notes: out, lengthBeats: lengthBeats };
  }

  // Name to show for a file path: the file name without folders or extension.
  function baseName(path) {
    var s = String(path || '');
    s = s.replace(/^.*[\/\\:]/, '');
    return s.replace(/\.(midi?|smf|kar)$/i, '');
  }

  function isMidiPath(path) { return /\.(midi?|smf|kar)$/i.test(String(path || '')); }

  return { parse: parse, baseName: baseName, isMidiPath: isMidiPath };
})();
