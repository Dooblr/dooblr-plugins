// dooblr-titan engine: seeded random numbers (ES5, host independent).
// mulberry32 with a hand-rolled 32-bit multiply, because Math.imul is ES6.
var DT = (typeof DT !== 'undefined') ? DT : {};

DT.rng = (function () {
  function imul(a, b) {
    var ah = (a >>> 16) & 0xffff, al = a & 0xffff;
    var bh = (b >>> 16) & 0xffff, bl = b & 0xffff;
    return ((al * bl) + ((((ah * bl + al * bh) & 0xffff) << 16) >>> 0)) | 0;
  }

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
  Stream.prototype.signed = function () {
    return this.next() * 2 - 1;
  };
  Stream.prototype.bool = function (p) {
    return this.next() < p;
  };

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

  function stream(seed, slot, purpose) {
    return new Stream(hash(seed, slot, purpose));
  }

  return {
    hash: hash,
    create: function (seed) { return new Stream(seed); },
    stream: stream,
    pickIndex: pickIndex,
    // Stream purposes. Add new ones at the end; never renumber.
    P_HUMANIZE: 1,
    P_SHIFT: 2,
    P_THIN: 3,
    P_FILL: 4,
    P_SUB: 5,
    P_OCT: 6,
    P_INV: 7,
    P_RETRO: 8,
    P_GHOST: 9,
    P_ACCENT: 10,
    P_CHAR: 11,
    P_VEL: 12
  };
})();
