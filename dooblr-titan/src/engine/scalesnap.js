// dooblr-titan engine: snap pitches onto a scale (ES5).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.scalesnap = (function () {
  var T = DT.theory, N = DT.notes;

  function apply(notes, root, mode) {
    var src = N.sort(N.copyAll(notes)), i;
    root = T.mod12(Math.round(+root || 0));
    mode = N.clamp(Math.round(+mode || 0), 0, T.MODES.length - 1);
    for (i = 0; i < src.length; i++) {
      src[i].pitch = T.nearestInScale(src[i].pitch, root, mode);
    }
    return src;
  }

  return { apply: apply };
})();
