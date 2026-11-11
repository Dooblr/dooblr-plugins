// dooblr-titan engine: key detection from duration-weighted pitch classes (ES5).
// Krumhansl-Kessler profiles, then the church mode in that family that fits best.
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.keydetect = (function () {
  var T = DT.theory, N = DT.notes;
  var PROFILE_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  var PROFILE_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  var FAMILY_MODES = { major: [0, 4, 5], minor: [1, 2, 3, 6] };

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

  function histogram(notes) {
    var h = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], i;
    for (i = 0; i < notes.length; i++) {
      var n = notes[i];
      if (n.mute) { continue; }
      var d = +n.duration || 0;
      if (!(d > 0)) { d = 0.0625; }
      h[T.mod12(Math.round(n.pitch))] += d;
    }
    return h;
  }

  function detect(notes) {
    var src = N.copyAll(notes);
    if (!src.length) { throw new Error('The clip has no notes.'); }
    var hist = histogram(src), total = 0, i, r;
    for (i = 0; i < 12; i++) { total += hist[i]; }
    if (!(total > 0)) { throw new Error('The clip has no notes.'); }

    var best = null;
    for (r = 0; r < 12; r++) {
      var sMaj = correlate(hist, PROFILE_MAJOR, r);
      var sMin = correlate(hist, PROFILE_MINOR, r);
      if (best === null || sMaj > best.score + 1e-9) { best = { root: r, family: 'major', score: sMaj }; }
      if (sMin > best.score + 1e-9) { best = { root: r, family: 'minor', score: sMin }; }
    }

    var cands = FAMILY_MODES[best.family], bestMode = cands[0], bestFit = -1e9;
    for (i = 0; i < cands.length; i++) {
      var iv = T.MODES[cands[i]].intervals, fitv = 0, pc;
      for (pc = 0; pc < 12; pc++) {
        var inS = false, k;
        for (k = 0; k < iv.length; k++) { if (T.mod12(best.root + iv[k]) === pc) { inS = true; } }
        fitv += inS ? hist[pc] : -hist[pc];
      }
      if (i > 0) { fitv -= 0.04 * total; }
      if (fitv > bestFit + 1e-9) { bestFit = fitv; bestMode = cands[i]; }
    }
    return {
      root: best.root,
      mode: bestMode,
      score: Math.round(best.score * 1000) / 1000,
      name: T.keyName(best.root, bestMode)
    };
  }

  return { histogram: histogram, detect: detect, PROFILE_MAJOR: PROFILE_MAJOR, PROFILE_MINOR: PROFILE_MINOR };
})();
