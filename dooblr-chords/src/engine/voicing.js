// dooblr-chords engine: voicing and voice leading (ES5).
//
// Each chord gets an upper structure (close, drop-2 or open, chosen by Spread)
// plus an optional bass note. Among inversions and octaves, the voicing with
// the smallest movement from the previous chord wins, with a pull back toward
// the Register so progressions do not drift.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.voicing = (function () {
  var T = DC.theory, C = DC.chords;

  function spreadMode(spread) { return spread < 34 ? 0 : (spread < 67 ? 1 : 2); }

  // Choose which chord tones (intervals above the root) the upper voices play.
  function upperIntervals(chord, bassOn) {
    var iv = C.tones(chord), maxUpper = bassOn ? 4 : 5;
    var slashIv = C.bassInterval(chord);
    if (chord.slash) { iv = removeIv(iv, slashIv); } // the bass note carries the slash tone
    var dropOrder = [chord.fifth === 7 ? 7 : -1, (bassOn && !chord.slash) ? 0 : -1, 17, 18, 13, 15];
    for (var k = 0; k < dropOrder.length && iv.length > maxUpper; k++) {
      if (dropOrder[k] >= 0) { iv = removeIv(iv, dropOrder[k]); }
    }
    while (iv.length > maxUpper) { iv.pop(); }
    return iv;
  }

  function removeIv(list, v) {
    var out = [];
    for (var i = 0; i < list.length; i++) { if (list[i] !== v) { out.push(list[i]); } }
    return out;
  }

  function sortNum(a) { a.sort(function (x, y) { return x - y; }); return a; }

  // Stack pitch classes upward in close position starting at pitch p0.
  function stackClose(pcs, p0) {
    var out = [p0], last = p0;
    for (var i = 1; i < pcs.length; i++) {
      var p = last + T.mod12(pcs[i] - last);
      if (p === last) { p += 12; }
      out.push(p); last = p;
    }
    return out;
  }

  function applySpread(notes, mode) {
    var n = notes.slice(0), len = n.length;
    if (mode === 0 || len < 3) { return n; }
    if (mode === 1) { n[len - 2] -= 12; return sortNum(n); }
    if (len >= 4) { n[len - 2] -= 12; n[len - 4] -= 12; return sortNum(n); }
    n[1] += 12; // open triad: root, fifth, tenth
    return sortNum(n);
  }

  function nearestDistance(p, list) {
    var best = 999;
    for (var i = 0; i < list.length; i++) { var d = Math.abs(p - list[i]); if (d < best) { best = d; } }
    return best;
  }

  function cost(notes, prev, register, rotation, bass) {
    var low = notes[0], c = 0, i;
    if (bass !== null && low - bass < 5) { c += 20; } // keep the bass clear of the upper voices
    for (i = 1; i < notes.length; i++) { if (notes[i] - notes[i - 1] === 1 && i === 1) { c += 3; } }
    if (prev && prev.upper && prev.upper.length) {
      for (i = 0; i < notes.length; i++) { c += nearestDistance(notes[i], prev.upper); }
      for (i = 0; i < prev.upper.length; i++) { c += nearestDistance(prev.upper[i], notes); }
      c += 0.5 * Math.abs(notes[notes.length - 1] - prev.upper[prev.upper.length - 1]);
      c += 0.6 * Math.abs(low - register);
    } else {
      c += Math.abs(low - register) + (rotation === 0 ? 0 : 1.5);
    }
    return c;
  }

  // Shift a whole set of pitches by octaves until it fits in 0..127.
  function clampOctaves(list) {
    var lo = 999, hi = -999, i, shift = 0;
    for (i = 0; i < list.length; i++) { if (list[i] < lo) { lo = list[i]; } if (list[i] > hi) { hi = list[i]; } }
    while (hi + shift > 127) { shift -= 12; }
    while (lo + shift < 0) { shift += 12; }
    var out = [];
    for (i = 0; i < list.length; i++) { out.push(Math.max(0, Math.min(127, list[i] + shift))); }
    return out;
  }

  // opts: { register: MIDI note for the bottom of the upper voicing,
  //         spread: 0..100, bass: bool }
  // prev: the previous result of voice() or null.
  function voice(chord, keyRoot, opts, prev) {
    var register = Math.round(opts.register), mode = spreadMode(opts.spread);
    var bassOn = !!opts.bass;
    var rootPc = T.mod12(keyRoot + chord.root);
    var iv = upperIntervals(chord, bassOn);
    var pcs = [], i, r, k;
    for (i = 0; i < iv.length; i++) { pcs.push(T.mod12(rootPc + iv[i])); }

    var bassPcV = T.mod12(rootPc + C.bassInterval(chord));
    var bass = null;
    if (bassOn) {
      var target = (prev && prev.bass !== null && prev.bass !== undefined) ? prev.bass : register - 14;
      for (var p = register - 24; p < register - 6; p++) {
        if (T.mod12(p) === bassPcV && (bass === null || Math.abs(p - target) < Math.abs(bass - target))) { bass = p; }
      }
    }
    var best = null, bestCost = 1e9;
    for (r = 0; r < pcs.length; r++) {
      var rot = pcs.slice(r).concat(pcs.slice(0, r));
      // Candidate starting pitches for the lowest stacked note around the register.
      var first = register - 7 + T.mod12(rot[0] - (register - 7));
      for (k = -1; k <= 1; k++) {
        var cand = applySpread(stackClose(rot, first + 12 * k), mode);
        var cc = cost(cand, prev, register, r, bass);
        if (cc < bestCost - 1e-9) { bestCost = cc; best = cand; }
      }
    }
    var upper = best;
    if (bassOn) {
      while (bass >= upper[0] || (upper[0] - bass < 5 && bass - 12 >= register - 30)) { bass -= 12; }
    } else if (chord.slash) {
      bass = upper[0] - 1 - T.mod12(upper[0] - 1 - bassPcV);
    }
    var all = clampOctaves(bass === null ? upper : [bass].concat(upper));
    var outBass = bass === null ? null : all[0];
    var outUpper = bass === null ? all : all.slice(1);
    return { upper: outUpper, bass: outBass, notes: sortNum(all.slice(0)) };
  }

  function voiceAll(chords, keyRoot, opts) {
    var out = [], prev = null;
    for (var i = 0; i < chords.length; i++) {
      prev = voice(chords[i], keyRoot, opts, prev);
      out.push(prev);
    }
    return out;
  }

  return { voice: voice, voiceAll: voiceAll, spreadMode: spreadMode, upperIntervals: upperIntervals };
})();
