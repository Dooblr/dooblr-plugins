// dooblr-titan engine: quantize with grid (incl. triplets), swing, and
// groove-preserving blend (ES5, host independent).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.quantize = (function () {
  var N = DT.notes;

  var GRIDS = [
    { id: '1/4', label: '1/4', beats: 1 },
    { id: '1/8', label: '1/8', beats: 0.5 },
    { id: '1/8T', label: '1/8T', beats: 1 / 3 },
    { id: '1/16', label: '1/16', beats: 0.25 },
    { id: '1/16T', label: '1/16T', beats: 1 / 6 },
    { id: '1/32', label: '1/32', beats: 0.125 },
    { id: '1/32T', label: '1/32T', beats: 1 / 12 }
  ];

  function gridBeats(index) {
    var i = Math.round(+index || 0);
    if (i < 0) { i = 0; }
    if (i >= GRIDS.length) { i = GRIDS.length - 1; }
    return GRIDS[i].beats;
  }

  function median(arr) {
    if (!arr.length) { return 0; }
    var s = arr.slice(0).sort(function (a, b) { return a - b; });
    var m = Math.floor(s.length / 2);
    return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
  }

  // Straight grid index of a time. 0,2,4... are onbeats; 1,3,5... offbeats.
  function gridIndex(t, g) {
    return Math.round(t / g);
  }

  function straightTime(index, g) { return index * g; }

  // 100% swing delays the offbeat by g/3, putting it on the last triplet of the pair.
  function swingDelay(index, g, swing) {
    if (index % 2 === 0) { return 0; }
    return (N.clamp(+swing || 0, 0, 100) / 100) * (g / 3);
  }

  function snappedTime(t, g, swing) {
    var i = gridIndex(t, g);
    var s = straightTime(i, g) + swingDelay(i, g, swing);
    return s < 0 ? 0 : s;
  }

  function apply(notes, opts) {
    opts = opts || {};
    var g = opts.gridBeats > 0 ? opts.gridBeats : gridBeats(opts.grid);
    var strength = N.clamp(+opts.strength || 0, 0, 100) / 100;
    var swing = N.clamp(+opts.swing || 0, 0, 100);
    var grooveOn = opts.groove ? 1 : 0;
    var src = N.sort(N.copyAll(notes));
    if (!src.length || strength <= 0) { return src; }

    var evenOff = [], oddOff = [], i, idx, off;
    var inlier = g * 0.25;
    for (i = 0; i < src.length; i++) {
      idx = gridIndex(src[i].start, g);
      off = src[i].start - straightTime(idx, g);
      if (Math.abs(off) > inlier) { continue; }
      if (idx % 2 === 0) { evenOff.push(off); } else { oddOff.push(off); }
    }
    var evenMed = grooveOn ? median(evenOff) : 0;
    var oddMed = grooveOn ? median(oddOff) : 0;

    for (i = 0; i < src.length; i++) {
      idx = gridIndex(src[i].start, g);
      var groove = (idx % 2 === 0) ? evenMed : oddMed;
      var target = straightTime(idx, g) + swingDelay(idx, g, swing) + groove;
      if (target < 0) { target = 0; }
      src[i].start = N.round4(src[i].start + strength * (target - src[i].start));
      if (src[i].start < 0) { src[i].start = 0; }
    }
    return N.sort(src);
  }

  return {
    GRIDS: GRIDS,
    gridBeats: gridBeats,
    snappedTime: snappedTime,
    swingDelay: swingDelay,
    apply: apply
  };
})();
