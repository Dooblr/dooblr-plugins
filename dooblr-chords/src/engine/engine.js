// dooblr-chords engine: facade used by the Max glue and the tests (ES5).
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.engine = (function () {
  var T = DC.theory, C = DC.chords, G = DC.generator, V = DC.voicing, RN = DC.render;
  var A = DC.analysis, DV = DC.derive, MF = DC.midifile;

  var VERSION = '0.4.0';
  var KINDS = [{ id: 'var', label: 'VAR', long: 'Variation' }, { id: 'b', label: 'B', long: 'B section' }];
  var DURATIONS = [
    { label: '1/2', beats: 2 },
    { label: '1', beats: 4 },
    { label: '2', beats: 8 }
  ];
  var SEED_MAX = 99999;

  var DEFAULTS = {
    root: 0, mode: 1, length: 4, duration: 1, color: 35, chromatic: 10,
    register: 60, spread: 25, bass: 1, velocity: 100, humanize: 0, seed: 1, kind: 0
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
    o.kind = clamp(Math.round(o.kind), 0, KINDS.length - 1);
    return o;
  }

  // "C min · 4 · Col35 Chr10"  (ASCII key name; the dot is U+00B7)
  function clipName(s) {
    var o = normalize(s);
    return T.keyNameAscii(o.root, o.mode) + ' ' + T.MODES[o.mode].short + ' \u00B7 ' + o.length +
      ' \u00B7 Col' + o.color + ' Chr' + o.chromatic;
  }

  function newSeed(u) { return 1 + Math.floor(u * SEED_MAX); }

  // Clip name for a derived clip: "riff · VAR · A min · Col35 Chr10" or
  // "riff · B III · A min · Col35 Chr10". The source name is shortened.
  function derivedClipName(o, source, region) {
    var nm = String((source && source.name) || 'Clip');
    if (nm.length > 18) { nm = nm.substring(0, 17) + '\u2026'; }
    var kind = KINDS[o.kind].label + (o.kind === 1 && region ? ' ' + region.id.replace(/\u266D/g, 'b') : '');
    return nm + ' \u00B7 ' + kind + ' \u00B7 ' + T.keyNameAscii(o.root, o.mode) + ' ' + T.MODES[o.mode].short +
      ' \u00B7 Col' + o.color + ' Chr' + o.chromatic;
  }

  // Read a Standard MIDI File (array of byte values) into a source analysis.
  function sourceFromMidi(bytes, name) {
    var smf = MF.parse(bytes);
    return analyzeSource(smf.notes, { lengthBeats: smf.lengthBeats, name: name || smf.name });
  }

  // Analyse clip notes ({pitch, start | start_time, duration, velocity, mute}).
  // Throws an Error with a readable message when no chords are found.
  function analyzeSource(notes, opts) {
    var an = A.analyze(notes, opts);
    an.keyName = T.keyName(an.key.root, an.key.mode) + ' ' + T.MODES[an.key.mode].name;
    return an;
  }

  // What the display shows about the source, read in the key (root, mode).
  function sourceDisplay(an, root, mode) {
    var ch = A.toChords(an, root, mode), names = [], romans = [];
    for (var i = 0; i < ch.length; i++) { names.push(C.name(ch[i], root, mode)); romans.push(C.roman(ch[i])); }
    return {
      name: an.name || 'Clip', detectedKey: an.keyName, key: T.keyName(root, mode) + ' ' + T.MODES[mode].name,
      chords: names, romans: romans, bars: an.lengthBeats / 4
    };
  }

  // Build everything a host needs: chords (with display names), voicings,
  // clip notes, clip length in beats and clip name.
  // source (optional): { analysis, keep: { rhythm } }. With a source the
  // progression is a VARIATION or B SECTION of it (settings.kind) instead of
  // a fresh one; Length equal to the source's chord count keeps its rhythm.
  function build(settings, locks, source) {
    var o = normalize(settings), i;
    var chords, ending, slots = null, lengthBeats, changed = null, rhythm = 'uniform', region = null;
    var beats = DURATIONS[o.duration].beats;
    var vopts = { register: o.register, spread: o.spread, bass: o.bass };
    if (source && source.analysis) {
      var d = DV.derive(source.analysis, o, KINDS[o.kind].id, locks, source.keep || null);
      chords = d.chords; ending = d.ending; slots = d.slots; lengthBeats = d.lengthBeats;
      changed = d.changed; rhythm = d.rhythm; region = d.region;
      if (source.analysis.voicing) { vopts.maxUpper = source.analysis.voicing.maxUpper; }
    } else {
      var prog = G.generate({ root: o.root, mode: o.mode, length: o.length, color: o.color, chromatic: o.chromatic, seed: o.seed }, locks);
      chords = prog.chords; ending = prog.ending; lengthBeats = beats * o.length;
    }
    var voicings = V.voiceAll(chords, o.root, vopts);
    var notes = RN.notes(voicings, { chordBeats: beats, velocity: o.velocity, humanize: o.humanize, seed: o.seed, slots: slots });
    var display = [];
    for (i = 0; i < chords.length; i++) {
      var ch = chords[i];
      display.push({
        name: C.name(ch, o.root, o.mode), roman: C.roman(ch), func: ch.func, source: ch.source,
        locked: !!(locks && locks[i]), changed: !!(changed && changed[i]),
        beats: slots ? slots[i].duration : beats
      });
    }
    var keyName = T.keyName(o.root, o.mode) + ' ' + T.MODES[o.mode].name;
    var out = {
      settings: o, chords: chords, display: display, voicings: voicings, notes: notes,
      ending: ending, lengthBeats: lengthBeats, clipName: clipName(o), keyName: keyName,
      derived: null, rhythm: rhythm
    };
    if (source && source.analysis) {
      var regionText = '';
      if (region) {
        regionText = 'in ' + T.simpleName(T.mod12(region.root), T.keyUsesFlats(o.root, o.mode)) + (region.minor ? ' minor' : ' major') +
          ' (' + region.id.replace(/^b/, T.accidental(-1)) + ')';
      }
      out.derived = { kind: KINDS[o.kind].id, label: KINDS[o.kind].long + (regionText ? ' ' + regionText : ''),
        heading: KINDS[o.kind].long.toUpperCase(), regionText: regionText, region: region ? region.id : null, rhythm: rhythm,
        source: sourceDisplay(source.analysis, o.root, o.mode) };
      out.clipName = derivedClipName(o, source.analysis, region);
    }
    return out;
  }

  return {
    VERSION: VERSION, DURATIONS: DURATIONS, DEFAULTS: DEFAULTS, SEED_MAX: SEED_MAX, KINDS: KINDS,
    normalize: normalize, clipName: clipName, newSeed: newSeed, build: build,
    sourceFromMidi: sourceFromMidi, analyzeSource: analyzeSource, sourceDisplay: sourceDisplay,
    isMidiPath: MF.isMidiPath, baseName: MF.baseName
  };
})();
