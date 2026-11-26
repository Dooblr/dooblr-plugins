// dooblr-chords LIVE scheduler (ES5, host independent).
// Driven by a clock snapshot { playing, beats, barBeats, tempo }. The Max
// host polls Live's transport; unit tests call clock() directly.
//
// Design:
//   - Arm on the next bar line after transport starts (fire immediately if
//     already on a bar). Then loopPos = songBeats % lengthBeats, so the
//     progression stays in phase with the Set even if a poll jitters.
//   - Knob changes apply at the next chord boundary. Generate/Reroll in
//     LIVE apply at the next bar.
//   - EVOLVE mutates at cycle wrap (after 1 / 2 / 4 complete loops).
//   - Every note-on is booked into `held` and gets exactly one note-off.
//     Offs are always emitted before ons at the same time. The host must
//     never send CC 123 (that would also kill user-held passthrough notes).
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.scheduler = (function () {
  var EPS = 1e-6;

  function create(initial) {
    var s = {
      live: false,
      playing: false,
      started: false,
      armedBar: null,
      lastBeats: null,
      chordIndex: -1,
      cycle: 0,
      held: [],
      build: null,
      events: [],
      lengthBeats: 0,
      pending: null,
      evolveEvery: 0,
      jumpThresh: 8
    };
    if (initial) { applyInit(s, initial); }
    return s;
  }

  function applyInit(s, o) {
    if (typeof o.live === 'boolean') { s.live = o.live; }
    if (typeof o.evolveEvery === 'number') { s.evolveEvery = o.evolveEvery; }
    if (typeof o.jumpThresh === 'number') { s.jumpThresh = o.jumpThresh; }
    if (o.build) { setBuild(s, o.build, { immediate: true }); }
  }

  function setLive(s, on) {
    s.live = !!on;
    if (!s.live) {
      s.playing = false;
      s.started = false;
      s.armedBar = null;
      s.lastBeats = null;
      s.chordIndex = -1;
      s.cycle = 0;
    }
  }

  function setEvolveEvery(s, n) {
    s.evolveEvery = n > 0 ? n : 0;
  }

  function eventsFromBuild(build) {
    var ev = [], notes = (build && build.notes) ? build.notes : [], i, n;
    for (i = 0; i < notes.length; i++) {
      n = notes[i];
      if (n.mute || !n.duration || n.duration <= 0) { continue; }
      ev.push({
        pitch: n.pitch,
        velocity: n.velocity,
        start: n.start_time,
        end: n.start_time + n.duration,
        chord: typeof n.chord === 'number' ? n.chord : -1
      });
    }
    ev.sort(function (a, b) {
      if (a.start !== b.start) { return a.start - b.start; }
      return a.pitch - b.pitch;
    });
    return ev;
  }

  function setBuild(s, build, opts) {
    opts = opts || {};
    s.build = build || null;
    s.events = eventsFromBuild(build);
    s.lengthBeats = (build && build.lengthBeats) ? build.lengthBeats : 0;
    if (opts.resetCycle) { s.cycle = 0; }
  }

  // Queue a rebuilt progression. when = 'chord' (next chord boundary) or
  // 'bar' (next bar line). Applied inside process() when the boundary is
  // crossed, after any offs due at that instant and before the new ons.
  function queueBuild(s, build, when) {
    if (!s.live || !s.started || !s.playing) {
      setBuild(s, build);
      s.pending = null;
      return;
    }
    s.pending = { build: build, when: when || 'chord' };
  }

  function posMod(t, len) {
    if (!(len > 0)) { return 0; }
    var r = t % len;
    if (r < 0) { r += len; }
    // Snap a value that is numerically at the loop end back to 0, so a
    // poll landing exactly on a wrap reads chord 0 of the new cycle.
    if (r + EPS >= len) { r = 0; }
    return r;
  }

  function slotsOf(build) {
    if (build && build.slots && build.slots.length) { return build.slots; }
    if (build && build.display && build.display.length) {
      var out = [], t = 0, i, d;
      for (i = 0; i < build.display.length; i++) {
        d = build.display[i].beats || 0;
        out.push({ start: t, duration: d, end: t + d });
        t += d;
      }
      return out;
    }
    return [];
  }

  function chordAt(build, beats) {
    if (!build || !(build.lengthBeats > 0)) { return -1; }
    var t = posMod(beats, build.lengthBeats), slots = slotsOf(build), i, last = 0;
    if (!slots.length) { return -1; }
    for (i = 0; i < slots.length; i++) {
      if (slots[i].start - EPS <= t) { last = i; }
    }
    return last;
  }

  function nextBar(beats, barBeats) {
    if (!(barBeats > 0)) { barBeats = 4; }
    var i = Math.floor((beats + EPS) / barBeats);
    var at = i * barBeats;
    if (beats <= at + EPS) { return at; }
    return (i + 1) * barBeats;
  }

  function crossed(prev, now, t) {
    // True when t is in (prev, now] (prev may be null).
    if (prev === null || prev === undefined) { return now + EPS >= t; }
    return prev + EPS < t && t <= now + EPS;
  }

  function barLines(prev, now, barBeats) {
    var out = [], t;
    if (!(barBeats > 0)) { return out; }
    t = nextBar((prev === null || prev === undefined) ? now : prev + EPS * 2, barBeats);
    while (t <= now + EPS) {
      if (prev === null || prev === undefined || t > prev + EPS) { out.push(t); }
      t += barBeats;
      if (out.length > 64) { break; }
    }
    return out;
  }

  function cycleIndex(beats, len) {
    if (!(len > 0)) { return 0; }
    return Math.floor((beats + EPS) / len);
  }

  function stamp(out, type, pitch, vel, t) {
    var ev = { type: type, pitch: pitch, velocity: vel };
    if (typeof t === 'number') { ev.t = t; }
    out.events.push(ev);
    if (type === 'off') { out.offs.push({ pitch: pitch, velocity: 0 }); }
    else { out.ons.push({ pitch: pitch, velocity: vel }); }
  }

  function emitOffPitch(s, out, pitch, t) {
    var kept = [], i, found = false;
    for (i = 0; i < s.held.length; i++) {
      if (!found && s.held[i].pitch === pitch) { found = true; }
      else { kept.push(s.held[i]); }
    }
    if (found) {
      stamp(out, 'off', pitch, 0, t);
      s.held = kept;
    }
  }

  function emitOffAll(s, out, t) {
    var i, held = s.held;
    s.held = [];
    for (i = 0; i < held.length; i++) {
      stamp(out, 'off', held[i].pitch, 0, t);
    }
  }

  function emitOn(s, out, pitch, vel, t) {
    var i, held = false;
    for (i = 0; i < s.held.length; i++) {
      if (s.held[i].pitch === pitch) { held = true; break; }
    }
    if (held) { emitOffPitch(s, out, pitch, t); }
    s.held.push({ pitch: pitch, velocity: vel });
    stamp(out, 'on', pitch, vel, t);
  }

  function soundingAt(events, loopPos, lengthBeats) {
    var found = [], i, e, t = loopPos;
    for (i = 0; i < events.length; i++) {
      e = events[i];
      // Sounding on [start, end). Notes do not wrap past the loop end.
      if (e.start - EPS <= t && t < e.end - EPS) { found.push(e); }
    }
    return found;
  }

  function collectSpan(events, lengthBeats, t0, t1) {
    // Events whose start or end falls in (t0, t1], as absolute times.
    var hits = [], k0, k1, k, i, e, absS, absE, kMin, kMax;
    if (!(lengthBeats > 0) || t1 + EPS < t0) { return hits; }
    kMin = Math.floor(t0 / lengthBeats) - 1;
    kMax = Math.floor(t1 / lengthBeats) + 1;
    for (k = kMin; k <= kMax; k++) {
      for (i = 0; i < events.length; i++) {
        e = events[i];
        absS = k * lengthBeats + e.start;
        absE = k * lengthBeats + e.end;
        if (t0 + EPS < absS && absS <= t1 + EPS) {
          hits.push({ t: absS, type: 'on', pitch: e.pitch, velocity: e.velocity });
        }
        if (t0 + EPS < absE && absE <= t1 + EPS) {
          hits.push({ t: absE, type: 'off', pitch: e.pitch, velocity: 0 });
        }
      }
    }
    hits.sort(function (a, b) {
      if (a.t !== b.t) { return a.t - b.t; }
      if (a.type !== b.type) { return a.type === 'off' ? -1 : 1; }
      return a.pitch - b.pitch;
    });
    return hits;
  }

  function applyHits(s, out, hits) {
    var i, h;
    for (i = 0; i < hits.length; i++) {
      h = hits[i];
      if (h.type === 'off') { emitOffPitch(s, out, h.pitch, h.t); }
      else { emitOn(s, out, h.pitch, h.velocity, h.t); }
    }
  }

  function syncSounding(s, out, beats) {
    var want = soundingAt(s.events, posMod(beats, s.lengthBeats), s.lengthBeats);
    var have = {}, i, p;
    for (i = 0; i < s.held.length; i++) { have[s.held[i].pitch] = true; }
    // Off anything no longer sounding.
    var heldNow = s.held.slice();
    for (i = 0; i < heldNow.length; i++) {
      p = heldNow[i].pitch;
      var still = false, j;
      for (j = 0; j < want.length; j++) { if (want[j].pitch === p) { still = true; break; } }
      if (!still) { emitOffPitch(s, out, p, beats); }
    }
    have = {};
    for (i = 0; i < s.held.length; i++) { have[s.held[i].pitch] = true; }
    for (i = 0; i < want.length; i++) {
      if (!have[want[i].pitch]) { emitOn(s, out, want[i].pitch, want[i].velocity, beats); }
    }
  }

  function applyPendingIf(s, out, when, t) {
    if (!s.pending || s.pending.when !== when) { return false; }
    emitOffAll(s, out, t);
    setBuild(s, s.pending.build);
    s.pending = null;
    return true;
  }

  function maybeEvolve(s, out, cycle, hooks, t) {
    var n = s.evolveEvery;
    if (!(n > 0) || !(cycle > 0) || (cycle % n) !== 0) { return false; }
    if (!hooks || typeof hooks.evolve !== 'function') { return false; }
    var next = hooks.evolve(s.build, cycle);
    if (!next) { return false; }
    emitOffAll(s, out, t);
    setBuild(s, next);
    return true;
  }

  function resetTransport(s, out, t) {
    if (s.held.length) { emitOffAll(s, out, t); }
    s.playing = false;
    s.started = false;
    s.armedBar = null;
    s.lastBeats = null;
    s.chordIndex = -1;
    s.cycle = 0;
  }

  // Advance the scheduler to `clock`. Returns { offs, ons, chordIndex,
  // cycle, started }. The host should send offs, then ons, in that order.
  function process(s, clock, hooks) {
    var out = { offs: [], ons: [], events: [], chordIndex: s.chordIndex, cycle: s.cycle, started: s.started };
    var barBeats = (clock && clock.barBeats > 0) ? clock.barBeats : 4;
    var beats = clock && typeof clock.beats === 'number' ? clock.beats : 0;

    if (!s.live || !clock || !clock.playing) {
      resetTransport(s, out, beats);
      out.chordIndex = -1;
      out.cycle = 0;
      out.started = false;
      return out;
    }

    if (!s.playing) {
      s.playing = true;
      s.started = false;
      s.armedBar = nextBar(beats, barBeats);
      s.lastBeats = null;
      s.chordIndex = -1;
    }

    if (!s.started) {
      if (beats + EPS < s.armedBar) {
        out.started = false;
        out.chordIndex = -1;
        return out;
      }
      s.started = true;
      s.lastBeats = null; // treat the start as a sync onto the current position
    }

    if (!s.build || !(s.lengthBeats > 0)) {
      s.chordIndex = -1;
      s.lastBeats = beats;
      out.started = true;
      out.chordIndex = -1;
      return out;
    }

    // Only a rewind is a seek. Forward motion — even a coarse poll that
    // crosses several chords — walks the interval so offs, ons, pending
    // boundaries and EVOLVE all fire in order.
    var jump = s.lastBeats !== null && beats + EPS < s.lastBeats;
    var t0 = s.lastBeats;
    var t1 = beats;

    if (t0 === null || jump) {
      // Seek / first attack: drop everything, apply pending immediately so
      // the new position plays the latest queued progression, then sync.
      if (s.pending) {
        emitOffAll(s, out);
        setBuild(s, s.pending.build);
        s.pending = null;
      }
      s.cycle = cycleIndex(beats, s.lengthBeats);
      emitOffAll(s, out);
      syncSounding(s, out, beats);
    } else {
      // Walk (t0, t1], splicing bar-pending, chord-pending, and evolve at
      // the instants they fire. Pending bar applies at a bar line; pending
      // chord applies at a chord start (including loop wrap). Evolve fires
      // at cycle wraps before the new cycle's note-ons.
      var cuts = [], i, ev, slots, len = s.lengthBeats;

      function addCut(t, kind) {
        if (t0 + EPS < t && t <= t1 + EPS) { cuts.push({ t: t, kind: kind }); }
      }

      if (s.pending && s.pending.when === 'bar') {
        ev = barLines(t0, t1, barBeats);
        for (i = 0; i < ev.length; i++) { addCut(ev[i], 'bar'); }
      }
      // Chord boundaries of the CURRENT build, plus wraps (which are also
      // chord-0 starts). If a pending bar also rebuilds at that instant,
      // bar is applied first and later cuts use the new build — we only
      // precompute cuts for the current build and re-scan after a rebuild
      // if needed. For typical poll intervals (10ms) at most one cut lands
      // in the window, so a single pass is enough; a rebuild mid-span is
      // followed by a sync onto t of the cut, then a walk of the remainder.
      slots = slotsOf(s.build);
      for (i = 0; i < slots.length; i++) {
        var k0 = Math.floor(t0 / len) - 1, k1 = Math.floor(t1 / len) + 1, k, abs;
        for (k = k0; k <= k1; k++) {
          abs = k * len + slots[i].start;
          addCut(abs, 'chord');
        }
      }
      // Cycle wraps (also a chord boundary at start 0).
      var c0 = cycleIndex(t0, len) + 1, c1 = cycleIndex(t1, len);
      for (i = c0; i <= c1; i++) { addCut(i * len, 'wrap'); }

      cuts.sort(function (a, b) {
        if (a.t !== b.t) { return a.t - b.t; }
        var rank = { bar: 1, wrap: 2, chord: 3 };
        var ra = rank[a.kind] || 9, rb = rank[b.kind] || 9;
        return ra - rb;
      });

      // Dedup identical (t, kind).
      var uniq = [], last = null;
      for (i = 0; i < cuts.length; i++) {
        if (last && last.t === cuts[i].t && last.kind === cuts[i].kind) { continue; }
        uniq.push(cuts[i]);
        last = cuts[i];
      }
      cuts = uniq;

      var cursor = t0, ci;
      for (ci = 0; ci < cuts.length; ci++) {
        var cut = cuts[ci];
        var willRebuild = false;
        if (cut.kind === 'bar' && s.pending && s.pending.when === 'bar') { willRebuild = true; }
        else if (cut.kind === 'wrap') {
          var willCyc = cycleIndex(cut.t, s.lengthBeats);
          if (s.pending && s.pending.when === 'chord') { willRebuild = true; }
          if (s.evolveEvery > 0 && willCyc > 0 && (willCyc % s.evolveEvery) === 0 && hooks && typeof hooks.evolve === 'function') { willRebuild = true; }
        } else if (cut.kind === 'chord' && s.pending && s.pending.when === 'chord') { willRebuild = true; }

        if (cut.t > cursor + EPS) {
          var walkEnd = willRebuild ? (cut.t - EPS * 2) : cut.t;
          if (walkEnd > cursor + EPS) {
            applyHits(s, out, collectSpan(s.events, s.lengthBeats, cursor, walkEnd));
          }
        }
        cursor = Math.max(cursor, cut.t);

        var rebuilt = false;
        if (cut.kind === 'bar' && s.pending && s.pending.when === 'bar') {
          applyPendingIf(s, out, 'bar', cut.t);
          syncSounding(s, out, cut.t);
          rebuilt = true;
        } else if (cut.kind === 'wrap') {
          var cyc = cycleIndex(cut.t, s.lengthBeats);
          if (s.pending && s.pending.when === 'chord') {
            applyPendingIf(s, out, 'chord', cut.t);
            rebuilt = true;
          }
          if (maybeEvolve(s, out, cyc, hooks, cut.t)) { rebuilt = true; }
          s.cycle = cyc;
          if (rebuilt) { syncSounding(s, out, cut.t); }
        } else if (cut.kind === 'chord') {
          if (s.pending && s.pending.when === 'chord') {
            applyPendingIf(s, out, 'chord', cut.t);
            syncSounding(s, out, cut.t);
            rebuilt = true;
          }
        }
        // Precomputed later cuts belong to the previous build once we
        // replace it. Finish the remaining span on the new events.
        if (rebuilt) {
          if (t1 > cursor + EPS) {
            applyHits(s, out, collectSpan(s.events, s.lengthBeats, cursor, t1));
          }
          cursor = t1;
          break;
        }
      }
      if (t1 > cursor + EPS) {
        applyHits(s, out, collectSpan(s.events, s.lengthBeats, cursor, t1));
      }
    }

    s.cycle = cycleIndex(beats, s.lengthBeats);
    s.chordIndex = chordAt(s.build, beats);
    s.lastBeats = beats;
    out.chordIndex = s.chordIndex;
    out.cycle = s.cycle;
    out.started = s.started;
    return out;
  }

  // Test helper: unique pitches currently held.
  function heldPitches(s) {
    var out = [], i, seen = {};
    for (i = 0; i < s.held.length; i++) {
      if (!seen[s.held[i].pitch]) {
        seen[s.held[i].pitch] = true;
        out.push(s.held[i].pitch);
      }
    }
    return out;
  }

  return {
    create: create,
    setLive: setLive,
    setEvolveEvery: setEvolveEvery,
    setBuild: setBuild,
    queueBuild: queueBuild,
    process: process,
    chordAt: chordAt, slotsOf: slotsOf,
    nextBar: nextBar,
    posMod: posMod,
    cycleIndex: cycleIndex,
    eventsFromBuild: eventsFromBuild,
    heldPitches: heldPitches,
    soundingAt: soundingAt
  };
})();
