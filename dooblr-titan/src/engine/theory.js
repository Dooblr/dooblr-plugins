// dooblr-titan engine: scales, modes and note spelling (ES5).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.theory = (function () {
  var SHARP_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  var FLAT_NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
  var ROOT_MENU = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];

  var MODES = [
    { id: 'major', name: 'Major', short: 'maj', family: 'major', parentOffset: 0, intervals: [0, 2, 4, 5, 7, 9, 11] },
    { id: 'minor', name: 'Natural Minor', short: 'min', family: 'minor', parentOffset: 9, intervals: [0, 2, 3, 5, 7, 8, 10] },
    { id: 'dorian', name: 'Dorian', short: 'dor', family: 'minor', parentOffset: 2, intervals: [0, 2, 3, 5, 7, 9, 10] },
    { id: 'phrygian', name: 'Phrygian', short: 'phr', family: 'minor', parentOffset: 4, intervals: [0, 1, 3, 5, 7, 8, 10] },
    { id: 'lydian', name: 'Lydian', short: 'lyd', family: 'major', parentOffset: 5, intervals: [0, 2, 4, 6, 7, 9, 11] },
    { id: 'mixolydian', name: 'Mixolydian', short: 'mix', family: 'major', parentOffset: 7, intervals: [0, 2, 4, 5, 7, 9, 10] },
    { id: 'harmonicMinor', name: 'Harmonic Minor', short: 'hmin', family: 'minor', parentOffset: 9, intervals: [0, 2, 3, 5, 7, 8, 11] }
  ];

  function mod12(n) { return ((n % 12) + 12) % 12; }

  function keyUsesFlats(rootPc, modeIdx) {
    var parent = mod12(rootPc - MODES[modeIdx].parentOffset);
    return parent === 5 || parent === 10 || parent === 3 || parent === 8 || parent === 1;
  }

  function keyNameAscii(rootPc, modeIdx) {
    return keyUsesFlats(rootPc, modeIdx) ? FLAT_NAMES[mod12(rootPc)] : SHARP_NAMES[mod12(rootPc)];
  }

  function keyName(rootPc, modeIdx) {
    return keyNameAscii(rootPc, modeIdx) + ' ' + MODES[modeIdx].name;
  }

  function scalePcs(rootPc, modeIdx) {
    var iv = MODES[modeIdx].intervals, out = [], i;
    for (i = 0; i < iv.length; i++) { out.push(mod12(rootPc + iv[i])); }
    return out;
  }

  function inScale(pitch, rootPc, modeIdx) {
    var pc = mod12(pitch), iv = MODES[modeIdx].intervals, i;
    for (i = 0; i < iv.length; i++) {
      if (mod12(rootPc + iv[i]) === pc) { return true; }
    }
    return false;
  }

  // Nearest in-scale MIDI note. Ties (same distance) prefer the lower pitch.
  function nearestInScale(pitch, rootPc, modeIdx) {
    pitch = Math.round(pitch);
    if (inScale(pitch, rootPc, modeIdx)) { return pitch; }
    var d;
    for (d = 1; d <= 6; d++) {
      var down = pitch - d, up = pitch + d;
      var downOk = down >= 0 && inScale(down, rootPc, modeIdx);
      var upOk = up <= 127 && inScale(up, rootPc, modeIdx);
      if (downOk) { return down; }
      if (upOk) { return up; }
    }
    return pitch < 0 ? 0 : (pitch > 127 ? 127 : pitch);
  }

  function degreePc(rootPc, modeIdx, degree) {
    var iv = MODES[modeIdx].intervals;
    var d = ((degree % 7) + 7) % 7;
    var oct = Math.floor(degree / 7);
    return mod12(rootPc + iv[d]) + 12 * oct;
  }

  return {
    ROOT_MENU: ROOT_MENU,
    SHARP_NAMES: SHARP_NAMES,
    FLAT_NAMES: FLAT_NAMES,
    MODES: MODES,
    mod12: mod12,
    keyUsesFlats: keyUsesFlats,
    keyNameAscii: keyNameAscii,
    keyName: keyName,
    scalePcs: scalePcs,
    inScale: inScale,
    nearestInScale: nearestInScale,
    degreePc: degreePc
  };
})();
