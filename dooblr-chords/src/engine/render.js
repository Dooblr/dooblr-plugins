// dooblr-chords engine: turn voiced chords into clip notes (ES5).
//
// Extension points:
//  - PATTERNS: rhythm patterns. Each returns hits {offset, length, vel} in
//    beats for one chord slot. Only 'sustain' ships so far.
//  - opts.strum: beats between successive voices (0 = off), low to high.
//  The note format matches Live's add_new_notes: {pitch, start_time,
//  duration, velocity, mute}.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.render = (function () {
  var R = DC.rng;

  var PATTERNS = {
    sustain: function (slotBeats) { return [{ offset: 0, length: slotBeats, vel: 1 }]; }
  };

  function round4(x) { return Math.round(x * 10000) / 10000; }
  function clampVel(v) { return Math.max(1, Math.min(127, Math.round(v))); }
  function uniquePitches(list) {
    var out = [], seen = {}, i;
    for (i = 0; i < list.length; i++) {
      if (!seen[list[i]]) { seen[list[i]] = true; out.push(list[i]); }
    }
    return out;
  }

  // voicings: array of {notes: [...]} ; opts: {chordBeats, velocity,
  // humanize (0..100), seed, pattern, strum, slots}
  // slots (optional): [{start, duration}] per chord, in beats, for an uneven
  // harmonic rhythm; without it every chord lasts chordBeats.
  function notes(voicings, opts) {
    var beats = opts.chordBeats, h = Math.max(0, Math.min(100, opts.humanize || 0)) / 100;
    var pattern = PATTERNS[opts.pattern || 'sustain'] || PATTERNS.sustain;
    var strum = opts.strum || 0, out = [], noteIndex = 0;
    for (var i = 0; i < voicings.length; i++) {
      var slotBeats = (opts.slots && opts.slots[i]) ? opts.slots[i].duration : beats;
      var slotStart = (opts.slots && opts.slots[i]) ? opts.slots[i].start : i * beats, slotEnd = slotStart + slotBeats;
      var nextStart = (opts.slots && opts.slots[i + 1]) ? opts.slots[i + 1].start
        : (i + 1 < voicings.length ? (i + 1) * beats : slotEnd);
      var chordEnd = Math.min(slotEnd, nextStart);
      var hits = pattern(slotBeats, i);
      var v = uniquePitches(voicings[i].notes);
      for (var j = 0; j < hits.length; j++) {
        for (var k = 0; k < v.length; k++) {
          var start = slotStart + hits[j].offset + strum * k;
          var vel = opts.velocity * hits[j].vel;
          if (h > 0) {
            var s = R.stream(opts.seed, noteIndex, R.P_HUMANIZE);
            start += s.signed() * 0.05 * h;
            vel += s.signed() * 14 * h;
          }
          noteIndex++;
          var end = Math.min(chordEnd, slotStart + hits[j].offset + hits[j].length);
          if (start < slotStart) { start = slotStart; }
          if (start < 0) { start = 0; }
          if (start >= end) { start = Math.max(slotStart, end - 0.0625); }
          if (start >= end) { continue; }
          var dur = end - start;
          out.push({ pitch: v[k], start_time: round4(start), duration: round4(dur), velocity: clampVel(vel), mute: 0 });
        }
      }
    }
    return out;
  }

  return { PATTERNS: PATTERNS, notes: notes };
})();
