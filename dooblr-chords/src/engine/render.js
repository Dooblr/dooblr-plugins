// dooblr-chords engine: turn voiced chords into clip notes (ES5).
//
// Extension points:
//  - PATTERNS: rhythm patterns. Each returns hits {offset, length, vel} in
//    beats for one chord slot. Only 'sustain' ships in v0.1.0.
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

  // voicings: array of {notes: [...]} ; opts: {chordBeats, velocity,
  // humanize (0..100), seed, pattern, strum}
  function notes(voicings, opts) {
    var beats = opts.chordBeats, h = Math.max(0, Math.min(100, opts.humanize || 0)) / 100;
    var pattern = PATTERNS[opts.pattern || 'sustain'] || PATTERNS.sustain;
    var strum = opts.strum || 0, out = [], noteIndex = 0;
    for (var i = 0; i < voicings.length; i++) {
      var slotStart = i * beats, slotEnd = slotStart + beats;
      var hits = pattern(beats, i);
      var v = voicings[i].notes;
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
          start = Math.max(slotStart === 0 ? 0 : slotStart - 0.05, start);
          if (start < 0) { start = 0; }
          var end = Math.min(slotEnd, slotStart + hits[j].offset + hits[j].length);
          var dur = Math.max(0.0625, end - start);
          out.push({ pitch: v[k], start_time: round4(start), duration: round4(dur), velocity: clampVel(vel), mute: 0 });
        }
      }
    }
    return out;
  }

  return { PATTERNS: PATTERNS, notes: notes };
})();
