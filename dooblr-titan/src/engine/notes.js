// dooblr-titan engine: clip-note helpers (ES5, host independent).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.notes = (function () {
  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
  function round4(x) { return Math.round(x * 10000) / 10000; }

  function startOf(n) {
    if (n.start !== undefined && n.start !== null) { return +n.start; }
    return +(n.start_time || 0);
  }

  function copyOne(n) {
    return {
      pitch: Math.round(+n.pitch),
      start: round4(startOf(n)),
      duration: round4(+n.duration > 0 ? +n.duration : 0.0625),
      velocity: clamp(Math.round(+n.velocity || 100), 1, 127),
      mute: n.mute ? 1 : 0,
      channel: n.channel === undefined ? 0 : (+n.channel || 0)
    };
  }

  function copyAll(notes) {
    var out = [], i;
    for (i = 0; i < (notes || []).length; i++) {
      if (notes[i] && !notes[i].mute) { out.push(copyOne(notes[i])); }
    }
    return out;
  }

  function sort(notes) {
    notes.sort(function (a, b) {
      return a.start - b.start || a.pitch - b.pitch || a.duration - b.duration;
    });
    return notes;
  }

  function lengthBeats(notes, given) {
    if (+given > 0) { return +given; }
    var maxEnd = 0, i;
    for (i = 0; i < notes.length; i++) {
      var e = notes[i].start + notes[i].duration;
      if (e > maxEnd) { maxEnd = e; }
    }
    return Math.max(4, Math.ceil(maxEnd / 4 - 1e-6) * 4);
  }

  function toLive(notes) {
    var out = [], i;
    for (i = 0; i < notes.length; i++) {
      out.push({
        pitch: notes[i].pitch,
        start_time: notes[i].start,
        duration: notes[i].duration,
        velocity: notes[i].velocity,
        mute: notes[i].mute ? 1 : 0
      });
    }
    return out;
  }

  function same(a, b, eps) {
    eps = eps || 1e-4;
    if (a.length !== b.length) { return false; }
    var i;
    for (i = 0; i < a.length; i++) {
      if (a[i].pitch !== b[i].pitch) { return false; }
      if (Math.abs(a[i].start - b[i].start) > eps) { return false; }
      if (Math.abs(a[i].duration - b[i].duration) > eps) { return false; }
      if (a[i].velocity !== b[i].velocity) { return false; }
    }
    return true;
  }

  function fingerprint(notes) {
    var s = '', i;
    for (i = 0; i < notes.length; i++) {
      s += notes[i].pitch + '@' + round4(notes[i].start) + ':' + round4(notes[i].duration) + 'v' + notes[i].velocity + ';';
    }
    return s;
  }

  return {
    clamp: clamp,
    round4: round4,
    startOf: startOf,
    copyOne: copyOne,
    copyAll: copyAll,
    sort: sort,
    lengthBeats: lengthBeats,
    toLive: toLive,
    same: same,
    fingerprint: fingerprint
  };
})();
