// dooblr-titan engine: velocity compression toward a target range, plus humanize (ES5).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.velocity = (function () {
  var N = DT.notes, R = DT.rng;

  function apply(notes, opts) {
    opts = opts || {};
    var src = N.sort(N.copyAll(notes));
    if (!src.length) { return src; }
    var lo = N.clamp(Math.round(+opts.velmin || 1), 1, 127);
    var hi = N.clamp(Math.round(+opts.velmax || 127), 1, 127);
    if (hi < lo) { var t = lo; lo = hi; hi = t; }
    var comp = N.clamp(+opts.velcomp || 0, 0, 100) / 100;
    var hum = N.clamp(+opts.humanize || 0, 0, 100) / 100;
    var seed = Math.round(+opts.seed || 1);

    var minV = 127, maxV = 1, i;
    for (i = 0; i < src.length; i++) {
      if (src[i].velocity < minV) { minV = src[i].velocity; }
      if (src[i].velocity > maxV) { maxV = src[i].velocity; }
    }
    var span = maxV - minV;

    for (i = 0; i < src.length; i++) {
      var v = src[i].velocity, mapped;
      if (span <= 0) {
        mapped = Math.round((lo + hi) / 2);
      } else {
        mapped = lo + (v - minV) * (hi - lo) / span;
      }
      v = v + comp * (mapped - v);
      if (hum > 0) {
        var rng = R.stream(seed, i, R.P_HUMANIZE);
        // ± up to 16 velocity at humanize 100, seeded per note.
        v += rng.signed() * 16 * hum;
      }
      src[i].velocity = N.clamp(Math.round(v), 1, 127);
    }
    return src;
  }

  return { apply: apply };
})();
