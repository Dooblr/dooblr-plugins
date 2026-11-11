// dooblr-titan engine: flam/overlap fix, tiny-note removal, legato tidy (ES5).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.cleanup = (function () {
  var N = DT.notes;
  var FLAM_WINDOW = 0.04; // beats: onsets this close on the same pitch are a flam
  var EPS = 0.001;

  function byPitch(notes) {
    var map = {}, i, p;
    for (i = 0; i < notes.length; i++) {
      p = notes[i].pitch;
      if (!map[p]) { map[p] = []; }
      map[p].push(notes[i]);
    }
    var keys = [];
    for (p in map) { if (map.hasOwnProperty(p)) { keys.push(+p); } }
    keys.sort(function (a, b) { return a - b; });
    return { map: map, keys: keys };
  }

  // Drop notes shorter than minBeats or quieter than minVel.
  function removeTiny(notes, minBeats, minVel) {
    var out = [], i;
    for (i = 0; i < notes.length; i++) {
      if (notes[i].duration + 1e-9 < minBeats) { continue; }
      if (notes[i].velocity < minVel) { continue; }
      out.push(notes[i]);
    }
    return out;
  }

  // Same-pitch onsets inside FLAM_WINDOW: keep the louder (then longer) note.
  function fixFlams(notes) {
    var grouped = byPitch(notes), out = [], k, i;
    for (k = 0; k < grouped.keys.length; k++) {
      var list = grouped.map[grouped.keys[k]];
      list.sort(function (a, b) { return a.start - b.start; });
      var kept = [];
      for (i = 0; i < list.length; i++) {
        var n = list[i], last = kept.length ? kept[kept.length - 1] : null;
        if (last && n.start - last.start <= FLAM_WINDOW) {
          var nScore = n.velocity * n.duration, lScore = last.velocity * last.duration;
          if (nScore > lScore + 1e-9 || (Math.abs(nScore - lScore) <= 1e-9 && n.duration > last.duration)) {
            kept[kept.length - 1] = n;
          }
        } else {
          kept.push(n);
        }
      }
      for (i = 0; i < kept.length; i++) { out.push(kept[i]); }
    }
    return N.sort(out);
  }

  // Same-pitch overlaps: trim the earlier note so it ends at the later start.
  function trimOverlaps(notes) {
    var grouped = byPitch(notes), out = [], k, i;
    for (k = 0; k < grouped.keys.length; k++) {
      var list = grouped.map[grouped.keys[k]];
      list.sort(function (a, b) { return a.start - b.start; });
      for (i = 0; i < list.length - 1; i++) {
        var end = list[i].start + list[i].duration;
        if (end > list[i + 1].start + 1e-9) {
          list[i].duration = N.round4(Math.max(EPS, list[i + 1].start - list[i].start - EPS));
        }
      }
      for (i = 0; i < list.length; i++) { out.push(list[i]); }
    }
    return N.sort(out);
  }

  // Close small same-pitch gaps up to maxGap beats by lengthening the earlier note.
  function closeGaps(notes, maxGap) {
    if (!(maxGap > 0)) { return notes; }
    var grouped = byPitch(notes), out = [], k, i;
    for (k = 0; k < grouped.keys.length; k++) {
      var list = grouped.map[grouped.keys[k]];
      list.sort(function (a, b) { return a.start - b.start; });
      for (i = 0; i < list.length - 1; i++) {
        var gap = list[i + 1].start - (list[i].start + list[i].duration);
        if (gap > EPS && gap <= maxGap + 1e-9) {
          list[i].duration = N.round4(list[i + 1].start - list[i].start - EPS);
        }
      }
      for (i = 0; i < list.length; i++) { out.push(list[i]); }
    }
    return N.sort(out);
  }

  // minlen 0..100 maps to 0..0.25 beats (a sixteenth). gapclose 0..100 maps to 0..0.5 beats.
  function minBeatsFrom(minlen) { return N.clamp(+minlen || 0, 0, 100) / 100 * 0.25; }
  function gapBeatsFrom(gapclose) { return N.clamp(+gapclose || 0, 0, 100) / 100 * 0.5; }

  function apply(notes, opts) {
    opts = opts || {};
    var src = N.sort(N.copyAll(notes));
    var minB = opts.minBeats !== undefined ? +opts.minBeats : minBeatsFrom(opts.minlen);
    var minV = opts.minVel !== undefined ? Math.round(+opts.minVel) : Math.round(+opts.minvel || 1);
    src = removeTiny(src, minB, minV);
    if (opts.flams) { src = fixFlams(src); }
    if (opts.legato) {
      src = trimOverlaps(src);
      src = closeGaps(src, opts.gapBeats !== undefined ? +opts.gapBeats : gapBeatsFrom(opts.gapclose));
    } else if (opts.flams) {
      src = trimOverlaps(src);
    }
    src = removeTiny(src, minB, minV);
    return src;
  }

  return {
    FLAM_WINDOW: FLAM_WINDOW,
    removeTiny: removeTiny,
    fixFlams: fixFlams,
    trimOverlaps: trimOverlaps,
    closeGaps: closeGaps,
    minBeatsFrom: minBeatsFrom,
    gapBeatsFrom: gapBeatsFrom,
    apply: apply
  };
})();
