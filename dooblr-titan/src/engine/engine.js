// dooblr-titan engine: facade used by the Max glue and the tests (ES5).
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.engine = (function () {
  var T = DT.theory, N = DT.notes, MF = DT.midifile, Q = DT.quantize;
  var C = DT.cleanup, V = DT.velocity, K = DT.keydetect, S = DT.scalesnap, VR = DT.vary;

  var VERSION = '0.1.0';
  var SEED_MAX = 99999;
  var PREVIEW_COUNT = VR.COUNT;

  var DEFAULTS = {
    qstrength: 100, grid: 3, swing: 0, groove: 1, flams: 1,
    minlen: 8, minvel: 8, legato: 1, gapclose: 0,
    velmin: 64, velmax: 112, velcomp: 40, humanize: 6,
    scalesnap: 0, root: 0, mode: 0, autokey: 1,
    amount: 40, rhythm: 50, melody: 50, ghosts: 30, accents: 40, seed: 1
  };

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  function normalize(s) {
    var o = {}, k;
    for (k in DEFAULTS) {
      if (DEFAULTS.hasOwnProperty(k)) {
        o[k] = (s && s[k] !== undefined && s[k] !== null) ? +s[k] : DEFAULTS[k];
      }
    }
    o.qstrength = clamp(Math.round(o.qstrength), 0, 100);
    o.grid = clamp(Math.round(o.grid), 0, Q.GRIDS.length - 1);
    o.swing = clamp(Math.round(o.swing), 0, 100);
    o.groove = o.groove ? 1 : 0;
    o.flams = o.flams ? 1 : 0;
    o.minlen = clamp(Math.round(o.minlen), 0, 100);
    o.minvel = clamp(Math.round(o.minvel), 1, 127);
    o.legato = o.legato ? 1 : 0;
    o.gapclose = clamp(Math.round(o.gapclose), 0, 100);
    o.velmin = clamp(Math.round(o.velmin), 1, 127);
    o.velmax = clamp(Math.round(o.velmax), 1, 127);
    o.velcomp = clamp(Math.round(o.velcomp), 0, 100);
    o.humanize = clamp(Math.round(o.humanize), 0, 100);
    o.scalesnap = o.scalesnap ? 1 : 0;
    o.root = T.mod12(Math.round(o.root));
    o.mode = clamp(Math.round(o.mode), 0, T.MODES.length - 1);
    o.autokey = o.autokey ? 1 : 0;
    o.amount = clamp(Math.round(o.amount), 0, 100);
    o.rhythm = clamp(Math.round(o.rhythm), 0, 100);
    o.melody = clamp(Math.round(o.melody), 0, 100);
    o.ghosts = clamp(Math.round(o.ghosts), 0, 100);
    o.accents = clamp(Math.round(o.accents), 0, 100);
    o.seed = clamp(Math.round(o.seed), 1, SEED_MAX);
    return o;
  }

  function newSeed(u) { return 1 + Math.floor(u * SEED_MAX); }

  function detectKey(notes) { return K.detect(notes); }

  function loadNotes(notes, opts) {
    opts = opts || {};
    var src = N.sort(N.copyAll(notes));
    if (!src.length) { throw new Error('The clip has no notes.'); }
    var lengthBeats = N.lengthBeats(src, opts.lengthBeats);
    var key = K.detect(src);
    return {
      notes: src,
      lengthBeats: lengthBeats,
      name: String(opts.name || 'Clip'),
      key: key,
      noteCount: src.length
    };
  }

  function sourceFromMidi(bytes, name) {
    var smf = MF.parse(bytes);
    return loadNotes(smf.notes, { lengthBeats: smf.lengthBeats, name: name || smf.name || 'Clip' });
  }

  function clipName(kind, sourceName, o, extra) {
    var nm = String(sourceName || 'Clip');
    if (nm.length > 18) { nm = nm.substring(0, 17) + '\u2026'; }
    var key = T.keyNameAscii(o.root, o.mode) + ' ' + T.MODES[o.mode].short;
    if (kind === 'tight') {
      return nm + ' \u00B7 TIGHT \u00B7 ' + Q.GRIDS[o.grid].label + ' Q' + o.qstrength;
    }
    return nm + ' \u00B7 VAR ' + (extra || '') + ' \u00B7 ' + key + ' amt' + o.amount;
  }

  function tighten(source, settings) {
    var o = normalize(settings);
    var src = source.notes;
    var lengthBeats = source.lengthBeats;
    var notes = N.copyAll(src);
    var before = notes.length;

    notes = C.apply(notes, {
      minlen: o.minlen, minvel: o.minvel, flams: o.flams, legato: o.legato, gapclose: o.gapclose
    });
    notes = Q.apply(notes, {
      strength: o.qstrength, grid: o.grid, swing: o.swing, groove: o.groove
    });
    if (o.scalesnap) { notes = S.apply(notes, o.root, o.mode); }
    notes = V.apply(notes, {
      velmin: o.velmin, velmax: o.velmax, velcomp: o.velcomp, humanize: o.humanize, seed: o.seed
    });
    notes = C.trimOverlaps(notes);

    var i;
    for (i = 0; i < notes.length; i++) {
      if (notes[i].start > lengthBeats - 0.01) { notes[i].start = N.round4(lengthBeats - 0.01); }
      if (notes[i].start + notes[i].duration > lengthBeats) {
        notes[i].duration = N.round4(Math.max(0.02, lengthBeats - notes[i].start));
      }
    }
    notes = N.sort(notes);
    return {
      notes: notes,
      liveNotes: N.toLive(notes),
      lengthBeats: lengthBeats,
      clipName: clipName('tight', source.name, o),
      settings: o,
      removed: Math.max(0, before - notes.length),
      key: source.key
    };
  }

  function variations(source, settings, count) {
    var o = normalize(settings);
    var n = count || PREVIEW_COUNT;
    var raw = VR.preview(source.notes, {
      amount: o.amount, rhythm: o.rhythm, melody: o.melody, ghosts: o.ghosts, accents: o.accents,
      seed: o.seed, root: o.root, mode: o.mode, grid: o.grid, lengthBeats: source.lengthBeats
    }, n);
    var out = [], i;
    for (i = 0; i < raw.length; i++) {
      out.push({
        index: i,
        label: raw[i].label,
        notes: raw[i].notes,
        liveNotes: N.toLive(raw[i].notes),
        lengthBeats: raw[i].lengthBeats,
        clipName: clipName('var', source.name, o, raw[i].label),
        noteCount: raw[i].notes.length
      });
    }
    return out;
  }

  function midiBytes(notes, opts) {
    return MF.write(notes, opts || {});
  }

  return {
    VERSION: VERSION, DEFAULTS: DEFAULTS, SEED_MAX: SEED_MAX, PREVIEW_COUNT: PREVIEW_COUNT,
    GRIDS: Q.GRIDS, MODES: T.MODES, ROOT_MENU: T.ROOT_MENU,
    normalize: normalize, newSeed: newSeed, detectKey: detectKey,
    loadNotes: loadNotes, sourceFromMidi: sourceFromMidi,
    tighten: tighten, variations: variations, midiBytes: midiBytes,
    clipName: clipName, isMidiPath: MF.isMidiPath, baseName: MF.baseName
  };
})();
