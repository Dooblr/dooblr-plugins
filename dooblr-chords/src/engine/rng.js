// dooblr-chords engine: seeded random numbers (ES5, host independent).
// mulberry32 with a hand-rolled 32-bit multiply, because Math.imul is ES6.
var DC = (typeof DC !== 'undefined') ? DC : {};

DC.rng = (function () {
  function imul(a, b) {
    var ah = (a >>> 16) & 0xffff, al = a & 0xffff;
    var bh = (b >>> 16) & 0xffff, bl = b & 0xffff;
    return ((al * bl) + ((((ah * bl + al * bh) & 0xffff) << 16) >>> 0)) | 0;
  }

  // Mix any number of integers into one 32-bit hash (used to derive streams).
  function hash() {
    var h = 0x811c9dc5 | 0;
    for (var i = 0; i < arguments.length; i++) {
      var v = arguments[i] | 0;
      h = imul(h ^ v, 0x01000193);
      h ^= h >>> 15;
      h = imul(h, 0x2c1b3c6d);
      h ^= h >>> 12;
    }
    return h >>> 0;
  }

  function Stream(seed) {
    this.s = (seed >>> 0) || 0x9e3779b9;
  }
  // Uniform float in [0, 1).
  Stream.prototype.next = function () {
    this.s = (this.s + 0x6d2b79f5) >>> 0;
    var t = this.s;
    t = imul(t ^ (t >>> 15), t | 1);
    t ^= t + imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  Stream.prototype.int = function (n) {
    return Math.floor(this.next() * n);
  };
  // Signed float in [-1, 1).
  Stream.prototype.signed = function () {
    return this.next() * 2 - 1;
  };

  // Pick an index from a weight array using a uniform draw u in [0,1).
  // Returns -1 when every weight is zero.
  function pickIndex(weights, u) {
    var total = 0, i;
    for (i = 0; i < weights.length; i++) {
      if (weights[i] > 0) { total += weights[i]; }
    }
    if (total <= 0) { return -1; }
    var x = u * total;
    for (i = 0; i < weights.length; i++) {
      if (weights[i] > 0) {
        if (x < weights[i]) { return i; }
        x -= weights[i];
      }
    }
    for (i = weights.length - 1; i >= 0; i--) {
      if (weights[i] > 0) { return i; }
    }
    return -1;
  }

  // A stream that depends only on (seed, slot, purpose), so decisions for
  // one chord never shift when another chord is locked or changed.
  function stream(seed, slot, purpose) {
    return new Stream(hash(seed, slot, purpose));
  }

  return {
    hash: hash,
    create: function (seed) { return new Stream(seed); },
    stream: stream,
    pickIndex: pickIndex,
    // Stream purposes. Add new ones at the end; never renumber.
    P_DEGREE: 1, P_CHROMATIC: 2, P_COLOR: 3, P_INVERSION: 4, P_HUMANIZE: 5, P_START: 6, P_ENDING: 7,
    // Variation / B section (derive.js)
    P_VARPICK: 8, P_VARSUB: 9, P_BRHYTHM: 10, P_BREGION: 11, P_BWALK: 12, P_BEND: 13, P_BCHROM: 14
  };
})();
