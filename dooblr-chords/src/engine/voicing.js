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
  function upperIntervals(chord, bassOn, maxUpperOpt) {
    var iv = C.tones(chord), maxUpper = bassOn ? 4 : 5;
    if (maxUpperOpt >= 3 && maxUpperOpt < maxUpper) { maxUpper = Math.round(maxUpperOpt); }
    var slashIv = C.bassInterval(chord);
    if (chord.slash) { iv = removeIv(iv, slashIv); } // the bass note carries the slash tone
    var dropOrder = [chord.fifth === 7 ? 7 : -1, (bassOn && !chord.slash) ? 0 : -1, 17, 18, 13, 15];
    for (var k = 0; k < dropOrder.length && iv.length > maxUpper; k++) {
      if (dropOrder[k] >= 0) { iv = removeIv(iv, dropOrder[k]); }
    }
    while (iv.length > maxUpper) { iv.pop(); }
    return iv;
  }

  // Explicit Notes (2..6): upper-voice count. The Bass note (and a slash bass)
  // is extra and not counted. Drop least essential first: extensions, then the
  // fifth, then the seventh; always keep the root and third (or sus). If more
  // notes are needed, add 7, 9, 11 and 13 from the chord scale (Color style),
  // then octave doublings.
  function hasIvOrPc(list, v) {
    var pc = T.mod12(v), i;
    for (i = 0; i < list.length; i++) { if (list[i] === v || T.mod12(list[i]) === pc) { return true; } }
    return false;
  }

  function pickUpper(chord, opts) {
    var bassOn = !!opts.bass;
    var target = Math.round(opts.notes || 0);
    if (!(target >= 2 && target <= 6)) {
      return { iv: upperIntervals(chord, bassOn, opts.maxUpper), doublings: 0, named: false };
    }
    var iv = C.tones(chord), slashIv = C.bassInterval(chord);
    if (chord.slash) { iv = removeIv(iv, slashIv); }
    var dropOrder = [];
    if (chord.thirteen !== null) { dropOrder.push(chord.thirteen); }
    if (chord.eleven !== null) { dropOrder.push(chord.eleven); }
    if (chord.nine !== null) { dropOrder.push(chord.nine); }
    if (chord.fifth !== null) { dropOrder.push(chord.fifth); }
    if (chord.seventh !== null) { dropOrder.push(chord.seventh); }
    var k;
    for (k = 0; k < dropOrder.length && iv.length > target; k++) {
      iv = removeIv(iv, dropOrder[k]);
    }
    while (iv.length > target) {
      var last = iv[iv.length - 1], keep = last === 0 || last === chord.third ||
        (chord.sus && last === (chord.sus === 4 ? 5 : 2));
      if (keep) { break; }
      iv.pop();
    }
    if (iv.length < target) {
      var extra = neededExtensions(chord, iv, target - iv.length);
      iv = iv.concat(extra);
    }
    return { iv: sortNum(iv.slice(0)), doublings: Math.max(0, target - iv.length), named: true };
  }

  function neededExtensions(chord, have, n) {
    var c = C.copy(chord), add = [], s = c.scale;
    function commit(field, v) {
      if (n <= 0 || v === null || v === undefined) { return; }
      if (hasIvOrPc(have, v) || hasIvOrPc(add, v)) { return; }
      if (field !== 'seventh' && C.avoided(c, v)) { return; }
      c[field] = v;
      add.push(v);
      n--;
    }
    if (s && c.seventh === null) { commit('seventh', s[6]); }
    if (s && c.nine === null) { commit('nine', s[1] + 12); }
    if (s && c.eleven === null) { commit('eleven', s[3] + 12); }
    if (s && c.thirteen === null) { commit('thirteen', s[5] + 12); }
    return add;
  }

  // Rebuild a naming copy of the chord from the intervals actually voiced,
  // so the blocks reflect added or dropped extensions.
  function chordForDisplay(src, iv) {
    var c = C.copy(src);
    function has(v) {
      var i;
      for (i = 0; i < iv.length; i++) { if (iv[i] === v) { return true; } }
      return false;
    }
    if (c.seventh !== null && !has(c.seventh)) { c.seventh = null; }
    if (c.nine !== null && !has(c.nine)) { c.nine = null; c.add9 = false; }
    if (c.eleven !== null && !has(c.eleven)) { c.eleven = null; }
    if (c.thirteen !== null && !has(c.thirteen)) { c.thirteen = null; }
    if (c.seventh === null) {
      if (has(11)) { c.seventh = 11; } else if (has(10)) { c.seventh = 10; } else if (has(9)) { c.seventh = 9; }
    }
    if (c.nine === null) {
      if (has(13)) { c.nine = 13; } else if (has(14)) { c.nine = 14; } else if (has(15)) { c.nine = 15; }
    }
    if (c.eleven === null) {
      if (has(17)) { c.eleven = 17; } else if (has(18)) { c.eleven = 18; }
    }
    if (c.thirteen === null) {
      if (has(20)) { c.thirteen = 20; } else if (has(21)) { c.thirteen = 21; }
    }
    c.add9 = c.nine !== null && c.seventh === null;
    return c;
  }

  function addDoublings(upper, n, register) {
    var out = upper.slice(0), guard = 0, i, src, up, down, cand, mid, used;
    function already(p) {
      for (var j = 0; j < out.length; j++) { if (out[j] === p) { return true; } }
      return false;
    }
    while (out.length < n && guard < 24) {
      src = upper[guard % upper.length];
      up = src + 12;
      down = src - 12;
      mid = (out[0] + out[out.length - 1]) / 2;
      cand = null;
      if (up <= 127 && !already(up) && down >= 0 && !already(down)) {
        cand = Math.abs(up - mid) <= Math.abs(down - mid) ? up : down;
      } else if (up <= 127 && !already(up)) { cand = up; }
      else if (down >= 0 && !already(down)) { cand = down; }
      else if (src + 24 <= 127 && !already(src + 24)) { cand = src + 24; }
      else if (src - 24 >= 0 && !already(src - 24)) { cand = src - 24; }
      if (cand !== null) { out.push(cand); }
      guard++;
    }
    return sortNum(out);
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
  //         spread: 0..100, bass: bool, maxUpper: optional cap on upper voices (3..5) }
  // prev: the previous result of voice() or null.
  function voice(chord, keyRoot, opts, prev) {
    var register = Math.round(opts.register), mode = spreadMode(opts.spread);
    var bassOn = !!opts.bass;
    var rootPc = T.mod12(keyRoot + chord.root);
    var picked = pickUpper(chord, opts);
    var iv = picked.iv;
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
    if (picked.doublings) { upper = addDoublings(upper, iv.length + picked.doublings, register); }
    if (bassOn) {
      while (bass >= upper[0] || (upper[0] - bass < 5 && bass - 12 >= register - 30)) { bass -= 12; }
    } else if (chord.slash) {
      bass = upper[0] - 1 - T.mod12(upper[0] - 1 - bassPcV);
    }
    var all = clampOctaves(bass === null ? upper : [bass].concat(upper));
    var outBass = bass === null ? null : all[0];
    var outUpper = bass === null ? all : all.slice(1);
    return {
      upper: outUpper, bass: outBass, notes: sortNum(all.slice(0)),
      intervals: iv, display: picked.named ? chordForDisplay(chord, iv) : null
    };
  }

  function voiceAll(chords, keyRoot, opts) {
    var out = [], prev = null;
    for (var i = 0; i < chords.length; i++) {
      prev = voice(chords[i], keyRoot, opts, prev);
      out.push(prev);
    }
    return out;
  }

  return { voice: voice, voiceAll: voiceAll, spreadMode: spreadMode, upperIntervals: upperIntervals,
    pickUpper: pickUpper, chordForDisplay: chordForDisplay };
})();
