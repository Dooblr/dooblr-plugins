// dooblr-titan engine: Standard MIDI File reader and writer (ES5, host independent).
//
// parse(bytes) takes an array of byte values (0..255) and returns
//   { format, tracks, division, name, timeSig: [num, den],
//     notes: [{ pitch, start, duration, velocity, channel }], lengthBeats }
// write(notes, opts) returns an array of byte values for a format-0 SMF.
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.midifile = (function () {
  var N = DT.notes;

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
        continue;
      }
      if (first === 0xf0 || first === 0xf7) { r.skip(r.vlq()); status = 0; continue; }
      if (first & 0x80) { status = first; data1 = r.u8(); }
      else {
        if (!status) { fail('The MIDI file uses running status before any status byte.'); }
        data1 = first;
      }
      var kind = status & 0xf0, ch = status & 0x0f;
      if (kind === 0xc0 || kind === 0xd0) { continue; }
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
    for (var k in open) {
      if (open.hasOwnProperty(k)) {
        for (var j = 0; j < open[k].length; j++) { open[k][j].endTick = tick; notes.push(open[k][j]); }
      }
    }
    return { notes: notes, name: name, timeSig: timeSig, endTick: tick };
  }

  function parse(bytes) {
    if (!bytes || bytes.length < 14) { fail('This file is too short to be a MIDI file.'); }
    var p = 0;
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
      out.push({ pitch: n.pitch, start: N.round4(s), duration: N.round4(e - s), velocity: n.velocity, channel: n.channel, mute: 0 });
    }
    N.sort(out);
    var ts = timeSig || [4, 4];
    var barBeats = ts[0] * 4 / ts[1];
    if (!(barBeats > 0)) { barBeats = 4; }
    var lastBeat = Math.max(maxEnd, Math.min(endTick / division, maxEnd + barBeats - 0.001));
    var lengthBeats = Math.max(barBeats, Math.ceil(lastBeat / barBeats - 1e-6) * barBeats);
    return { format: format, tracks: found, division: division, name: name, timeSig: ts,
      notes: out, lengthBeats: lengthBeats };
  }

  function vlqBytes(n) {
    var out = [n & 0x7f];
    n = Math.floor(n / 128);
    while (n > 0) { out.unshift((n & 0x7f) | 0x80); n = Math.floor(n / 128); }
    return out;
  }

  function pushAll(arr, more) {
    for (var i = 0; i < more.length; i++) { arr.push(more[i] & 0xff); }
  }

  function u32bytes(n) {
    return [(n >>> 24) & 0xff, (n >>> 16) & 0xff, (n >>> 8) & 0xff, n & 0xff];
  }

  // Format-0 SMF. Times in beats. opts: { division, name, lengthBeats }.
  function write(notes, opts) {
    opts = opts || {};
    var div = opts.division || 480;
    var src = N.sort(N.copyAll(notes));
    var evs = [], i;
    for (i = 0; i < src.length; i++) {
      var n = src[i];
      var onT = Math.round(n.start * div);
      var offT = Math.round((n.start + n.duration) * div);
      if (offT <= onT) { offT = onT + 1; }
      evs.push({ t: onT, on: 1, p: n.pitch, v: n.velocity });
      evs.push({ t: offT, on: 0, p: n.pitch, v: 0 });
    }
    evs.sort(function (a, b) { return a.t - b.t || a.on - b.on || a.p - b.p; });

    var trk = [];
    if (opts.name) {
      var nm = String(opts.name), chars = [];
      for (i = 0; i < nm.length; i++) {
        var c = nm.charCodeAt(i);
        if (c >= 32 && c < 127) { chars.push(c); }
      }
      pushAll(trk, [0, 0xff, 0x03]);
      pushAll(trk, vlqBytes(chars.length));
      pushAll(trk, chars);
    }
    pushAll(trk, [0, 0xff, 0x58, 4, 4, 2, 24, 8]);
    pushAll(trk, [0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20]);
    var last = 0;
    for (i = 0; i < evs.length; i++) {
      var e = evs[i];
      pushAll(trk, vlqBytes(e.t - last));
      last = e.t;
      trk.push(e.on ? 0x90 : 0x80, e.p, e.on ? e.v : 64);
    }
    var endTick = last;
    if (opts.lengthBeats) { endTick = Math.max(endTick, Math.round(opts.lengthBeats * div)); }
    pushAll(trk, vlqBytes(Math.max(0, endTick - last)));
    pushAll(trk, [0xff, 0x2f, 0]);

    var out = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, (div >> 8) & 0xff, div & 0xff];
    pushAll(out, [0x4d, 0x54, 0x72, 0x6b]);
    pushAll(out, u32bytes(trk.length));
    pushAll(out, trk);
    return out;
  }

  function baseName(path) {
    var s = String(path || '');
    s = s.replace(/^.*[\/\\:]/, '');
    return s.replace(/\.(midi?|smf|kar)$/i, '');
  }

  function isMidiPath(path) { return /\.(midi?|smf|kar)$/i.test(String(path || '')); }

  return { parse: parse, write: write, baseName: baseName, isMidiPath: isMidiPath };
})();
