// dooblr-chords harmony bus protocol (ES5, host independent).
// Global Max send/receive name: dooblr_chords_bus (not ---local; those
// names stay inside one device). Channel 0..3 = A..D so several senders
// can share a Live set. Messages are a single JSON string:
//   { type: "harmony", v, ch, beat, root, mode, scale, chordRoot, chord,
//     bass, bassMidi, index, name, lengthBeats }
//   { type: "hello", v, ch }   receiver asks the matching sender to send now
//   { type: "bye",   v, ch }   optional; sender going away
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.protocol = (function () {
  var BUS = 'dooblr_chords_bus';
  var VERSION = 1;
  var CHANNELS = ['A', 'B', 'C', 'D'];

  function clampChan(n) {
    n = Math.round(+n || 0);
    if (n < 0) { return 0; }
    if (n > 3) { return 3; }
    return n;
  }

  function scalePcs(root, mode) {
    var modes = DC.theory.MODES;
    var m = modes[mode] ? mode : 0;
    var iv = modes[m].intervals, out = [], i;
    for (i = 0; i < iv.length; i++) { out.push(DC.theory.mod12(root + iv[i])); }
    return out;
  }

  function chordPcs(chord) {
    var tones = DC.chords.tones(chord), out = [], seen = {}, i, pc;
    for (i = 0; i < tones.length; i++) {
      pc = DC.theory.mod12(chord.root + tones[i]);
      if (!seen[pc]) { seen[pc] = 1; out.push(pc); }
    }
    return out;
  }

  function indexAt(build, beats) {
    if (DC.scheduler && DC.scheduler.chordAt) { return DC.scheduler.chordAt(build, beats); }
    if (!build || !build.slots || !build.slots.length || !(build.lengthBeats > 0)) { return 0; }
    var len = build.lengthBeats, t = beats % len, i, last = 0;
    if (t < 0) { t += len; }
    for (i = 0; i < build.slots.length; i++) {
      if (build.slots[i].start - 1e-6 <= t) { last = i; }
    }
    return last;
  }

  // Snapshot the harmony of `build` at a song position. When the transport
  // is stopped, index 0 of the last generated progression is advertised so
  // receivers still have a chord. When playing, the index follows song
  // position around the loop (CLIP and LIVE both use this).
  function fromBuild(build, opts) {
    opts = opts || {};
    if (!build || !build.chords || !build.chords.length) { return null; }
    var beats = typeof opts.beats === 'number' ? opts.beats : 0;
    var idx;
    if (typeof opts.index === 'number') { idx = opts.index; }
    else if (opts.playing) { idx = indexAt(build, beats); }
    else { idx = 0; }
    if (idx < 0) { idx = 0; }
    if (idx >= build.chords.length) { idx = build.chords.length - 1; }
    return payload(build, idx, beats, opts.channel || 0);
  }

  function payload(build, index, beats, channel) {
    if (!build || !build.chords || !build.chords[index]) { return null; }
    var ch = build.chords[index];
    var o = build.settings || {};
    var bassIv = DC.chords.bassInterval(ch);
    var bassPc = DC.theory.mod12(ch.root + bassIv);
    var bassMidi = null;
    if (build.voicings && build.voicings[index] && build.voicings[index].bass !== null && build.voicings[index].bass !== undefined) {
      bassMidi = build.voicings[index].bass;
    }
    var name = (build.display && build.display[index] && build.display[index].name)
      ? build.display[index].name
      : DC.chords.name(ch, o.root, o.mode);
    return {
      type: 'harmony',
      v: VERSION,
      ch: clampChan(channel),
      beat: beats,
      root: DC.theory.mod12(o.root || 0),
      mode: o.mode || 0,
      scale: scalePcs(o.root || 0, o.mode || 0),
      chordRoot: DC.theory.mod12(ch.root),
      chord: chordPcs(ch),
      bass: bassPc,
      bassMidi: bassMidi,
      index: index,
      name: name,
      lengthBeats: build.lengthBeats || 0
    };
  }

  function encode(msg) {
    if (!msg) { return ''; }
    if (msg.type === 'hello' || msg.type === 'bye') {
      return JSON.stringify({ type: msg.type, v: VERSION, ch: clampChan(msg.ch) });
    }
    var p = msg.payload ? msg.payload : msg;
    if (!p.type) { p.type = 'harmony'; }
    p.v = VERSION;
    p.ch = clampChan(p.ch);
    return JSON.stringify(p);
  }

  function decode(s) {
    if (s && typeof s === 'object' && !s.charAt) { return s.type ? s : null; }
    try {
      var o = JSON.parse(String(s));
      if (!o || !o.type) { return null; }
      if (o.ch !== undefined) { o.ch = clampChan(o.ch); }
      return o;
    } catch (e) {
      return null;
    }
  }

  // A compact identity string so the host can skip unchanged broadcasts
  // (beat is ignored; receivers only need to know when the harmony itself
  // changes).
  function identity(p) {
    if (!p) { return ''; }
    return [p.ch, p.root, p.mode, p.chordRoot, (p.chord || []).join('.'), p.bass, p.index, p.name].join('|');
  }

  return {
    BUS: BUS,
    VERSION: VERSION,
    CHANNELS: CHANNELS,
    clampChan: clampChan,
    scalePcs: scalePcs,
    chordPcs: chordPcs,
    indexAt: indexAt,
    fromBuild: fromBuild,
    payload: payload,
    encode: encode,
    decode: decode,
    identity: identity
  };
})();
