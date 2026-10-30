// dooblr-chords engine: derive a VARIATION or a B SECTION from an analysed
// source clip (ES5, host independent).
//
//   derive(analysis, o, kind, locks, keep) -> {
//     chords, slots: [{start, duration}], lengthBeats, ending, changed: [bool],
//     rhythm, region: {id, label} | null }
// o: normalized engine settings (root, mode, length, duration, color,
//    chromatic, seed). kind: 'var' | 'b'. locks: chord or null per slot.
// keep: { rhythm } to hold the B-section rhythm plan (used when chords are
//    locked, so a reroll does not change the number of chords).
//
// VARIATION keeps the key, the harmonic rhythm, the first chord and the
// cadence, and reharmonises some of the chords in between: diatonic
// substitutes a third away or with the same function, and with Chromatic up,
// secondary dominants, tritone subs and borrowed chords. Color + Chromatic
// set how many chords change (one or two at low settings).
//
// B SECTION moves to another tonal centre (relative major/minor, the IV
// region, ii, or with Chromatic up a modal region), changes the harmonic
// rhythm, starts away from the tonic and ends on a dominant or
// pre-dominant that leads back to the source's first chord.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.derive = (function () {
  var T = DC.theory, C = DC.chords, G = DC.generator, R = DC.rng, A = DC.analysis;
  var DURATION_BEATS = [2, 4, 8];

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
  function degreeOf(modeIdx, rel) {
    var iv = T.MODES[modeIdx].intervals;
    for (var d = 0; d < 7; d++) { if (iv[d] === T.mod12(rel)) { return d; } }
    return -1;
  }
  function weird(c) { return C.isDiminished(c) || C.isAugmented(c); }
  function sameChord(a, b) { return a && b && a.root === b.root && a.third === b.third && a.fifth === b.fifth; }
  function inScale(c, modeIdx) {
    var iv = T.MODES[modeIdx].intervals, t = C.tones(c);
    for (var i = 0; i < t.length; i++) { if (degreeOf(modeIdx, c.root + t[i]) < 0) { return false; } }
    return true;
  }

  function sourceSlots(analysis) {
    var out = [];
    for (var i = 0; i < analysis.segments.length; i++) { out.push({ start: analysis.segments[i].start, duration: analysis.segments[i].duration }); }
    return out;
  }
  function uniformSlots(n, beats) {
    var out = [];
    for (var i = 0; i < n; i++) { out.push({ start: i * beats, duration: beats }); }
    return out;
  }
  function endOf(slots) { var l = slots[slots.length - 1]; return l.start + l.duration; }

  function prepareLocks(locks, n) {
    var lk = [], locked = [];
    for (var i = 0; i < n; i++) {
      var l = (locks && locks[i]) ? locks[i] : null;
      lk.push(l); locked.push(!!l);
    }
    return { lk: lk, locked: locked };
  }

  // ---- VARIATION -----------------------------------------------------------------

  function variationOptions(o, cur, prev, next) {
    var c = o.chromatic / 100, opts = [], w = [];
    var d = degreeOf(o.mode, cur.root);
    function add(ch, weight) {
      if (!ch || weight <= 0 || sameChord(ch, cur) || sameChord(ch, prev) || sameChord(ch, next)) { return; }
      for (var k = 0; k < opts.length; k++) { if (sameChord(opts[k], ch)) { w[k] = Math.max(w[k], weight); return; } }
      opts.push(ch); w.push(weight);
    }
    function dia(deg, weight) {
      var ch = C.diatonic(o.mode, deg, { degree: deg, func: G.FUNC[deg] });
      if (!weird(ch)) { add(ch, weight); }
    }
    if (d >= 0) {
      dia((d + 5) % 7, 1.5); // a third below (relative / submediant)
      dia((d + 2) % 7, 1.0); // a third above (mediant)
      for (var k = 0; k < 7; k++) { if (k !== d && G.FUNC[k] === G.FUNC[d] && k !== (d + 5) % 7 && k !== (d + 2) % 7) { dia(k, 0.8); } }
    }
    if (c > 0 && next && !weird(next)) {
      var isVofNext = cur.root === T.mod12(next.root + 7) && cur.third === 4;
      if (!isVofNext && next.source !== 'tritone') { add(G._secondaryDominant(next), 1.5 * c); }
      add(G._tritoneSub(next), (isVofNext ? 2 : 1) * c);
    }
    if (c > 0 && d >= 0) {
      add(G._borrowedChord(o, d, R.stream(o.seed, d, R.P_VARSUB).next()), 1.2 * c);
    }
    return { opts: opts, w: w };
  }

  function variation(analysis, o, locks) {
    var src = A.toChords(analysis, o.root, o.mode), srcN = src.length;
    var n = o.length, i, slots, lengthBeats, rhythm;
    if (n === srcN) { slots = sourceSlots(analysis); lengthBeats = analysis.lengthBeats; rhythm = 'source'; }
    else { slots = uniformSlots(n, DURATION_BEATS[o.duration]); lengthBeats = endOf(slots); rhythm = 'uniform'; }
    var L = prepareLocks(locks, n), chords = [], changed = [];
    for (i = 0; i < n; i++) {
      var idx = (i === n - 1) ? srcN - 1 : i % srcN;
      chords.push(L.locked[i] ? C.copy(L.lk[i]) : C.copy(src[idx]));
      changed.push(false);
    }
    // Fixed: the opening chord, the last chord, and a V - I cadence before it.
    var fixed = [];
    for (i = 0; i < n; i++) { fixed.push(L.locked[i]); }
    fixed[0] = true;
    if (n > 2) { fixed[n - 1] = true; }
    if (n >= 4) {
      var pen = chords[n - 2], fin = chords[n - 1];
      if (T.mod12(pen.root - fin.root) === 7 || pen.func === 'D' || pen.source === 'secondary') { fixed[n - 2] = true; }
    }
    var cands = [];
    for (i = 0; i < n; i++) { if (!fixed[i]) { cands.push({ i: i, u: R.stream(o.seed, i, R.P_VARPICK).next() }); } }
    var m = cands.length;
    if (m > 0) {
      var k = clamp(Math.round(m * (0.2 + 0.8 * (o.color + o.chromatic) / 200)), 1, m);
      cands.sort(function (a, b) { return a.u - b.u; });
      var pick = [];
      for (i = 0; i < n; i++) { pick.push(false); }
      for (i = 0; i < k; i++) { pick[cands[i].i] = true; }
      for (i = n - 1; i >= 0; i--) {
        if (!pick[i]) { continue; }
        var cur = chords[i], prev = i > 0 ? chords[i - 1] : null, next = (i + 1 < n) ? chords[i + 1] : chords[0];
        var op = variationOptions(o, cur, prev, next);
        var choice = R.pickIndex(op.w, R.stream(o.seed, i, R.P_VARSUB).next());
        if (choice >= 0) { chords[i] = op.opts[choice]; changed[i] = true; }
      }
    }
    var keepMask = [];
    for (i = 0; i < n; i++) { keepMask.push(!changed[i]); }
    G._colorize(o, chords, keepMask);
    G._invert(o, chords, keepMask, 'turnaround');
    return { chords: chords, slots: slots, lengthBeats: lengthBeats, ending: 'source', changed: changed, rhythm: rhythm, region: null };
  }

  // ---- B SECTION -----------------------------------------------------------------

  var RHYTHMS = ['same', 'faster', 'slower', 'push'];

  function bSlots(analysis, plan) {
    var src = sourceSlots(analysis), out = [], i;
    if (plan === 'faster') {
      for (i = 0; i < src.length; i++) {
        if (src[i].duration >= 1 && out.length + (src.length - i) < 16) {
          var h = src[i].duration / 2;
          out.push({ start: src[i].start, duration: h }, { start: src[i].start + h, duration: h });
        } else { out.push(src[i]); }
      }
    } else if (plan === 'slower') {
      for (i = 0; i < src.length; i += 2) {
        if (i + 1 < src.length) { out.push({ start: src[i].start, duration: src[i].duration + src[i + 1].duration }); }
        else { out.push(src[i]); }
      }
    } else if (plan === 'push') {
      for (i = 0; i < src.length - 1; i++) { out.push(src[i]); }
      var last = src[src.length - 1], first = last.duration / 2;
      out.push({ start: last.start, duration: first }, { start: last.start + first, duration: last.duration - first });
    } else {
      out = src;
    }
    return out;
  }

  function pickRhythm(analysis, o) {
    var src = analysis.segments, n = src.length, last = src[n - 1];
    var w = [1, n <= 8 ? 1.1 : 0, n >= 4 ? 1 : 0, last.duration >= 2 && n < 16 ? 1.2 : 0];
    var k = R.pickIndex(w, R.stream(o.seed, 0, R.P_BRHYTHM).next());
    return RHYTHMS[k < 0 ? 0 : k];
  }

  // Regions: a centre (degree of a vocabulary mode on the same tonic) and
  // the vocabulary the walk uses. With Chromatic at 0 the vocabulary is the
  // source mode, so the B section stays diatonic.
  function regionsFor(o) {
    var c = o.chromatic / 100, fam = T.MODES[o.mode].family, out = [];
    var home = o.mode;
    if (fam === 'major') {
      out.push({ id: 'vi', vocab: home, center: 5, w: 3 });
      out.push({ id: 'IV', vocab: home, center: 3, w: 2.5 });
      out.push({ id: 'ii', vocab: home, center: 1, w: 1 });
      if (c > 0) { out.push({ id: 'bVI', vocab: T.modeIndexById('minor'), center: 5, w: 2 * c }); }
    } else {
      out.push({ id: 'III', vocab: home, center: 2, w: 3 });
      out.push({ id: 'iv', vocab: home, center: 3, w: 2 });
      out.push({ id: 'VI', vocab: home, center: 5, w: 1.5 });
      if (c > 0) {
        out.push({ id: 'IV', vocab: T.modeIndexById('dorian'), center: 3, w: 1.5 * c });
        out.push({ id: 'bII', vocab: T.modeIndexById('phrygian'), center: 1, w: 0.8 * c });
      }
    }
    var keep = [];
    for (var i = 0; i < out.length; i++) {
      if (!weird(C.diatonic(out[i].vocab, out[i].center))) { keep.push(out[i]); }
    }
    return keep;
  }

  // Chord r steps above the region centre. Chords from a borrowed vocabulary
  // that leave the home scale are marked 'borrowed' (shown as such).
  function regionChord(region, r, homeMode) {
    var deg = (region.center + r) % 7;
    var ch = C.diatonic(region.vocab, deg, { degree: deg, func: G.FUNC[deg] });
    if (region.vocab !== homeMode && !inScale(ch, homeMode)) { ch.source = 'borrowed'; }
    return ch;
  }

  // Chords that lead back to target (the source's first chord).
  function endingOptions(o, target) {
    var c = o.chromatic / 100, opts = [], w = [];
    function dia(interval, weight, allowDim) {
      var d = degreeOf(o.mode, target.root + interval);
      if (d < 0) { return null; }
      var ch = C.diatonic(o.mode, d, { degree: d, func: G.FUNC[d] });
      if (weird(ch) && !allowDim) { return null; }
      if (sameChord(ch, target)) { return null; }
      opts.push(ch); w.push(weight + (interval === 7 && ch.third === 4 ? 1 : 0));
      return ch;
    }
    var dom = dia(7, 3, false);       // V of the opening chord
    dia(5, 1.5, false);               // plagal: IV of it
    dia(2, 1, false);                 // ii of it
    var lt = degreeOf(o.mode, target.root + 11);
    if (lt >= 0 && C.isDiminished(C.diatonic(o.mode, lt))) { dia(11, 0.8, true); }
    if (c > 0 && !weird(target)) {
      if (!dom || dom.third !== 4) { opts.push(G._secondaryDominant(target)); w.push(3 * c); }
      var bv = C.fromScale(target.root + 10, target.letter + 6, C.SCALES.mixolydian, { degree: 6, func: 'D', source: 'borrowed' });
      if (degreeOf(o.mode, bv.root) < 0 || !inScale(bv, o.mode)) { opts.push(bv); w.push(1.5 * c); }
    }
    return { opts: opts, w: w };
  }

  function bSection(analysis, o, locks, keep) {
    var src = A.toChords(analysis, o.root, o.mode), srcN = src.length, i, d;
    var target = src[0];
    var slots, rhythm, lengthBeats;
    if (o.length === srcN) {
      rhythm = (keep && keep.rhythm && keep.rhythm !== 'uniform') ? keep.rhythm : pickRhythm(analysis, o);
      slots = bSlots(analysis, rhythm);
      lengthBeats = analysis.lengthBeats;
    } else {
      rhythm = 'uniform';
      slots = uniformSlots(o.length, DURATION_BEATS[o.duration]);
      lengthBeats = endOf(slots);
    }
    var n = slots.length;
    var L = prepareLocks(locks, n);

    var regions = regionsFor(o), rw = [];
    for (i = 0; i < regions.length; i++) { rw.push(regions[i].w); }
    var region = regions[Math.max(0, R.pickIndex(rw, R.stream(o.seed, 0, R.P_BREGION).next()))];

    var chords = [], prevR = -1, r;
    // Slot 0: the region centre, or a pre-dominant of the region.
    var s0w = [3, 0.8, 0, 1, 0, 0, 0];
    // Middle slots: Markov walk over region degrees, away from the home tonic.
    var endPlan = endingOptions(o, target);
    var endChoice = R.pickIndex(endPlan.w, R.stream(o.seed, n - 1, R.P_BEND).next());
    var finalChord = endChoice >= 0 ? endPlan.opts[endChoice] : C.diatonic(o.mode, 4, { degree: 4, func: 'D' });
    for (i = 0; i < n; i++) {
      if (L.locked[i]) { chords.push(C.copy(L.lk[i])); prevR = -1; continue; }
      if (i === n - 1 && n > 1) { chords.push(C.copy(finalChord)); continue; }
      var w = [];
      for (r = 0; r < 7; r++) {
        var ch = regionChord(region, r, o.mode);
        var base = (i === 0) ? s0w[r] : (prevR >= 0 ? G.MATRIX[prevR][r] : 1);
        if (weird(ch)) { base = 0; }
        if (ch.root === 0 && T.MODES[region.vocab].intervals[ch.degree] === 0) { base *= 0.35; }
        if (i > 0 && sameChord(ch, chords[i - 1])) { base = 0; }
        if (i === n - 2 && !L.locked[n - 1]) {
          if (sameChord(ch, finalChord)) { base = 0; }
          // Lean towards a chord that moves to the final chord by a falling
          // fifth or a step, so the return reads as a cadence.
          var toFinal = T.mod12(ch.root - finalChord.root);
          base *= (toFinal === 7 || toFinal === 2 || toFinal === 10) ? 1.6 : 1;
        }
        w.push(base);
      }
      var u = R.stream(o.seed, i, R.P_BWALK).next();
      r = R.pickIndex(w, u);
      if (r < 0) { r = 0; }
      chords.push(regionChord(region, r, o.mode));
      prevR = r;
    }
    // Light chromatic colour inside the B section.
    var c = o.chromatic / 100;
    if (c > 0) {
      for (i = n - 2; i >= 1; i--) {
        if (L.locked[i]) { continue; }
        var s = R.stream(o.seed, i, R.P_BCHROM);
        var u1 = s.next(), u2 = s.next(), u3 = s.next();
        if (u1 >= 0.4 * c) { continue; }
        var nx = chords[i + 1];
        var opts = [], ow = [];
        if (!weird(nx) && !(chords[i].root === T.mod12(nx.root + 7) && chords[i].third === 4)) { opts.push(G._secondaryDominant(nx)); ow.push(1.2); }
        var dd = degreeOf(o.mode, chords[i].root);
        if (dd >= 0) { var b = G._borrowedChord(o, dd, u3); if (b && !sameChord(b, chords[i - 1]) && !sameChord(b, nx)) { opts.push(b); ow.push(1); } }
        var k = R.pickIndex(ow, u2);
        if (k >= 0) { chords[i] = opts[k]; }
      }
    }
    G._colorize(o, chords, L.locked);
    G._invert(o, chords, L.locked, 'turnaround');
    var changed = [];
    for (i = 0; i < n; i++) { changed.push(false); }
    var centre = C.diatonic(region.vocab, region.center);
    return {
      chords: chords, slots: slots, lengthBeats: lengthBeats, ending: 'return', changed: changed, rhythm: rhythm,
      region: { id: region.id, root: T.mod12(o.root + centre.root), minor: centre.third === 3, chord: centre }
    };
  }

  function derive(analysis, o, kind, locks, keep) {
    if (!analysis || !analysis.segments || !analysis.segments.length) { throw new Error('No source clip is loaded.'); }
    return kind === 'b' ? bSection(analysis, o, locks, keep) : variation(analysis, o, locks);
  }

  return { derive: derive, RHYTHMS: RHYTHMS, regionsFor: regionsFor, endingOptions: endingOptions };
})();
