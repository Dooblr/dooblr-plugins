// dooblr-chord-receiver: pitch mapping and note-off bookkeeping (ES5).
// Used by the receiver device; hosted here so the sender repo's tests and a
// shared build can load one copy of the theory + protocol + snap stack.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.snap = (function () {
  var NEAREST = 0, UP = 1, DOWN = 2;
  var TONES = 0, SCALE = 1, BASS = 2, FOLLOW = 3;

  function mod12(n) { return ((n % 12) + 12) % 12; }
  function clamp7(p) {
    p = Math.round(+p);
    if (p < 0) { return 0; }
    if (p > 127) { return 127; }
    return p;
  }

  // Snap `pitch` onto the given pitch-classes.
  // dir 0 = nearest (ties go down), 1 = up (>= pitch), 2 = down (<= pitch).
  // A pitch that is already a member stays put for every direction.
  function snapToPcs(pitch, pcs, dir) {
    pitch = clamp7(pitch);
    if (!pcs || !pcs.length) { return pitch; }
    dir = dir || 0;
    var best = null, bestAd = 0, i, oct, cand, d, ad, pc;
    for (i = 0; i < pcs.length; i++) {
      pc = mod12(pcs[i]);
      for (oct = 0; oct <= 10; oct++) {
        cand = oct * 12 + pc;
        if (cand < 0 || cand > 127) { continue; }
        d = cand - pitch;
        if (dir === UP && d < 0) { continue; }
        if (dir === DOWN && d > 0) { continue; }
        ad = d < 0 ? -d : d;
        if (best === null || ad < bestAd || (ad === bestAd && cand < best)) {
          best = cand;
          bestAd = ad;
        }
      }
    }
    if (best === null) { return snapToPcs(pitch, pcs, NEAREST); }
    return best;
  }

  // Put the bass/slash pitch class in the incoming note's MIDI octave
  // window [12*floor(p/12), 12*floor(p/12)+11].
  function snapBass(pitch, bassPc) {
    pitch = clamp7(pitch);
    var oct = Math.floor(pitch / 12);
    var out = oct * 12 + mod12(bassPc);
    if (out > 127) { out -= 12; }
    if (out < 0) { out += 12; }
    return clamp7(out);
  }

  // Transpose so the played interval above the reference root is kept
  // above the current chord root. Delta is 0..11 upward (C→G is +7).
  function snapFollow(pitch, refRoot, chordRoot) {
    var delta = mod12(chordRoot - refRoot);
    return clamp7(pitch + delta);
  }

  // When range is on, wrap the pitch into one octave at `register`.
  function constrainRange(pitch, rangeOn, register) {
    if (!rangeOn) { return clamp7(pitch); }
    var lo = clamp7(register), hi = lo + 12, p = clamp7(pitch);
    while (p > hi && p - 12 >= 0) { p -= 12; }
    while (p < lo && p + 12 <= 127) { p += 12; }
    return clamp7(p);
  }

  function mapPitch(harmony, pitch, opts) {
    opts = opts || {};
    if (!harmony) { return clamp7(pitch); }
    var role = opts.role || 0;
    var dir = opts.snapdir || 0;
    var out;
    if (role === BASS) {
      out = snapBass(pitch, harmony.bass);
    } else if (role === FOLLOW) {
      out = snapFollow(pitch, opts.refroot || 0, harmony.chordRoot);
    } else if (role === SCALE) {
      out = snapToPcs(pitch, harmony.scale, dir);
    } else {
      out = snapToPcs(pitch, harmony.chord, dir);
    }
    return constrainRange(out, opts.range, opts.register || 48);
  }

  function create() {
    return { held: {}, refs: {}, harmony: null };
  }

  function incRef(state, pitch) {
    state.refs[pitch] = (state.refs[pitch] || 0) + 1;
    return state.refs[pitch] === 1;
  }

  function decRef(state, pitch) {
    var n = state.refs[pitch] || 0;
    if (n <= 1) { delete state.refs[pitch]; return true; }
    state.refs[pitch] = n - 1;
    return false;
  }

  // Returns a time-ordered event list {type, pitch, velocity}. Offs before
  // ons when both happen. `passthrough` is true when there is no harmony
  // (the incoming pitch is sent unchanged).
  function noteOn(state, pitch, vel, opts) {
    pitch = clamp7(pitch);
    vel = Math.round(+vel || 0);
    if (vel <= 0) { return noteOff(state, pitch); }
    var ev = [];
    if (state.held[pitch]) { ev = ev.concat(noteOff(state, pitch)); }
    var out = mapPitch(state.harmony, pitch, opts);
    state.held[pitch] = { out: out, vel: vel };
    if (incRef(state, out)) { ev.push({ type: 'on', pitch: out, velocity: vel }); }
    return ev;
  }

  function noteOff(state, pitch) {
    pitch = clamp7(pitch);
    var h = state.held[pitch];
    if (!h) { return []; }
    delete state.held[pitch];
    if (decRef(state, h.out)) { return [{ type: 'off', pitch: h.out, velocity: 0 }]; }
    return [];
  }

  // Re-map every held input to the (new) harmony. Used on chord change
  // when Retrigger is on, and on role/snap/range changes while notes are
  // held. Offs for abandoned output pitches come first.
  function retrigger(state, opts) {
    var ev = [], inputs = [], k, i, h, out, first;
    for (k in state.held) {
      if (state.held.hasOwnProperty(k)) { inputs.push(+k); }
    }
    inputs.sort(function (a, b) { return a - b; });
    for (i = 0; i < inputs.length; i++) {
      h = state.held[inputs[i]];
      out = mapPitch(state.harmony, inputs[i], opts);
      if (out === h.out) { continue; }
      first = decRef(state, h.out);
      if (first) { ev.push({ type: 'off', pitch: h.out, velocity: 0 }); }
      h.out = out;
      if (incRef(state, out)) { ev.push({ type: 'on', pitch: out, velocity: h.vel }); }
    }
    return ev;
  }

  function flush(state) {
    var ev = [], k, pitches = [], i;
    for (k in state.refs) {
      if (state.refs.hasOwnProperty(k) && state.refs[k] > 0) { pitches.push(+k); }
    }
    pitches.sort(function (a, b) { return a - b; });
    for (i = 0; i < pitches.length; i++) { ev.push({ type: 'off', pitch: pitches[i], velocity: 0 }); }
    state.held = {};
    state.refs = {};
    return ev;
  }

  function sounding(state) {
    var out = [], k;
    for (k in state.refs) {
      if (state.refs.hasOwnProperty(k) && state.refs[k] > 0) { out.push(+k); }
    }
    out.sort(function (a, b) { return a - b; });
    return out;
  }

  return {
    NEAREST: NEAREST, UP: UP, DOWN: DOWN,
    TONES: TONES, SCALE: SCALE, BASS: BASS, FOLLOW: FOLLOW,
    snapToPcs: snapToPcs,
    snapBass: snapBass,
    snapFollow: snapFollow,
    constrainRange: constrainRange,
    mapPitch: mapPitch,
    create: create,
    noteOn: noteOn,
    noteOff: noteOff,
    retrigger: retrigger,
    flush: flush,
    sounding: sounding
  };
})();
