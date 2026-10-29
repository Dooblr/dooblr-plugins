// dooblr-chords engine: scales, modes and note spelling (ES5).
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.theory = (function () {
  var LETTERS = ['C', 'D', 'E', 'F', 'G', 'A', 'B'];
  var NATURALS = [0, 2, 4, 5, 7, 9, 11];
  var SHARP_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  var FLAT_NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
  // Menu labels for the Root control (plain ASCII so every font can show them).
  var ROOT_MENU = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];

  // family: 'major' modes have a major third over the tonic, 'minor' a minor third.
  // parentOffset: semitones from the parent major scale tonic to this mode's tonic.
  var MODES = [
    { id: 'major', name: 'Major', short: 'maj', family: 'major', parentOffset: 0, intervals: [0, 2, 4, 5, 7, 9, 11] },
    { id: 'minor', name: 'Natural Minor', short: 'min', family: 'minor', parentOffset: 9, intervals: [0, 2, 3, 5, 7, 8, 10] },
    { id: 'dorian', name: 'Dorian', short: 'dor', family: 'minor', parentOffset: 2, intervals: [0, 2, 3, 5, 7, 9, 10] },
    { id: 'phrygian', name: 'Phrygian', short: 'phr', family: 'minor', parentOffset: 4, intervals: [0, 1, 3, 5, 7, 8, 10] },
    { id: 'lydian', name: 'Lydian', short: 'lyd', family: 'major', parentOffset: 5, intervals: [0, 2, 4, 6, 7, 9, 11] },
    { id: 'mixolydian', name: 'Mixolydian', short: 'mix', family: 'major', parentOffset: 7, intervals: [0, 2, 4, 5, 7, 9, 10] },
    { id: 'harmonicMinor', name: 'Harmonic Minor', short: 'hmin', family: 'minor', parentOffset: 9, intervals: [0, 2, 3, 5, 7, 8, 11] }
  ];

  var asciiAccidentals = false;
  function setAsciiAccidentals(on) { asciiAccidentals = !!on; }
  function flatSign() { return asciiAccidentals ? 'b' : '\u266D'; }
  function sharpSign() { return asciiAccidentals ? '#' : '\u266F'; }
  function accidental(n) {
    if (n === -1) { return flatSign(); }
    if (n === 1) { return sharpSign(); }
    if (n === -2) { return flatSign() + flatSign(); }
    if (n === 2) { return sharpSign() + sharpSign(); }
    return '';
  }

  function mod12(n) { return ((n % 12) + 12) % 12; }

  function modeIndexById(id) {
    for (var i = 0; i < MODES.length; i++) { if (MODES[i].id === id) { return i; } }
    return -1;
  }

  // Interval above the tonic of scale degree d (d may exceed 6; wraps by octave).
  function degreeInterval(modeIdx, d) {
    var iv = MODES[modeIdx].intervals;
    var oct = Math.floor(d / 7);
    return iv[((d % 7) + 7) % 7] + 12 * oct;
  }

  // The 7-note scale seen from degree d, as ascending intervals from 0.
  function rotate(intervals, d) {
    var out = [];
    for (var k = 0; k < 7; k++) {
      out.push(mod12(intervals[(d + k) % 7] - intervals[d]));
    }
    return out;
  }

  // Key signature preference: flat keys use flat spellings.
  function keyUsesFlats(rootPc, modeIdx) {
    var parent = mod12(rootPc - MODES[modeIdx].parentOffset);
    return parent === 5 || parent === 10 || parent === 3 || parent === 8 || parent === 1;
  }

  // Letter index (0..6) used to spell the key tonic.
  function keyLetter(rootPc, modeIdx) {
    var name = keyUsesFlats(rootPc, modeIdx) ? FLAT_NAMES[mod12(rootPc)] : SHARP_NAMES[mod12(rootPc)];
    return indexOf(LETTERS, name.charAt(0));
  }

  function indexOf(arr, v) {
    for (var i = 0; i < arr.length; i++) { if (arr[i] === v) { return i; } }
    return -1;
  }

  // Spell an absolute pitch class on a given letter (0..6). Falls back to a
  // plain sharp/flat name if that letter would need a double accidental.
  function spell(pc, letter, preferFlats) {
    pc = mod12(pc);
    if (letter >= 0) {
      letter = ((letter % 7) + 7) % 7;
      var acc = mod12(pc - NATURALS[letter] + 6) - 6;
      if (acc >= -1 && acc <= 1) { return LETTERS[letter] + accidental(acc); }
    }
    return simpleName(pc, preferFlats);
  }

  function simpleName(pc, preferFlats) {
    var n = preferFlats ? FLAT_NAMES[mod12(pc)] : SHARP_NAMES[mod12(pc)];
    if (n.length > 1) { n = n.charAt(0) + (n.charAt(1) === 'b' ? flatSign() : sharpSign()); }
    return n;
  }

  // Plain ASCII key name for clip names, e.g. "Eb", "F#".
  function keyNameAscii(rootPc, modeIdx) {
    return keyUsesFlats(rootPc, modeIdx) ? FLAT_NAMES[mod12(rootPc)] : SHARP_NAMES[mod12(rootPc)];
  }

  function keyName(rootPc, modeIdx) {
    return spell(rootPc, keyLetter(rootPc, modeIdx), keyUsesFlats(rootPc, modeIdx));
  }

  return {
    LETTERS: LETTERS,
    NATURALS: NATURALS,
    ROOT_MENU: ROOT_MENU,
    MODES: MODES,
    mod12: mod12,
    modeIndexById: modeIndexById,
    degreeInterval: degreeInterval,
    rotate: rotate,
    keyUsesFlats: keyUsesFlats,
    keyLetter: keyLetter,
    keyName: keyName,
    keyNameAscii: keyNameAscii,
    spell: spell,
    simpleName: simpleName,
    accidental: accidental,
    setAsciiAccidentals: setAsciiAccidentals
  };
})();
