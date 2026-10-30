// dooblr-chords engine: analyse a MIDI chord clip (ES5, host independent).
//
//   analyze(notes, { lengthBeats, name }) -> analysis
// notes: [{ pitch, start, duration, velocity }] in beats (start_time is
// accepted too, so Live's get_notes_extended output can be passed directly).
//
// Steps: group notes into chords by onset (with a small tolerance, sustained
// notes and arpeggios folded in), name each chord by template matching
// (root, quality, extensions, inversion), estimate the key and mode
// (Krumhansl-Kessler profiles on duration-weighted pitch classes, plus the
// chord roots and the first and last chord), and measure the harmonic rhythm
// and the register, spacing and density of the voicing.
//
// analysis = {
//   name, lengthBeats,
//   segments: [{ start, duration, root, quality, bass, pcs, slash, voices }]  (absolute pitch classes)
//   key: { root, mode, score },
//   voicing: { register, spread, bass, maxUpper },
//   noteCount
// }
// toChords(analysis, keyRoot, modeIdx) turns the segments into engine chord
// objects relative to a key, so naming, Roman numerals and voicing work as
// for generated chords.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.analysis = (function () {
  var T = DC.theory, C = DC.chords;
  var ONSET_TOL = 0.125;   // beats: onsets this close form one chord
  var GRID = 0.25;         // beats: chord boundaries snap to this grid
  var MAX_CHORDS = 16;

  // Chord templates: intervals above the root, and the chord fields they set.
  // weight: a small preference for common chords when two templates tie.
  var TEMPLATES = [
    { q: 'maj', iv: [0, 4, 7], f: { third: 4, fifth: 7 }, weight: 0.3 },
    { q: 'min', iv: [0, 3, 7], f: { third: 3, fifth: 7 }, weight: 0.3 },
    { q: 'dim', iv: [0, 3, 6], f: { third: 3, fifth: 6 }, weight: 0.1 },
    { q: 'aug', iv: [0, 4, 8], f: { third: 4, fifth: 8 }, weight: 0 },
    { q: 'sus4', iv: [0, 5, 7], f: { third: null, sus: 4, fifth: 7 }, weight: 0 },
    { q: 'sus2', iv: [0, 2, 7], f: { third: null, sus: 2, fifth: 7 }, weight: -0.1 },
    { q: '5', iv: [0, 7], f: { third: null, fifth: 7 }, weight: -0.4 },
    { q: '7', iv: [0, 4, 7, 10], f: { third: 4, fifth: 7, seventh: 10 }, weight: 0.2 },
    { q: 'maj7', iv: [0, 4, 7, 11], f: { third: 4, fifth: 7, seventh: 11 }, weight: 0.2 },
    { q: 'm7', iv: [0, 3, 7, 10], f: { third: 3, fifth: 7, seventh: 10 }, weight: 0.2 },
    { q: 'm7b5', iv: [0, 3, 6, 10], f: { third: 3, fifth: 6, seventh: 10 }, weight: 0.1 },
    { q: 'dim7', iv: [0, 3, 6, 9], f: { third: 3, fifth: 6, seventh: 9 }, weight: 0 },
    { q: 'mMaj7', iv: [0, 3, 7, 11], f: { third: 3, fifth: 7, seventh: 11 }, weight: -0.2 },
    { q: '7sus4', iv: [0, 5, 7, 10], f: { third: null, sus: 4, fifth: 7, seventh: 10 }, weight: 0 },
    { q: '6', iv: [0, 4, 7, 9], f: { third: 4, fifth: 7, thirteen: 21 }, weight: 0 },
    { q: 'm6', iv: [0, 3, 7, 9], f: { third: 3, fifth: 7, thirteen: 21 }, weight: -0.1 },
    { q: 'add9', iv: [0, 2, 4, 7], f: { third: 4, fifth: 7, nine: 14, add9: true }, weight: 0 },
    { q: 'madd9', iv: [0, 2, 3, 7], f: { third: 3, fifth: 7, nine: 14, add9: true }, weight: 0 },
    { q: '9', iv: [0, 2, 4, 7, 10], f: { third: 4, fifth: 7, seventh: 10, nine: 14 }, weight: 0.1 },
    { q: 'maj9', iv: [0, 2, 4, 7, 11], f: { third: 4, fifth: 7, seventh: 11, nine: 14 }, weight: 0.1 },
    { q: 'm9', iv: [0, 2, 3, 7, 10], f: { third: 3, fifth: 7, seventh: 10, nine: 14 }, weight: 0.1 },
    { q: '7b9', iv: [0, 1, 4, 7, 10], f: { third: 4, fifth: 7, seventh: 10, nine: 13 }, weight: 0 },
    { q: '7#9', iv: [0, 3, 4, 7, 10], f: { third: 4, fifth: 7, seventh: 10, nine: 15 }, weight: 0 },
    { q: 'm11', iv: [0, 2, 3, 5, 7, 10], f: { third: 3, fifth: 7, seventh: 10, nine: 14, eleven: 17 }, weight: 0 },
    { q: 'maj7#11', iv: [0, 4, 6, 7, 11], f: { third: 4, fifth: 7, seventh: 11, eleven: 18 }, weight: -0.1 },
    { q: '13', iv: [0, 2, 4, 7, 9, 10], f: { third: 4, fifth: 7, seventh: 10, nine: 14, thirteen: 21 }, weight: 0 },
    { q: 'maj13', iv: [0, 2, 4, 7, 9, 11], f: { third: 4, fifth: 7, seventh: 11, nine: 14, thirteen: 21 }, weight: 0 }
  ];

  // Krumhansl-Kessler key profiles.
  var PROFILE_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  var PROFILE_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  // Mode indices (see theory.MODES) tried for each family, plain mode first.
  var FAMILY_MODES = { major: [0, 4, 5], minor: [1, 2, 3, 6] };

  function has(list, v) { for (var i = 0; i < list.length; i++) { if (list[i] === v) { return true; } } return false; }
  function templateByQ(q) { for (var i = 0; i < TEMPLATES.length; i++) { if (TEMPLATES[i].q === q) { return TEMPLATES[i]; } } return null; }
  function snap(x) { return Math.round(x / GRID) * GRID; }
  function noteStart(n) { return +(n.start !== undefined ? n.start : n.start_time) || 0; }
  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  // ---- chord identification ----------------------------------------------------

  // pcs: distinct pitch classes; bass: pitch class of the lowest note (or -1).
  // weights (optional): pc -> weight, so passing tones count less.
  function identify(pcs, bass, weights) {
    var best = null, r, t, i;
    var w = function (pc) { return weights && weights[pc] !== undefined ? weights[pc] : 1; };
    for (r = 0; r < pcs.length; r++) {
      var root = pcs[r], rel = [];
      for (i = 0; i < pcs.length; i++) { rel.push(T.mod12(pcs[i] - root)); }
      for (t = 0; t < TEMPLATES.length; t++) {
        var tp = TEMPLATES[t], score = tp.weight, missingThird = false;
        for (i = 0; i < tp.iv.length; i++) {
          if (has(rel, tp.iv[i])) { score += 2 * Math.min(1, w(T.mod12(root + tp.iv[i])) + 0.25); }
          else if (tp.iv[i] === 7) { score -= 0.6; } // an omitted fifth is common
          else { score -= 2.2; if (i === 1) { missingThird = true; } }
        }
        for (i = 0; i < rel.length; i++) { if (!has(tp.iv, rel[i])) { score -= 1.6 * Math.min(1, w(pcs[i]) + 0.25); } }
        if (bass === root) { score += 1.2; }
        if (missingThird && tp.f.third !== null) { score -= 0.5; }
        if (best === null || score > best.score + 1e-9) { best = { root: root, quality: tp.q, score: score }; }
      }
    }
    if (!best) { return null; }
    var tpl = templateByQ(best.quality);
    var slash = null;
    if (bass >= 0 && bass !== best.root) {
      var b = T.mod12(bass - best.root);
      if (tpl.f.third !== null && tpl.f.third !== undefined && b === tpl.f.third) { slash = 'third'; }
      else if (b === tpl.f.fifth) { slash = 'fifth'; }
      else if (tpl.f.seventh !== undefined && b === tpl.f.seventh) { slash = 'seventh'; }
    }
    return { root: best.root, quality: best.quality, bass: bass, slash: slash, score: best.score };
  }

  // ---- grouping notes into chords -------------------------------------------------

  function clusters(notes) {
    var sorted = notes.slice(0).sort(function (a, b) { return noteStart(a) - noteStart(b) || a.pitch - b.pitch; });
    var out = [], cur = null;
    for (var i = 0; i < sorted.length; i++) {
      var n = sorted[i], s = noteStart(n);
      if (!cur || s - cur.t > ONSET_TOL) { cur = { t: s, notes: [] }; out.push(cur); }
      cur.notes.push(n);
    }
    return out;
  }

  function pcsOf(notes) {
    var out = [];
    for (var i = 0; i < notes.length; i++) { var pc = T.mod12(notes[i].pitch); if (!has(out, pc)) { out.push(pc); } }
    return out;
  }

  function subset(a, b) { for (var i = 0; i < a.length; i++) { if (!has(b, a[i])) { return false; } } return true; }

  // Notes still sounding at time t that began earlier.
  function sustained(notes, t) {
    var out = [];
    for (var i = 0; i < notes.length; i++) {
      var s = noteStart(notes[i]);
      if (s < t - ONSET_TOL && s + (+notes[i].duration) > t + ONSET_TOL) { out.push(notes[i]); }
    }
    return out;
  }

  function lowest(notes) {
    var lo = null;
    for (var i = 0; i < notes.length; i++) { if (lo === null || notes[i].pitch < lo.pitch) { lo = notes[i]; } }
    return lo;
  }

  // Group onset clusters into chord segments.
  function segment(notes, lengthBeats) {
    var cl = clusters(notes), segs = [], i, k;
    for (i = 0; i < cl.length; i++) {
      var c = cl[i], held = sustained(notes, c.t);
      var own = pcsOf(c.notes);
      var sounding = c.notes.concat(held);
      var prev = segs.length ? segs[segs.length - 1] : null;
      // A re-attack of notes already in the chord, or a lone note of an
      // arpeggio that has not reached three notes yet, extends the chord.
      if (prev && (subset(own, prev.pcs) || (prev.pcs.length < 3 && c.t - prev.start < 2 + 1e-9))) {
        for (k = 0; k < c.notes.length; k++) { prev.notes.push(c.notes[k]); }
        prev.pcs = pcsOf(prev.notes.concat(prev.held));
        continue;
      }
      // A partial chord over held notes (for example a bass that sustains
      // under a new upper chord) borrows the held notes.
      var useHeld = own.length < 3 ? held : [];
      var low = lowest(sounding);
      segs.push({ start: c.t, notes: c.notes.slice(0), held: useHeld, pcs: pcsOf(c.notes.concat(useHeld)), low: low ? low.pitch : 0 });
    }
    // Identify, then merge neighbours that name the same chord.
    var out = [];
    for (i = 0; i < segs.length; i++) {
      var sg = segs[i], all = sg.notes.concat(sg.held), wts = {}, tot = 0;
      for (k = 0; k < all.length; k++) { var pc = T.mod12(all[k].pitch); wts[pc] = (wts[pc] || 0) + (+all[k].duration); tot += (+all[k].duration); }
      var maxW = 0, key;
      for (key in wts) { if (wts.hasOwnProperty(key) && wts[key] > maxW) { maxW = wts[key]; } }
      for (key in wts) { if (wts.hasOwnProperty(key)) { wts[key] = maxW > 0 ? wts[key] / maxW : 1; } }
      var id = identify(sg.pcs, T.mod12(sg.low), wts);
      if (!id) { continue; }
      var last = out.length ? out[out.length - 1] : null;
      if (last && last.root === id.root && last.quality === id.quality && last.bass === id.bass) {
        last.notes = last.notes.concat(sg.notes);
        continue;
      }
      out.push({ start: sg.start, root: id.root, quality: id.quality, bass: id.bass, slash: id.slash, pcs: sg.pcs, notes: sg.notes.concat(sg.held), firstNotes: sg.notes.concat(sg.held) });
    }
    // Snap boundaries to the grid and compute durations.
    for (i = 0; i < out.length; i++) { out[i].start = snap(out[i].start); }
    var cleaned = [];
    for (i = 0; i < out.length; i++) {
      if (cleaned.length && out[i].start <= cleaned[cleaned.length - 1].start) { continue; } // collapsed onto the previous one
      cleaned.push(out[i]);
    }
    for (i = 0; i < cleaned.length; i++) {
      var end = (i + 1 < cleaned.length) ? cleaned[i + 1].start : lengthBeats;
      cleaned[i].duration = Math.max(GRID, end - cleaned[i].start);
    }
    // Too many chords: fold the shortest into the one before it.
    while (cleaned.length > MAX_CHORDS) {
      var shortest = 1;
      for (i = 2; i < cleaned.length; i++) { if (cleaned[i].duration < cleaned[shortest].duration) { shortest = i; } }
      cleaned[shortest - 1].duration += cleaned[shortest].duration;
      cleaned.splice(shortest, 1);
    }
    return cleaned;
  }

  // ---- key detection ------------------------------------------------------------

  function correlate(x, profile, shift) {
    var mx = 0, mp = 0, i;
    for (i = 0; i < 12; i++) { mx += x[i]; mp += profile[i]; }
    mx /= 12; mp /= 12;
    var num = 0, dx = 0, dp = 0;
    for (i = 0; i < 12; i++) {
      var a = x[T.mod12(i + shift)] - mx, b = profile[i] - mp;
      num += a * b; dx += a * a; dp += b * b;
    }
    return (dx > 0 && dp > 0) ? num / Math.sqrt(dx * dp) : 0;
  }

  function isMinorQuality(q) { return q.charAt(0) === 'm' && q.substring(0, 3) !== 'maj'; }
  function isMajorQuality(q) { return q === 'maj' || q === '7' || q === 'maj7' || q === '6' || q === 'add9' || q === '9' || q === 'maj9' || q === '13' || q === 'maj13' || q === 'maj7#11' || q === '7b9' || q === '7#9'; }

  // hist: duration-weighted pitch classes; segs: identified chord segments.
  function detectKey(hist, segs) {
    var h = hist.slice(0), total = 0, i, r;
    for (i = 0; i < 12; i++) { total += hist[i]; }
    var rootTotal = 0, roots = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    for (i = 0; i < segs.length; i++) { roots[segs[i].root] += segs[i].duration; rootTotal += segs[i].duration; }
    if (rootTotal > 0) { for (i = 0; i < 12; i++) { h[i] += 0.5 * total * roots[i] / rootTotal; } }
    var best = null;
    for (r = 0; r < 12; r++) {
      for (var fam = 0; fam < 2; fam++) {
        var score = correlate(h, fam === 0 ? PROFILE_MAJOR : PROFILE_MINOR, r);
        if (segs.length) {
          var f = segs[0], l = segs[segs.length - 1];
          var fit = function (s) { return s.root === r && (fam === 0 ? !isMinorQuality(s.quality) : isMinorQuality(s.quality)); };
          if (fit(f)) { score += 0.12; }
          if (fit(l)) { score += 0.08; }
        }
        if (best === null || score > best.score + 1e-9) { best = { root: r, family: fam === 0 ? 'major' : 'minor', score: score }; }
      }
    }
    // Pick the mode within the family that fits the pitch classes best; the
    // plain major or natural minor wins unless the evidence is clear.
    var cands = FAMILY_MODES[best.family], bestMode = cands[0], bestFit = -1e9;
    for (i = 0; i < cands.length; i++) {
      var iv = T.MODES[cands[i]].intervals, fitv = 0;
      for (var pc = 0; pc < 12; pc++) { fitv += has(iv, T.mod12(pc - best.root)) ? hist[pc] : -hist[pc]; }
      if (i > 0) { fitv -= 0.04 * total; }
      if (fitv > bestFit + 1e-9) { bestFit = fitv; bestMode = cands[i]; }
    }
    return { root: best.root, mode: bestMode, score: Math.round(best.score * 1000) / 1000 };
  }

  // ---- voicing ---------------------------------------------------------------------

  function measureVoicing(segs) {
    var lows = [], spans = [], counts = [], bassCount = 0, i;
    for (i = 0; i < segs.length; i++) {
      var p = [], ns = segs[i].firstNotes;
      for (var k = 0; k < ns.length; k++) { if (!has(p, ns[k].pitch)) { p.push(ns[k].pitch); } }
      p.sort(function (a, b) { return a - b; });
      if (!p.length) { continue; }
      var upper = p;
      if (p.length >= 3 && (p[1] - p[0] >= 7 || (p[0] < 50 && p[1] - p[0] >= 5))) { upper = p.slice(1); bassCount++; }
      lows.push(upper[0]);
      spans.push(upper[upper.length - 1] - upper[0]);
      counts.push(upper.length);
    }
    if (!lows.length) { return { register: 60, spread: 25, bass: 1, maxUpper: 4 }; }
    var med = function (a) { var s = a.slice(0).sort(function (x, y) { return x - y; }); return s[Math.floor(s.length / 2)]; };
    var span = med(spans), cnt = med(counts);
    var spread = span <= 12 ? 10 : (span <= 19 ? 50 : 85);
    var bassOn = bassCount * 2 >= lows.length ? 1 : 0;
    return { register: clamp(med(lows), 36, 72), spread: spread, bass: bassOn, maxUpper: clamp(cnt, 3, bassOn ? 4 : 5) };
  }

  // ---- public ------------------------------------------------------------------------

  function analyze(rawNotes, opts) {
    opts = opts || {};
    var notes = [], i, maxEnd = 0;
    for (i = 0; i < (rawNotes || []).length; i++) {
      var n = rawNotes[i];
      if (n.mute) { continue; }
      var s = noteStart(n), d = +n.duration || 0;
      if (!(d > 0)) { d = 0.0625; }
      notes.push({ pitch: Math.round(n.pitch), start: s, duration: d, velocity: +n.velocity || 100 });
      if (s + d > maxEnd) { maxEnd = s + d; }
    }
    if (!notes.length) { throw new Error('The clip has no notes.'); }
    var lengthBeats = +opts.lengthBeats > 0 ? +opts.lengthBeats : Math.max(4, Math.ceil(maxEnd / 4 - 1e-6) * 4);
    var segs = segment(notes, lengthBeats);
    if (!segs.length) { throw new Error('No chords were found in the clip.'); }
    var hist = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    for (i = 0; i < notes.length; i++) { hist[T.mod12(notes[i].pitch)] += notes[i].duration; }
    var key = detectKey(hist, segs);
    var out = [];
    for (i = 0; i < segs.length; i++) {
      out.push({ start: segs[i].start, duration: segs[i].duration, root: segs[i].root, quality: segs[i].quality,
        bass: segs[i].bass, slash: segs[i].slash, pcs: segs[i].pcs.slice(0).sort(function (a, b) { return a - b; }) });
    }
    return {
      name: String(opts.name || '').replace(/^\s+|\s+$/g, ''), lengthBeats: lengthBeats, segments: out,
      key: key, voicing: measureVoicing(segs), noteCount: notes.length
    };
  }

  // Letters (0..6 above the tonic letter) for chromatic roots, flat-leaning.
  var CHROMATIC_LETTER = [0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6];

  function genericScale(f) {
    var s;
    if (f.third === 3) { s = [0, 2, 3, 5, f.fifth, f.fifth === 6 ? 8 : 9, 10]; }
    else { s = [0, 2, 4, f.eleven === 18 ? 6 : 5, f.fifth === 8 ? 8 : 7, 9, 10]; }
    if (f.seventh !== undefined && f.seventh !== null) { s[6] = f.seventh; }
    if (f.third === null || f.third === undefined) { s[2] = 4; }
    return s;
  }

  // One segment as an engine chord relative to (keyRoot, modeIdx).
  function makeChord(seg, keyRoot, modeIdx) {
    var tpl = templateByQ(seg.quality), f = tpl.f;
    var iv = T.MODES[modeIdx].intervals;
    var rel = T.mod12(seg.root - keyRoot), d = -1, i;
    for (i = 0; i < 7; i++) { if (iv[i] === rel) { d = i; } }
    var letter = d >= 0 ? d : CHROMATIC_LETTER[rel];
    if (d < 0 && rel === 6 && f.fifth === 6) { letter = 3; } // #iv dim, not bV dim
    var scale = d >= 0 ? T.rotate(iv, d) : genericScale(f);
    var c = C.fromScale(rel, letter, scale, { degree: d >= 0 ? d : letter, func: DC.generator.FUNC[d >= 0 ? d : letter] });
    c.third = f.third === undefined ? null : f.third;
    c.sus = f.sus || 0;
    c.fifth = f.fifth;
    c.seventh = f.seventh === undefined ? null : f.seventh;
    c.nine = f.nine === undefined ? null : f.nine;
    c.eleven = f.eleven === undefined ? null : f.eleven;
    c.thirteen = f.thirteen === undefined ? null : f.thirteen;
    c.add9 = !!f.add9;
    c.slash = seg.slash;
    var lv = 0;
    if (c.seventh !== null || c.add9 || c.sus || c.thirteen !== null) { lv = 1; }
    if (c.nine !== null && !c.add9) { lv = 2; }
    if (c.eleven !== null) { lv = 3; }
    if (c.thirteen !== null && c.seventh !== null) { lv = 4; }
    c.level = lv;
    // Diatonic if every chord tone is in the scale.
    var tones = C.tones(c), inScale = true;
    for (i = 0; i < tones.length; i++) { if (!has(iv, T.mod12(rel + tones[i]))) { inScale = false; } }
    c.source = inScale ? 'diatonic' : 'borrowed';
    return c;
  }

  function toChords(analysis, keyRoot, modeIdx) {
    var segs = analysis.segments, out = [], i;
    for (i = 0; i < segs.length; i++) { out.push(makeChord(segs[i], keyRoot, modeIdx)); }
    // A non-diatonic major chord a fifth above the next chord reads as V/x.
    for (i = 0; i < out.length; i++) {
      var c = out[i], next = out[(i + 1) % out.length];
      if (out.length > 1 && c.source === 'borrowed' && c.third === 4 && c.fifth === 7 && (c.seventh === null || c.seventh === 10) &&
          T.mod12(c.root - 7) === next.root && next.root !== 0 && c.root !== 7) {
        c.source = 'secondary'; c.func = 'D'; c.degree = 4; c.targetRoman = C.baseRoman(next);
      }
    }
    return out;
  }

  // Chord symbols for display (absolute, spelled in the detected key).
  function symbols(analysis, keyRoot, modeIdx) {
    var ch = toChords(analysis, keyRoot, modeIdx), out = [];
    for (var i = 0; i < ch.length; i++) { out.push(C.name(ch[i], keyRoot, modeIdx)); }
    return out;
  }

  return {
    TEMPLATES: TEMPLATES, ONSET_TOL: ONSET_TOL, GRID: GRID, MAX_CHORDS: MAX_CHORDS,
    identify: identify, segment: segment, detectKey: detectKey, analyze: analyze,
    makeChord: makeChord, toChords: toChords, symbols: symbols
  };
})();
