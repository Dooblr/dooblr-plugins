// dooblr-chords engine: facade used by the Max glue and the tests (ES5).
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.engine = (function () {
  var T = DC.theory, C = DC.chords, G = DC.generator, V = DC.voicing, RN = DC.render;

  var VERSION = '0.1.0';
  var DURATIONS = [
    { label: '1/2', beats: 2 },
    { label: '1', beats: 4 },
    { label: '2', beats: 8 }
  ];
  var SEED_MAX = 99999;

  var DEFAULTS = {
    root: 0, mode: 1, length: 4, duration: 1, color: 35, chromatic: 10,
    register: 60, spread: 25, bass: 1, velocity: 100, humanize: 0, seed: 1
  };

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  function normalize(s) {
    var o = {}, k;
    for (k in DEFAULTS) { if (DEFAULTS.hasOwnProperty(k)) { o[k] = (s && s[k] !== undefined && s[k] !== null) ? +s[k] : DEFAULTS[k]; } }
    o.root = T.mod12(Math.round(o.root));
    o.mode = clamp(Math.round(o.mode), 0, T.MODES.length - 1);
    o.length = clamp(Math.round(o.length), 2, 16);
    o.duration = clamp(Math.round(o.duration), 0, DURATIONS.length - 1);
    o.color = clamp(Math.round(o.color), 0, 100);
    o.chromatic = clamp(Math.round(o.chromatic), 0, 100);
    o.register = clamp(Math.round(o.register), 36, 72);
    o.spread = clamp(o.spread, 0, 100);
    o.bass = o.bass ? 1 : 0;
    o.velocity = clamp(Math.round(o.velocity), 1, 127);
    o.humanize = clamp(o.humanize, 0, 100);
    o.seed = clamp(Math.round(o.seed), 1, SEED_MAX);
    return o;
  }

  // "C min · 4 · Col35 Chr10"  (ASCII key name; the dot is U+00B7)
  function clipName(s) {
    var o = normalize(s);
    return T.keyNameAscii(o.root, o.mode) + ' ' + T.MODES[o.mode].short + ' \u00B7 ' + o.length +
      ' \u00B7 Col' + o.color + ' Chr' + o.chromatic;
  }

  function newSeed(u) { return 1 + Math.floor(u * SEED_MAX); }

  // Build everything a host needs: chords (with display names), voicings,
  // clip notes, clip length in beats and clip name.
  function build(settings, locks) {
    var o = normalize(settings);
    var prog = G.generate({ root: o.root, mode: o.mode, length: o.length, color: o.color, chromatic: o.chromatic, seed: o.seed }, locks);
    var voicings = V.voiceAll(prog.chords, o.root, { register: o.register, spread: o.spread, bass: o.bass });
    var beats = DURATIONS[o.duration].beats;
    var notes = RN.notes(voicings, { chordBeats: beats, velocity: o.velocity, humanize: o.humanize, seed: o.seed });
    var display = [];
    for (var i = 0; i < prog.chords.length; i++) {
      var ch = prog.chords[i];
      display.push({
        name: C.name(ch, o.root, o.mode), roman: C.roman(ch), func: ch.func, source: ch.source,
        locked: !!(locks && locks[i])
      });
    }
    return {
      settings: o, chords: prog.chords, display: display, voicings: voicings, notes: notes,
      ending: prog.ending, lengthBeats: beats * o.length, clipName: clipName(o),
      keyName: T.keyName(o.root, o.mode) + ' ' + T.MODES[o.mode].name
    };
  }

  return {
    VERSION: VERSION, DURATIONS: DURATIONS, DEFAULTS: DEFAULTS, SEED_MAX: SEED_MAX,
    normalize: normalize, clipName: clipName, newSeed: newSeed, build: build
  };
})();
