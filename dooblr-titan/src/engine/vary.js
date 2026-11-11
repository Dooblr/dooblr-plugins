// dooblr-titan engine: deterministic musical variations of a clip (ES5).
//
// Four preview slots. Slot i is a named character mixed with the type
// amounts (rhythm, melody, ghosts, accents) and the master Amount.
// The same (notes, settings, seed, slot) always yields the same notes.
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.vary = (function () {
  var N = DT.notes, R = DT.rng, T = DT.theory, Q = DT.quantize;
  var COUNT = 4;
  var LABELS = ['Displace', 'Substitute', 'Fill', 'Invert'];

  function amt(settings, which) {
    var master = N.clamp(+settings.amount || 0, 0, 100) / 100;
    var part = N.clamp(+settings[which] || 0, 0, 100) / 100;
    return master * part;
  }

  function phraseEnds(notes, lengthBeats) {
    // A phrase break is a gap of at least 1 beat, or a barline if none.
    var ends = [], i;
    var sorted = N.sort(N.copyAll(notes));
    for (i = 0; i < sorted.length; i++) {
      var end = sorted[i].start + sorted[i].duration;
      var next = (i + 1 < sorted.length) ? sorted[i + 1].start : lengthBeats;
      if (next - end >= 1 - 1e-6) { ends.push(end); }
    }
    if (!ends.length) {
      var bar = 4;
      for (i = bar; i <= lengthBeats + 1e-6; i += bar) { ends.push(i); }
    }
    return ends;
  }

  function scaleNeighbor(pitch, root, mode, dir) {
    var step = dir < 0 ? -1 : 1, p = pitch, k;
    for (k = 1; k <= 12; k++) {
      p = pitch + step * k;
      if (p < 0 || p > 127) { return pitch; }
      if (T.inScale(p, root, mode)) { return p; }
    }
    return pitch;
  }

  function meanPitch(notes) {
    if (!notes.length) { return 60; }
    var s = 0, i;
    for (i = 0; i < notes.length; i++) { s += notes[i].pitch; }
    return s / notes.length;
  }

  function shiftNotes(notes, a, rng, grid) {
    if (a <= 0) { return notes; }
    var i, maxShift = grid;
    for (i = 0; i < notes.length; i++) {
      if (rng.next() < a * 0.55) {
        var steps = rng.int(2) === 0 ? -1 : 1;
        if (rng.next() < a * 0.35) { steps *= 2; }
        notes[i].start = N.round4(Math.max(0, notes[i].start + steps * maxShift * (0.5 + 0.5 * rng.next())));
      }
    }
    return N.sort(notes);
  }

  function thinNotes(notes, a, rng) {
    if (a <= 0 || notes.length < 3) { return notes; }
    var out = [], i;
    for (i = 0; i < notes.length; i++) {
      var quiet = notes[i].velocity < 70;
      var drop = quiet && rng.next() < a * 0.5;
      if (!drop) { out.push(notes[i]); }
    }
    return out.length ? out : notes.slice(0, 1);
  }

  function fillNotes(notes, a, rng, root, mode, lengthBeats, grid) {
    if (a <= 0) { return notes; }
    var out = notes.slice(0), i;
    for (i = 0; i < notes.length - 1; i++) {
      var gap = notes[i + 1].start - (notes[i].start + notes[i].duration);
      if (gap >= grid && rng.next() < a * 0.45) {
        var p = scaleNeighbor(notes[i].pitch, root, mode, rng.next() < 0.5 ? -1 : 1);
        var st = N.round4(notes[i].start + notes[i].duration);
        var dur = N.round4(Math.min(grid, gap * 0.5));
        if (st + dur <= lengthBeats + 1e-6) {
          out.push({ pitch: p, start: st, duration: dur, velocity: N.clamp(notes[i].velocity - 20, 1, 127), mute: 0, channel: 0 });
        }
      }
    }
    return N.sort(out);
  }

  function substitute(notes, a, rng, root, mode) {
    if (a <= 0) { return notes; }
    var i;
    for (i = 0; i < notes.length; i++) {
      if (rng.next() < a * 0.5) {
        notes[i].pitch = scaleNeighbor(notes[i].pitch, root, mode, rng.next() < 0.5 ? -1 : 1);
      }
    }
    return notes;
  }

  function octaveJumps(notes, a, rng) {
    if (a <= 0) { return notes; }
    var i;
    for (i = 0; i < notes.length; i++) {
      if (rng.next() < a * 0.25) {
        var dir = rng.next() < 0.5 ? -12 : 12;
        var p = notes[i].pitch + dir;
        if (p >= 0 && p <= 127) { notes[i].pitch = p; }
      }
    }
    return notes;
  }

  function invert(notes, a, root, mode) {
    if (a < 0.35) { return notes; }
    var m = meanPitch(notes), i;
    for (i = 0; i < notes.length; i++) {
      var p = Math.round(2 * m - notes[i].pitch);
      p = T.nearestInScale(p, root, mode);
      notes[i].pitch = N.clamp(p, 0, 127);
    }
    return notes;
  }

  function retrograde(notes, a) {
    if (a < 0.45 || notes.length < 2) { return notes; }
    var pitches = [], i;
    for (i = 0; i < notes.length; i++) { pitches.push(notes[i].pitch); }
    pitches.reverse();
    for (i = 0; i < notes.length; i++) { notes[i].pitch = pitches[i]; }
    return notes;
  }

  function ghostFills(notes, a, rng, root, mode, lengthBeats, ends) {
    if (a <= 0) { return notes; }
    var out = notes.slice(0), i, k;
    for (i = 0; i < ends.length; i++) {
      if (rng.next() >= a) { continue; }
      var end = ends[i];
      var barEnd = Math.ceil(end / 4 - 1e-6) * 4;
      if (barEnd - end < 0.2) { barEnd += 4; }
      if (barEnd > lengthBeats) { barEnd = lengthBeats; }
      var nGhosts = 1 + rng.int(2);
      var lastPitch = notes.length ? notes[notes.length - 1].pitch : 60;
      for (k = 0; k < nGhosts; k++) {
        var st = N.round4(end + (k + 0.25) * (barEnd - end) / (nGhosts + 0.5));
        if (st >= barEnd - 0.05) { break; }
        var p = scaleNeighbor(lastPitch, root, mode, rng.next() < 0.5 ? -1 : 1);
        out.push({
          pitch: p, start: st, duration: N.round4(Math.min(0.12, barEnd - st - 0.01)),
          velocity: 20 + rng.int(18), mute: 0, channel: 0
        });
        lastPitch = p;
      }
    }
    return N.sort(out);
  }

  function accent(notes, a, rng, lengthBeats) {
    if (a <= 0) { return notes; }
    var i;
    for (i = 0; i < notes.length; i++) {
      var beat = notes[i].start % 4;
      var onDown = beat < 0.08 || Math.abs(beat - 2) < 0.08;
      var delta;
      if (onDown) { delta = Math.round(14 * a); }
      else { delta = -Math.round(8 * a * (0.4 + 0.6 * rng.next())); }
      notes[i].velocity = N.clamp(notes[i].velocity + delta, 1, 127);
    }
    return notes;
  }

  function applySlot(notes, settings, slot) {
    var src = N.sort(N.copyAll(notes));
    if (!src.length) { return { notes: src, label: LABELS[slot] || 'Var', lengthBeats: 4 }; }
    var lengthBeats = N.lengthBeats(src, settings.lengthBeats);
    var seed = Math.round(+settings.seed || 1);
    var root = T.mod12(Math.round(+settings.root || 0));
    var mode = N.clamp(Math.round(+settings.mode || 0), 0, T.MODES.length - 1);
    var grid = Q.gridBeats(settings.grid);
    var rA = amt(settings, 'rhythm');
    var mA = amt(settings, 'melody');
    var gA = amt(settings, 'ghosts');
    var aA = amt(settings, 'accents');

    var rngS = R.stream(seed, slot, R.P_SHIFT);
    var rngT = R.stream(seed, slot, R.P_THIN);
    var rngF = R.stream(seed, slot, R.P_FILL);
    var rngU = R.stream(seed, slot, R.P_SUB);
    var rngO = R.stream(seed, slot, R.P_OCT);
    var rngG = R.stream(seed, slot, R.P_GHOST);
    var rngA = R.stream(seed, slot, R.P_ACCENT);

    // Character: each slot leans on a different treatment, still scaled by amounts.
    if (slot === 0) {
      src = shiftNotes(src, rA, rngS, grid);
      src = accent(src, aA * 0.8, rngA, lengthBeats);
    } else if (slot === 1) {
      src = substitute(src, mA, rngU, root, mode);
      src = octaveJumps(src, mA, rngO);
      src = accent(src, aA * 0.5, rngA, lengthBeats);
    } else if (slot === 2) {
      src = fillNotes(src, rA, rngF, root, mode, lengthBeats, grid);
      src = ghostFills(src, gA, rngG, root, mode, lengthBeats, phraseEnds(src, lengthBeats));
    } else {
      if (mA >= 0.45) { src = invert(src, mA, root, mode); }
      else { src = retrograde(src, mA); }
      src = thinNotes(src, rA * 0.8, rngT);
      src = ghostFills(src, gA * 0.6, rngG, root, mode, lengthBeats, phraseEnds(src, lengthBeats));
    }

    // Shared extras at high amount so every knob still does something in every slot.
    if (rA > 0 && slot !== 0) { src = shiftNotes(src, rA * 0.25, rngS, grid); }
    if (mA > 0 && slot !== 1) { src = substitute(src, mA * 0.2, rngU, root, mode); }
    if (gA > 0 && slot !== 2 && slot !== 3) {
      src = ghostFills(src, gA * 0.35, rngG, root, mode, lengthBeats, phraseEnds(src, lengthBeats));
    }
    if (aA > 0 && slot !== 0 && slot !== 1) { src = accent(src, aA * 0.4, rngA, lengthBeats); }

    var i;
    for (i = 0; i < src.length; i++) {
      if (src[i].start > lengthBeats - 0.01) { src[i].start = N.round4(lengthBeats - 0.01); }
      if (src[i].start + src[i].duration > lengthBeats) {
        src[i].duration = N.round4(Math.max(0.02, lengthBeats - src[i].start));
      }
    }
    src = N.sort(src);
    return { notes: src, label: LABELS[slot], lengthBeats: lengthBeats };
  }

  function preview(notes, settings, count) {
    var n = count || COUNT, out = [], i;
    for (i = 0; i < n; i++) { out.push(applySlot(notes, settings, i)); }
    return out;
  }

  return { COUNT: COUNT, LABELS: LABELS, applySlot: applySlot, preview: preview, phraseEnds: phraseEnds };
})();
