// dooblr-chords engine: functional progression generator (ES5).
//
// Pipeline: degrees (weighted Markov, T -> PD -> D) -> chromatic substitutions
// -> colour (extensions) -> inversions. Every slot draws from its own seeded
// stream, so locking one chord or nudging a knob never reshuffles the others.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.generator = (function () {
  var T = DC.theory, C = DC.chords, R = DC.rng;

  var FUNC = ['T', 'PD', 'T', 'PD', 'D', 'T', 'D'];

  // Base transition weights between diatonic degrees (rows: from, cols: to),
  // in the spirit of common-practice tendencies (T -> PD -> D -> T, V -> vi deceptive).
  var MATRIX = [
    [0.0, 2.0, 1.0, 3.0, 3.0, 2.5, 0.5],
    [0.5, 0.0, 0.3, 1.0, 4.0, 0.5, 1.5],
    [0.5, 1.0, 0.0, 2.5, 0.5, 3.0, 0.1],
    [2.0, 1.5, 0.5, 0.0, 3.5, 0.7, 1.0],
    [4.0, 0.3, 0.5, 1.0, 0.0, 2.0, 0.1],
    [0.7, 3.0, 0.5, 3.0, 1.5, 0.0, 0.3],
    [4.0, 0.1, 0.7, 0.1, 1.0, 0.5, 0.0]
  ];

  // Per-mode multipliers on arriving at each degree (characteristic chords up,
  // diminished triads down), the cadence chords for each mode, and which modes
  // to borrow from.
  var MODE_RULES = {
    major:         { factor: [1, 1, 0.8, 1, 1, 1, 0.25], cadence: [0, 0, 0, 1, 3, 0, 0.4], borrow: [['minor', 3], ['mixolydian', 1], ['dorian', 0.5]] },
    minor:         { factor: [1, 0.3, 1.1, 1.2, 0.8, 1.5, 1.5], cadence: [0, 0, 0, 1, 1.5, 0, 2], borrow: [['harmonicMinor', 1.5], ['major', 1.5], ['dorian', 1]] },
    dorian:        { factor: [1, 1.2, 1, 2, 0.8, 0.25, 1.3], cadence: [0, 0, 0, 2.5, 0.8, 0, 1.5], borrow: [['minor', 1.5], ['harmonicMinor', 1], ['major', 0.7]] },
    phrygian:      { factor: [1, 2.5, 1, 1, 0.25, 1, 1.5], cadence: [0, 3, 0, 0, 0, 0, 1.5], borrow: [['minor', 1.5], ['harmonicMinor', 1]] },
    lydian:        { factor: [1, 2.5, 1, 0.25, 1, 1, 0.8], cadence: [0, 3, 0, 0, 1, 0, 0], borrow: [['major', 1.5], ['minor', 1], ['mixolydian', 0.7]] },
    mixolydian:    { factor: [1, 1, 0.25, 1.3, 0.8, 1, 2.5], cadence: [0, 0, 0, 1.5, 0.8, 0, 3], borrow: [['minor', 1.5], ['major', 1], ['dorian', 0.7]] },
    harmonicMinor: { factor: [1, 0.3, 0.3, 1.2, 1.5, 1.2, 0.8], cadence: [0, 0, 0, 0, 3, 0, 1], borrow: [['minor', 1.5], ['major', 1], ['dorian', 1]] }
  };

  var START_ALTERNATIVES = [0, 0.6, 0.2, 1.2, 0.3, 1.5, 0];

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  function normalizeOptions(o) {
    return {
      root: T.mod12(Math.round(o.root || 0)),
      mode: clamp(Math.round(o.mode || 0), 0, T.MODES.length - 1),
      length: clamp(Math.round(o.length || 4), 2, 16),
      color: clamp(+o.color || 0, 0, 100),
      chromatic: clamp(+o.chromatic || 0, 0, 100),
      seed: (Math.round(o.seed || 1) >>> 0)
    };
  }

  function rulesFor(modeIdx) { return MODE_RULES[T.MODES[modeIdx].id]; }

  // ---- 1. Degrees --------------------------------------------------------
  function planDegrees(o, locks) {
    var n = o.length, rules = rulesFor(o.mode), i, d;
    var ending = (n >= 3 && R.stream(o.seed, 0, R.P_ENDING).next() < 0.55) ? 'authentic' : 'turnaround';
    var bias = [];
    for (i = 0; i < n; i++) { bias.push([1, 1, 1, 1, 1, 1, 1]); }

    // Start: usually on the tonic.
    var startTonic = (n <= 2) || R.stream(o.seed, 0, R.P_START).next() < 0.85;
    for (d = 0; d < 7; d++) {
      var dimStart = C.isDiminished(C.diatonic(o.mode, d)) || C.isAugmented(C.diatonic(o.mode, d));
      bias[0][d] = startTonic ? (d === 0 ? 1 : 0) : (dimStart ? 0 : START_ALTERNATIVES[d] * rules.factor[d]);
    }
    // Ending: authentic = cadence chord then tonic; turnaround = end on the cadence chord.
    var cadSlot = ending === 'authentic' ? n - 2 : n - 1;
    if (cadSlot > 0) {
      for (d = 0; d < 7; d++) { bias[cadSlot][d] *= rules.cadence[d] > 0 ? 1 + 4 * rules.cadence[d] : 0.15; }
    }
    if (ending === 'authentic') {
      for (d = 0; d < 7; d++) { bias[n - 1][d] *= (d === 0) ? 30 : 0.2; }
    }

    var degrees = [], prev = -1;
    for (i = 0; i < n; i++) {
      if (locks[i]) { degrees.push(locks[i].degree); prev = locks[i].degree; continue; }
      var next = (i + 1 < n && locks[i + 1]) ? locks[i + 1].degree : -1;
      var w = [];
      for (d = 0; d < 7; d++) {
        var base = (i === 0) ? 1 : MATRIX[prev][d] * rules.factor[d];
        var look = (next >= 0) ? (MATRIX[d][next] + 0.15) : 1;
        w.push((d === prev) ? 0 : base * bias[i][d] * look);
      }
      var u = R.stream(o.seed, i, R.P_DEGREE).next();
      var pick = R.pickIndex(w, u);
      if (pick < 0) { // every option ruled out: fall back to any change of degree
        for (d = 0; d < 7; d++) { w[d] = (d === prev) ? 0 : 1; }
        pick = R.pickIndex(w, u);
      }
      degrees.push(pick);
      prev = pick;
    }
    return { degrees: degrees, ending: ending };
  }

  // ---- 2. Chromatic substitutions -----------------------------------------
  function borrowedChord(o, d, u) {
    var rules = rulesFor(o.mode), base = C.diatonic(o.mode, d), cands = [], w = [], i;
    for (i = 0; i < rules.borrow.length; i++) {
      var m = T.modeIndexById(rules.borrow[i][0]);
      var c = C.diatonic(m, d, { degree: d, func: FUNC[d], source: 'borrowed' });
      var differs = c.root !== base.root || c.third !== base.third || c.fifth !== base.fifth;
      if (differs && !C.isDiminished(c) && !C.isAugmented(c)) { cands.push(c); w.push(rules.borrow[i][1]); }
    }
    var k = R.pickIndex(w, u);
    return k < 0 ? null : cands[k];
  }

  function secondaryDominant(next) {
    var scale = C.isMinor(next) ? C.SCALES.phrygianDominant : C.SCALES.mixolydian;
    return C.fromScale(next.root + 7, next.letter + 4, scale,
      { degree: 4, func: 'D', source: 'secondary', targetRoman: C.baseRoman(next) });
  }

  function tritoneSub(next) {
    return C.fromScale(next.root + 1, next.letter + 1, C.SCALES.lydianDominant,
      { degree: 4, func: 'D', source: 'tritone', targetRoman: C.baseRoman(next) });
  }

  function neapolitan() {
    return C.fromScale(1, 1, C.SCALES.lydian, { degree: 1, func: 'PD', source: 'neapolitan' });
  }

  function substitute(o, chords, locked, ending) {
    var n = chords.length, c = o.chromatic / 100, i;
    if (c <= 0) { return; }
    var modeIv = T.MODES[o.mode].intervals;
    var dominantIsMajor = C.diatonic(o.mode, 4).third === 4;
    var modeId = T.MODES[o.mode].id;
    var allowNeapolitan = modeIv[1] === 2 && (modeId === 'major' || T.MODES[o.mode].family === 'minor');
    for (i = n - 1; i >= 0; i--) {
      if (locked[i]) { continue; }
      var s = R.stream(o.seed, i, R.P_CHROMATIC);
      var u1 = s.next(), u2 = s.next(), u3 = s.next();
      if (u1 >= 0.5 * c) { continue; }
      var cur = chords[i];
      // Keep the opening chord and a final authentic tonic as they are.
      if (i === 0 || (i === n - 1 && ending === 'authentic')) { continue; }
      var next = (i + 1 < n) ? chords[i + 1] : chords[0];
      var nextPlain = next.source === 'diatonic' || next.source === 'borrowed';
      var opts = [], w = [];

      var b = borrowedChord(o, cur.degree, u3);
      if (b) { opts.push(b); w.push(1.0); }
      if (!C.isDiminished(next) && !C.isAugmented(next)) {
        var targetsTonic = next.root === 0;
        if ((!targetsTonic || !dominantIsMajor) && next.source !== 'tritone') {
          if (!(cur.root === T.mod12(next.root + 7) && cur.third === 4)) {
            opts.push(secondaryDominant(next)); w.push(1.2);
          }
        }
        if (nextPlain) { opts.push(tritoneSub(next)); w.push(1.2 * c); }
        var nextIsDominantish = next.func === 'D' || next.root === 0;
        if (allowNeapolitan && cur.func === 'PD' && nextIsDominantish) {
          opts.push(neapolitan()); w.push(1.0 * c + 0.3);
        }
      }
      var k = R.pickIndex(w, u2);
      if (k >= 0) { chords[i] = opts[k]; }
    }
  }

  // ---- 3. Colour ------------------------------------------------------------
  function colorize(o, chords, locked) {
    var L = o.color / 100 * 4, c = o.chromatic / 100;
    for (var i = 0; i < chords.length; i++) {
      if (locked[i]) { continue; }
      var s = R.stream(o.seed, i, R.P_COLOR);
      var uLevel = s.next(), uVariant = s.next(), uAlter = s.next(), uKind = s.next();
      var level = Math.min(4, Math.floor(L + uLevel));
      if (chords[i].source === 'neapolitan') { level = Math.min(level, 1); }
      C.applyColor(chords[i], level, uVariant);
      var functional = chords[i].func === 'D' || chords[i].source === 'secondary' || chords[i].source === 'tritone';
      if (c > 0 && functional && C.isDominant(chords[i]) && chords[i].nine !== null && uAlter < 0.6 * c) {
        var nxt = chords[(i + 1) % chords.length];
        C.alterDominant(chords[i], uKind, C.isMinor(nxt));
      }
    }
  }

  // ---- 4. Inversions --------------------------------------------------------
  function bassPc(ch) { return T.mod12(ch.root + C.bassInterval(ch)); }
  function stepDist(a, b) { var d = T.mod12(a - b); return Math.min(d, 12 - d); }

  function invert(o, chords, locked, ending) {
    var n = chords.length;
    for (var i = 1; i < n; i++) {
      if (locked[i]) { continue; }
      var ch = chords[i];
      ch.slash = null;
      var u = R.stream(o.seed, i, R.P_INVERSION).next();
      if (ch.third === null) { continue; }
      if (i === n - 1 && ending === 'authentic') { continue; }
      if (ch.source === 'neapolitan') { if (u < 0.75) { ch.slash = 'third'; } continue; }
      var thirdPc = T.mod12(ch.root + ch.third);
      var prevStep = stepDist(thirdPc, bassPc(chords[i - 1]));
      var nextStep = (i + 1 < n) ? stepDist(thirdPc, chords[i + 1].root) : 0;
      var stepwise = (prevStep === 1 || prevStep === 2) || (nextStep === 1 || nextStep === 2);
      if (u < (stepwise ? 0.35 : 0.06)) { ch.slash = 'third'; }
    }
  }

  // Public: generate a progression. locks is an optional array of chords
  // (or null) by slot; locked chords are returned unchanged.
  function generate(options, locks) {
    var o = normalizeOptions(options), n = o.length, i;
    var lk = [], locked = [];
    for (i = 0; i < n; i++) {
      var l = (locks && locks[i]) ? locks[i] : null;
      lk.push(l); locked.push(!!l);
    }
    var plan = planDegrees(o, lk);
    var chords = [];
    for (i = 0; i < n; i++) {
      chords.push(locked[i] ? C.copy(lk[i]) : C.diatonic(o.mode, plan.degrees[i], { degree: plan.degrees[i], func: FUNC[plan.degrees[i]] }));
    }
    substitute(o, chords, locked, plan.ending);
    colorize(o, chords, locked);
    invert(o, chords, locked, plan.ending);
    return { options: o, chords: chords, ending: plan.ending, degrees: plan.degrees };
  }

  return {
    FUNC: FUNC,
    MATRIX: MATRIX,
    MODE_RULES: MODE_RULES,
    normalizeOptions: normalizeOptions,
    generate: generate
  };
})();
