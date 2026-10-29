// dooblr-chords engine: chord construction, colour (extensions) and naming (ES5).
//
// A chord is a plain object stored relative to the key tonic, so locked chords
// survive key changes and serialise cleanly:
//   root     semitones above the key tonic (0..11)
//   letter   letter steps above the key tonic letter (0..6), for spelling
//   scale    7 ascending intervals: the chord scale seen from the chord root
//   third    3 | 4 | null (null when suspended)
//   sus      0 | 2 | 4
//   fifth    6 | 7 | 8
//   seventh  null | 9 | 10 | 11
//   nine     null | 13 | 14 | 15     (b9, 9, #9)
//   eleven   null | 17 | 18          (11, #11)
//   thirteen null | 20 | 21          (b13, 13)
//   add9     true when the 9 is added without a seventh
//   slash    null | 'third' | 'fifth' | 'seventh'  (chord tone in the bass)
//   degree   diatonic degree (0..6) used for functional context
//   func     'T' | 'PD' | 'D'
//   source   'diatonic' | 'borrowed' | 'secondary' | 'tritone' | 'neapolitan'
//   targetRoman  roman numeral of the target (secondary / tritone only)
//   level    colour level requested (0..4)
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.chords = (function () {
  var T = DC.theory;
  var ROMANS = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];
  var SLASH_STEPS = { third: 2, fifth: 4, seventh: 6 };
  var CHROMATIC_SOURCES = { secondary: true, tritone: true, neapolitan: true };

  function indexOfNum(arr, v) {
    for (var i = 0; i < arr.length; i++) { if (arr[i] === v) { return i; } }
    return -1;
  }

  // Common chord scales, as intervals above the chord root.
  var SCALES = {
    mixolydian: [0, 2, 4, 5, 7, 9, 10],
    phrygianDominant: [0, 1, 4, 5, 7, 8, 10],
    lydianDominant: [0, 2, 4, 6, 7, 9, 10],
    lydian: [0, 2, 4, 6, 7, 9, 11]
  };

  function copy(chord) {
    var out = {}, k;
    for (k in chord) {
      if (chord.hasOwnProperty(k)) {
        out[k] = (chord[k] instanceof Array) ? chord[k].slice(0) : chord[k];
      }
    }
    return out;
  }

  function fromScale(root, letter, scale, props) {
    var c = {
      root: T.mod12(root), letter: ((letter % 7) + 7) % 7, scale: scale.slice(0),
      third: scale[2], sus: 0, fifth: scale[4], seventh: null,
      nine: null, eleven: null, thirteen: null, add9: false, slash: null,
      degree: 0, func: 'T', source: 'diatonic', targetRoman: null, level: 0
    };
    if (props) { for (var k in props) { if (props.hasOwnProperty(k)) { c[k] = props[k]; } } }
    return c;
  }

  // Diatonic triad on degree d of a mode.
  function diatonic(modeIdx, d, props) {
    var iv = T.MODES[modeIdx].intervals;
    return fromScale(iv[d], d, T.rotate(iv, d), props);
  }

  function isDominant(c) { return c.third === 4 && c.fifth === 7 && c.seventh === 10; }
  function isDominantScale(c) { return c.third === 4 && c.fifth === 7 && c.scale[6] === 10; }
  function isDiminished(c) { return c.third === 3 && c.fifth === 6; }
  function isAugmented(c) { return c.third === 4 && c.fifth === 8; }
  function isMinor(c) { return c.third === 3; }

  function chordTonesPc(c) {
    var t = [0, c.fifth];
    if (c.third !== null) { t.push(c.third); }
    if (c.sus) { t.push(c.sus === 4 ? 5 : 2); }
    if (c.seventh !== null) { t.push(c.seventh); }
    return t;
  }

  // Is extension e (an interval > 12) an avoid note for this chord?
  // Rule: avoid an extension a semitone above a chord tone (a minor 9th
  // clash) or doubling a chord tone. Dominants may keep b9 and b13.
  function avoided(c, e) {
    var em = T.mod12(e), tones = chordTonesPc(c);
    var dom = c.third === 4 && c.seventh === 10;
    for (var i = 0; i < tones.length; i++) {
      var diff = T.mod12(em - tones[i]);
      if (diff === 0) { return true; }
      if (em === 3 && !dom) { return true; } // #9 only on dominants
      if (diff === 1 && !(dom && (em === 1 || em === 8))) { return true; }
    }
    return false;
  }

  function clearColor(c) {
    c.seventh = null; c.nine = null; c.eleven = null; c.thirteen = null; c.add9 = false;
    c.third = c.scale[2]; c.fifth = c.scale[4]; c.sus = 0;
  }

  // Apply a colour level (0 triad, 1 seventh/add9/sus, 2 ninth, 3 eleventh, 4 thirteenth).
  // u is a uniform draw used to pick the level-1 variant. Extensions come only
  // from the chord scale, so diatonic chords stay diatonic.
  function applyColor(c, level, u) {
    clearColor(c);
    c.level = level;
    if (level <= 0) { return c; }
    var s = c.scale;
    var dim = isDiminished(c), aug = isAugmented(c), domScale = isDominantScale(c);
    var canSus4 = s[3] === 5 && c.fifth === 7 && !aug;
    var canSus2 = s[1] === 2 && c.fifth === 7 && !aug && !domScale;
    var variant = 'seventh';
    if (level === 1 && !dim) {
      var add9ok = !avoided(c, s[1] + 12);
      var names = ['seventh', 'add9', 'sus4', 'sus2'];
      var w = [3, add9ok ? 1 : 0, canSus4 ? 0.8 : 0, canSus2 ? 0.5 : 0];
      variant = names[DC.rng.pickIndex(w, u)];
    } else if (level >= 2 && domScale && canSus4 && u > 0.85) {
      variant = 'sus4';
    }

    if (variant === 'add9') {
      c.nine = s[1] + 12; c.add9 = true;
      return c;
    }
    if (variant === 'sus4' || variant === 'sus2') {
      c.third = null; c.sus = variant === 'sus4' ? 4 : 2;
      if (level === 1 && !domScale) { return c; } // plain sus triad
    }
    c.seventh = s[6];
    if (dim && c.seventh === 9) { return c; } // dim7: no extensions
    var want = level - 1;
    var cands = [['nine', s[1] + 12], ['eleven', s[3] + 12], ['thirteen', s[5] + 12]];
    for (var i = 0; i < cands.length && want > 0; i++) {
      if (aug && cands[i][0] !== 'nine') { continue; }
      if (!avoided(c, cands[i][1])) { c[cands[i][0]] = cands[i][1]; want--; }
    }
    return c;
  }

  // Altered tensions on a dominant chord (chromatic colour): b9/#9 and b13.
  function alterDominant(c, u, preferFlatNine) {
    if (!isDominant(c)) { return c; }
    if (c.nine !== null) { c.nine = (preferFlatNine || u < 0.5) ? 13 : 15; }
    if (c.thirteen !== null) { c.thirteen = 20; }
    return c;
  }

  // Intervals above the chord root, ascending.
  function tones(c) {
    var out = [0];
    if (c.third !== null) { out.push(c.third); }
    if (c.sus) { out.push(c.sus === 4 ? 5 : 2); }
    out.push(c.fifth);
    if (c.seventh !== null) { out.push(c.seventh); }
    if (c.nine !== null) { out.push(c.nine); }
    if (c.eleven !== null) { out.push(c.eleven); }
    if (c.thirteen !== null) { out.push(c.thirteen); }
    out.sort(function (a, b) { return a - b; });
    return out;
  }

  function bassInterval(c) {
    return c.slash ? (c[c.slash] || 0) : 0;
  }

  function qualitySymbol(c) {
    var fl = T.accidental(-1), sh = T.accidental(1);
    var nat = 0;
    if (c.thirteen === 21) { nat = 13; } else if (c.eleven === 17) { nat = 11; } else if (c.nine === 14 && !c.add9) { nat = 9; }
    var alts = '';
    if (c.nine === 13) { alts += fl + '9'; }
    if (c.nine === 15) { alts += sh + '9'; }
    if (c.eleven === 18) { alts += sh + '11'; }
    if (c.thirteen === 20) { alts += fl + '13'; }
    var maj3 = c.third === 4, min3 = c.third === 3;

    if (c.seventh === null) {
      if (c.add9) { return (min3 ? 'm(add9)' : 'add9'); }
      if (c.sus === 4) { return 'sus4'; }
      if (c.sus === 2) { return 'sus2'; }
      if (min3 && c.fifth === 6) { return 'dim'; }
      if (maj3 && c.fifth === 8) { return '+'; }
      return min3 ? 'm' : '';
    }
    var n = String(nat || 7), s;
    if (c.sus) {
      s = (c.seventh === 11 ? 'maj' + n : n) + 'sus' + (c.sus === 4 ? '4' : '2');
    } else if (maj3 && c.fifth === 8) {
      s = (c.seventh === 11 ? 'maj' + n : n) + sh + '5';
    } else if (min3 && c.fifth === 6) {
      s = (c.seventh === 9) ? 'dim7' : 'm' + n + fl + '5';
    } else if (min3) {
      s = (c.seventh === 11) ? 'm(maj' + n + ')' : 'm' + n;
    } else {
      s = (c.seventh === 11) ? 'maj' + n : n;
    }
    return s + alts;
  }

  // Display name, e.g. "Fmaj7/A", "Am9", "E7b9".
  function name(c, keyRoot, modeIdx) {
    var flats = T.keyUsesFlats(keyRoot, modeIdx);
    var letter = T.keyLetter(keyRoot, modeIdx) + c.letter;
    var rootPc = keyRoot + c.root;
    var natural = indexOfNum(T.NATURALS, T.mod12(rootPc));
    if (CHROMATIC_SOURCES[c.source] && natural >= 0) { letter = natural; } // B7, not Cb7
    var out = T.spell(rootPc, letter, flats) + qualitySymbol(c);
    if (c.slash) {
      out += '/' + T.spell(rootPc + bassInterval(c), letter + SLASH_STEPS[c.slash], flats);
    }
    return out;
  }

  // Roman numeral relative to the major scale on the tonic: i, bVI, V/ii ...
  function baseRoman(c) {
    var acc = T.mod12(c.root - T.NATURALS[c.letter] + 6) - 6;
    var r = ROMANS[c.letter];
    var third = c.third !== null ? c.third : c.scale[2];
    if (third === 3) { r = r.toLowerCase(); }
    return T.accidental(acc) + r;
  }

  function roman(c) {
    var toTonic = c.targetRoman === 'I' || c.targetRoman === 'i';
    if (c.source === 'secondary') { return toTonic ? 'V' : 'V/' + c.targetRoman; }
    if (c.source === 'tritone') { return toTonic ? 'subV' : 'subV/' + c.targetRoman; }
    if (c.source === 'neapolitan') { return 'N'; }
    var r = baseRoman(c);
    if (isDiminished(c)) { r += (c.seventh === 10) ? '\u00F8' : '\u00B0'; }
    if (isAugmented(c)) { r += '+'; }
    return r;
  }

  return {
    SCALES: SCALES,
    copy: copy,
    fromScale: fromScale,
    diatonic: diatonic,
    applyColor: applyColor,
    alterDominant: alterDominant,
    avoided: avoided,
    tones: tones,
    bassInterval: bassInterval,
    qualitySymbol: qualitySymbol,
    name: name,
    baseRoman: baseRoman,
    roman: roman,
    isDominant: isDominant,
    isDiminished: isDiminished,
    isAugmented: isAugmented,
    isMinor: isMinor
  };
})();
