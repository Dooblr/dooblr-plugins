// dooblr-chords engine: monophonic lead line over a chord progression (ES5).
// The line follows the same chords, seed, key and locks as the chord clip.
// Strong beats favour chord tones; weak beats use scale passing and neighbour
// tones. Color raises extensions (9, 11, 13) as melody notes; Spice (the
// Chromatic setting) raises chromatic approaches and borrowed notes.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.lead = (function () {
  var T = DC.theory, C = DC.chords, R = DC.rng;

  function round4(x) { return Math.round(x * 10000) / 10000; }
  function clampVel(v) { return Math.max(1, Math.min(127, Math.round(v))); }
  function inList(arr, v) {
    for (var i = 0; i < arr.length; i++) { if (arr[i] === v) { return true; } }
    return false;
  }

  function scalePcs(root, mode) {
    var iv = T.MODES[mode].intervals, out = [], i;
    for (i = 0; i < iv.length; i++) { out.push(T.mod12(root + iv[i])); }
    return out;
  }

  // Absolute pitch classes of triad + seventh (no extensions).
  function chordTonePcs(chord, keyRoot) {
    var base = T.mod12(keyRoot + chord.root);
    var iv = C.tones(chord), out = [], i, x;
    for (i = 0; i < iv.length; i++) {
      if (iv[i] <= 11) { x = T.mod12(base + iv[i]); if (!inList(out, x)) { out.push(x); } }
    }
    return out;
  }

  // 9ths, 11ths, 13ths already on the chord (absolute pcs).
  function extensionPcs(chord, keyRoot) {
    var base = T.mod12(keyRoot + chord.root);
    var iv = C.tones(chord), out = [], i, x;
    for (i = 0; i < iv.length; i++) {
      if (iv[i] > 11) { x = T.mod12(base + iv[i]); if (!inList(out, x)) { out.push(x); } }
    }
    return out;
  }

  // 0 or 2 in the bar, on the beat: strong. 1 or 3: medium. Else weak.
  function strength(absBeats) {
    var p = absBeats - 4 * Math.floor(absBeats / 4 + 1e-9);
    if (p < 0) { p += 4; }
    var nearest = Math.round(p * 4) / 4;
    if (Math.abs(p - nearest) > 0.06) { return 'weak'; }
    if (Math.abs(nearest) < 1e-6 || Math.abs(nearest - 2) < 1e-6) { return 'strong'; }
    if (Math.abs(nearest - 1) < 1e-6 || Math.abs(nearest - 3) < 1e-6) { return 'medium'; }
    return 'weak';
  }

  function loHi(register) {
    var lo = Math.max(48, register);
    var hi = Math.min(84, register + 12);
    if (hi - lo < 7) { hi = lo + 12; }
    return [lo, hi];
  }

  function classify(pc, chordPcs, extPcs, scale) {
    if (inList(chordPcs, pc)) { return 'chord'; }
    if (inList(extPcs, pc)) { return 'ext'; }
    if (inList(scale, pc)) { return 'scale'; }
    return 'chrom';
  }

  function typeWeight(kind, str, color, spice) {
    var c = color / 100, s = spice / 100;
    if (str === 'strong') {
      if (kind === 'chord') { return 8; }
      if (kind === 'ext') { return 0.3 + 3.2 * c; }
      if (kind === 'scale') { return 0.7; }
      return 0.05 + 0.6 * s;
    }
    if (str === 'medium') {
      if (kind === 'chord') { return 4; }
      if (kind === 'ext') { return 0.5 + 2.5 * c; }
      if (kind === 'scale') { return 2.2; }
      return 0.1 + 1.2 * s;
    }
    if (kind === 'chord') { return 1.6; }
    if (kind === 'ext') { return 0.4 + 2.0 * c; }
    if (kind === 'scale') { return 4.5; }
    return 0.15 + 3.0 * s;
  }

  function contourWeight(pitch, prev, lastLeapDir) {
    if (prev === null) { return 1; }
    var iv = pitch - prev, a = iv < 0 ? -iv : iv;
    var w;
    if (a === 0) { w = 0.4; }
    else if (a <= 2) { w = 1.15; }
    else if (a <= 4) { w = 0.5; }
    else if (a <= 7) { w = 0.2; }
    else if (a <= 12) { w = 0.07; }
    else { w = 0.015; }
    if (lastLeapDir && a <= 2 && ((lastLeapDir > 0 && iv < 0) || (lastLeapDir < 0 && iv > 0))) {
      w *= 3.5;
    }
    if (lastLeapDir && a >= 5 && ((lastLeapDir > 0 && iv > 0) || (lastLeapDir < 0 && iv < 0))) {
      w *= 0.15;
    }
    return w;
  }

  function pickPitch(prev, lastLeapDir, chord, keyRoot, mode, color, spice, register, str, cadence, rng) {
    var range = loHi(register), lo = range[0], hi = range[1];
    var scale = scalePcs(keyRoot, mode);
    var chordPcs = chordTonePcs(chord, keyRoot);
    var extPcs = extensionPcs(chord, keyRoot);
    var cands = [], weights = [], p, pc, kind, w;
    for (p = lo; p <= hi; p++) {
      pc = T.mod12(p);
      kind = classify(pc, chordPcs, extPcs, scale);
      if (kind === 'chrom' && spice <= 0) { continue; }
      if (cadence && kind !== 'chord') { continue; }
      w = typeWeight(kind, str, color, spice) * contourWeight(p, prev, lastLeapDir);
      if (cadence) {
        // Prefer root, then third, then fifth, at the close.
        var iv = T.mod12(p - (keyRoot + chord.root));
        if (iv === 0) { w *= 4; }
        else if (iv === 3 || iv === 4) { w *= 2.4; }
        else if (iv === 7) { w *= 1.6; }
      }
      // Stay near the middle of the register when starting.
      if (prev === null) { w *= 1.2 - Math.abs((p - (lo + hi) / 2) / 8); }
      if (w > 0) { cands.push(p); weights.push(w); }
    }
    if (!cands.length) {
      // Fallback: nearest chord tone in range.
      var tones = chordPcs, i, best = lo, bestD = 99, d, q;
      for (p = lo; p <= hi; p++) {
        if (inList(tones, T.mod12(p))) {
          d = prev === null ? 0 : (p > prev ? p - prev : prev - p);
          if (d < bestD) { bestD = d; best = p; }
        }
      }
      return best;
    }
    q = R.pickIndex(weights, rng.next());
    return cands[q < 0 ? 0 : q];
  }

  function pickDur(remaining, density, rng, preferLong) {
    var cands = [], w = [], d;
    function add(dur, wt) {
      if (dur <= remaining + 1e-9 && wt > 0) { cands.push(dur); w.push(wt); }
    }
    add(4, preferLong ? 10 : (9 - density * 0.07));
    add(2, preferLong ? 8 : (10 - density * 0.05));
    add(1, 6 + density * 0.025);
    add(0.5, 1.5 + density * 0.07);
    if (density > 22) { add(0.25, (density - 22) * 0.09); }
    if (!cands.length) { return remaining; }
    d = cands[R.pickIndex(w, rng.next())];
    if (d > remaining) { d = remaining; }
    return d;
  }

  function restChance(density, pos, slotBeats, firstOfClip, lastSlot) {
    var p = 0.26 - density * 0.0018;
    if (pos < 1e-6) { p *= firstOfClip ? 0.05 : 0.22; } // hit downbeats
    if (pos / slotBeats > 0.55 && !lastSlot) { p += 0.1; }
    if (lastSlot && pos / slotBeats > 0.7) { p = 0; }
    if (p < 0) { p = 0; }
    if (p > 0.55) { p = 0.55; }
    return p;
  }

  function slotPlan(i, n, slots, chordBeats, lengthBeats) {
    var start, dur;
    if (slots && slots[i]) {
      start = slots[i].start;
      dur = slots[i].duration;
    } else {
      start = i * chordBeats;
      dur = chordBeats;
    }
    if (start + dur > lengthBeats) { dur = lengthBeats - start; }
    return { start: start, duration: dur };
  }

  function applyHumanize(notes, velocity, humanize, seed, lengthBeats) {
    var h = Math.max(0, Math.min(100, humanize || 0)) / 100;
    var out = [], i, n, s, minStart, maxEnd;
    for (i = 0; i < notes.length; i++) {
      n = {
        pitch: notes[i].pitch, start_time: notes[i].start_time, duration: notes[i].duration,
        velocity: notes[i].velocity, mute: 0
      };
      if (h > 0) {
        s = R.stream(seed, i, R.P_HUMANIZE);
        n.start_time += s.signed() * 0.04 * h;
        n.velocity = clampVel(n.velocity + s.signed() * 14 * h);
      } else {
        n.velocity = clampVel(n.velocity);
      }
      minStart = i === 0 ? 0 : out[out.length - 1].start_time + out[out.length - 1].duration;
      if (n.start_time < minStart) { n.start_time = minStart; }
      if (n.start_time < 0) { n.start_time = 0; }
      maxEnd = (i + 1 < notes.length) ? notes[i + 1].start_time : lengthBeats;
      if (maxEnd > lengthBeats) { maxEnd = lengthBeats; }
      if (n.start_time + n.duration > maxEnd) { n.duration = maxEnd - n.start_time; }
      if (n.duration >= 0.0625 && n.start_time < lengthBeats - 1e-6) {
        out.push({
          pitch: n.pitch, start_time: round4(n.start_time), duration: round4(n.duration),
          velocity: n.velocity, mute: 0
        });
      }
    }
    return out;
  }

  // chords: engine chord objects; o: normalised settings; opts: {chordBeats, slots, lengthBeats}
  function notes(chords, o, opts) {
    var beats = opts.chordBeats;
    var lengthBeats = opts.lengthBeats;
    var n = chords.length;
    var raw = [];
    var prev = null, lastLeapDir = 0;
    var i, plan, pos, remaining, rngR, rngP, firstOfClip, lastSlot, isRest, dur, abs, str, cadence, pitch, vel;

    for (i = 0; i < n; i++) {
      plan = slotPlan(i, n, opts.slots, beats, lengthBeats);
      pos = 0;
      firstOfClip = i === 0;
      lastSlot = i === n - 1;
      rngR = R.stream(o.seed, i, R.P_LEAD_RHYTHM);
      rngP = R.stream(o.seed, i, R.P_LEAD_PITCH);
      var events = [];
      while (pos < plan.duration - 1e-6) {
        remaining = round4(plan.duration - pos);
        isRest = rngR.next() < restChance(o.density, pos, plan.duration, firstOfClip && pos < 1e-6, lastSlot);
        dur = pickDur(remaining, o.density, rngR, lastSlot && remaining <= 2);
        if (dur > remaining) { dur = remaining; }
        if (isRest && events.length === 0 && pos < 1e-6) { isRest = false; }
        events.push({ rest: isRest, offset: pos, duration: dur });
        pos = round4(pos + dur);
      }
      // A slot must contain a note; the last slot must end on a note.
      var hasNote = false, j;
      for (j = 0; j < events.length; j++) { if (!events[j].rest) { hasNote = true; } }
      if (!hasNote && events.length) { events[0].rest = false; }
      if (lastSlot && events.length && events[events.length - 1].rest) {
        events[events.length - 1].rest = false;
      }

      for (j = 0; j < events.length; j++) {
        if (events[j].rest) { continue; }
        abs = plan.start + events[j].offset;
        str = strength(abs);
        cadence = lastSlot && j === events.length - 1;
        pitch = pickPitch(prev, lastLeapDir, chords[i], o.root, o.mode, o.color, o.chromatic, o.register, str, cadence, rngP);
        vel = o.velocity * (str === 'strong' || cadence ? 1 : (str === 'medium' ? 0.92 : 0.84));
        raw.push({ pitch: pitch, start_time: abs, duration: events[j].duration, velocity: vel, mute: 0 });
        if (prev !== null) {
          var iv = pitch - prev;
          lastLeapDir = (iv >= 5 || iv <= -5) ? (iv > 0 ? 1 : -1) : 0;
        }
        prev = pitch;
      }
    }
    return applyHumanize(raw, o.velocity, o.humanize, o.seed, lengthBeats);
  }

  return { notes: notes, strength: strength, scalePcs: scalePcs, chordTonePcs: chordTonePcs };
})();
