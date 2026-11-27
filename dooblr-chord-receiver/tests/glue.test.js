// Runs the BUILT receiver script in a VM with a mocked Max runtime.
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { execFileSync } = require('child_process');

const ROOT = path.join(__dirname, '..');
const DEVICE = path.join(ROOT, 'device', 'dooblr-chord-receiver');
execFileSync(process.execPath, [path.join(ROOT, 'scripts', 'build.js')], { stdio: 'ignore' });

const STRIP = [
  'delete Object.assign;',
  'delete Object.entries;',
  'delete Object.values;',
  'delete Array.from;',
  'delete Array.of;',
  'delete Array.prototype.includes;',
  'delete Array.prototype.find;',
  'delete Array.prototype.findIndex;',
  'delete String.prototype.includes;',
  'delete String.prototype.startsWith;',
  'delete String.prototype.endsWith;',
  'delete String.prototype.repeat;',
  'delete Number.isInteger;',
  'delete Number.isNaN;',
  'delete Math.sign;',
  'delete Math.trunc;',
].join('\n');

function load(opts) {
  opts = opts || {};
  const ctx = {};
  vm.createContext(ctx);
  vm.runInContext(STRIP, ctx);
  const out = { outlets: [], posts: [], named: {}, now: opts.now || 1000 };
  ctx.post = (s) => out.posts.push(String(s));
  ctx.outlet = function () { out.outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  ctx.Date = { now: () => out.now };
  ctx.patcher = {
    getnamed: (n) => {
      if (!out.named[n]) {
        out.named[n] = { msgs: [], message: function () { this.msgs.push(Array.prototype.slice.call(arguments)); } };
      }
      return out.named[n];
    }
  };
  const live = { is_active: 1, is_playing: 1, observers: [] };
  function LiveAPI(cb, p) {
    if (typeof cb === 'function') { this._cb = cb; this._path = p; } else { this._path = cb; }
    this.id = '1';
    this.unquotedpath = this._path;
    live.observers.push(this);
  }
  LiveAPI.prototype.get = function (prop) {
    const VmArray = vm.runInContext('Array', ctx);
    const a = new VmArray();
    if (prop === 'is_active') { a.push(live.is_active); }
    else if (prop === 'is_playing') { a.push(live.is_playing); }
    else { a.push(0); }
    return a;
  };
  ctx.LiveAPI = LiveAPI;
  ctx.Task = function (fn, self) { this.fn = fn; this.self = self; this.interval = 0; this.running = false; };
  ctx.Task.prototype.repeat = function () { this.running = true; };
  ctx.Task.prototype['repeat'] = ctx.Task.prototype.repeat;
  ctx.Task.prototype.cancel = function () { this.running = false; };
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_chord_receiver.js'), 'utf8'), ctx, { filename: 'dooblr_chord_receiver.js' });
  const midi = () => out.outlets.filter((o) => o[0] === 0);
  const bus = () => out.outlets.filter((o) => o[0] === 1).map((o) => JSON.parse(o[1]));
  const lastState = () => {
    const s = out.outlets.filter((o) => o[0] === 2 && o[1] === 'setstate');
    return s.length ? JSON.parse(s[s.length - 1][2]) : null;
  };
  return { ctx, out, live, midi, bus, lastState };
}

function harmony(extra) {
  return JSON.stringify(Object.assign({
    type: 'harmony', v: 1, ch: 0, beat: 0, root: 0, mode: 1,
    scale: [0, 2, 3, 5, 7, 8, 10],
    chordRoot: 0, chord: [0, 3, 7, 10], bass: 0, bassMidi: 36, index: 0,
    name: 'Am7', lengthBeats: 4
  }, extra || {}));
}

test('init sends hello and shows NO SENDER', () => {
  const t = load();
  t.ctx.loadbang();
  assert.strictEqual(t.lastState().name, 'NO SENDER');
  assert.strictEqual(t.lastState().connected, false);
  t.ctx.init();
  const hellos = t.bus().filter((m) => m.type === 'hello');
  assert.ok(hellos.length >= 1);
  assert.strictEqual(hellos[0].ch, 0);
  assert.strictEqual(t.lastState().name, 'NO SENDER');
});

test('passthrough when no sender: notes leave unchanged, velocity kept', () => {
  const t = load();
  t.ctx.init();
  t.out.outlets.length = 0;
  t.ctx.note(60, 100);
  assert.deepStrictEqual(t.midi(), [[0, 144, 60, 100]]);
  t.out.outlets.length = 0;
  t.ctx.note(60, 0);
  assert.deepStrictEqual(t.midi(), [[0, 128, 60, 0]]);
});

test('TONES snaps to the nearest chord tone and preserves velocity', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ chord: [0, 3, 7], chordRoot: 0, name: 'Am' }));
  assert.strictEqual(t.lastState().connected, true);
  assert.strictEqual(t.lastState().name, 'Am');
  t.out.outlets.length = 0;
  t.ctx.note(61, 90); // C# -> C (60) or D? nearest to 61 of 60, 63, 67 is 60
  assert.deepStrictEqual(t.midi(), [[0, 144, 60, 90]]);
  t.out.outlets.length = 0;
  t.ctx.note(61, 0);
  assert.deepStrictEqual(t.midi(), [[0, 128, 60, 0]], 'note-off uses the snapped pitch');
});

test('ignores a sender on a different channel and stays in passthrough', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ ch: 2, name: 'G7' }));
  assert.strictEqual(t.lastState().connected, false);
  t.out.outlets.length = 0;
  t.ctx.note(64, 80);
  assert.deepStrictEqual(t.midi(), [[0, 144, 64, 80]]);
});

test('hello on a channel change, and a matching harmony connects', () => {
  const t = load();
  t.ctx.init();
  t.out.outlets.length = 0;
  t.ctx.param('channel', 1);
  const hellos = t.bus().filter((m) => m.type === 'hello');
  assert.ok(hellos.some((m) => m.ch === 1));
  t.ctx.bus(harmony({ ch: 1, name: 'Em9' }));
  assert.strictEqual(t.lastState().connected, true);
  assert.strictEqual(t.lastState().name, 'Em9');
  assert.strictEqual(t.lastState().channel, 1);
});

test('changing channel flushes held notes', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ chord: [0, 3, 7], name: 'Am' }));
  t.ctx.note(60, 100);
  t.out.outlets.length = 0;
  t.ctx.param('channel', 2);
  const offs = t.midi().filter((o) => o[1] === 128);
  assert.ok(offs.length >= 1, 'held note is flushed');
  assert.strictEqual(t.lastState().connected, false);
});

test('retrigger on chord change turns off the old pitch and on the new one', () => {
  const t = load();
  t.ctx.init();
  t.ctx.param('retrigger', 1);
  t.ctx.bus(harmony({ chord: [0, 3, 7], chordRoot: 0, name: 'Am' }));
  t.ctx.note(60, 100);
  t.out.outlets.length = 0;
  t.ctx.bus(harmony({ chord: [7, 11, 2], chordRoot: 7, name: 'G', index: 1 }));
  const evs = t.midi();
  assert.ok(evs.some((e) => e[1] === 128 && e[2] === 60), 'old snapped pitch off');
  assert.ok(evs.some((e) => e[1] === 144 && e[3] === 100), 'new pitch on at same velocity');
});

test('retrigger Off leaves a held note until the input note-off', () => {
  const t = load();
  t.ctx.init();
  t.ctx.param('retrigger', 0);
  t.ctx.bus(harmony({ chord: [0, 3, 7], name: 'Am' }));
  t.ctx.note(60, 100);
  t.out.outlets.length = 0;
  t.ctx.bus(harmony({ chord: [7, 11, 2], chordRoot: 7, name: 'G', index: 1 }));
  assert.strictEqual(t.midi().length, 0, 'held note is not retriggered');
  t.out.outlets.length = 0;
  t.ctx.note(60, 0);
  assert.ok(t.midi().some((e) => e[1] === 128 && e[2] === 60));
});

test('refcount: two inputs mapped to the same output stay on after one note-off', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ chord: [0, 3, 7], name: 'Am' }));
  // 60 and 61 both nearest to C (60) — 61 is 1 away from 60, 2 from 63
  t.ctx.note(60, 80);
  t.ctx.note(61, 90);
  t.out.outlets.length = 0;
  t.ctx.note(60, 0);
  assert.ok(!t.midi().some((e) => e[1] === 128), 'shared output is not cut by the first off');
  t.out.outlets.length = 0;
  t.ctx.note(61, 0);
  assert.ok(t.midi().some((e) => e[1] === 128 && e[2] === 60));
});

test('stale sender after 1000 ms returns to passthrough and flushes', () => {
  const t = load({ now: 5000 });
  t.ctx.init();
  t.ctx.bus(harmony({ name: 'Am' }));
  t.ctx.note(60, 100);
  assert.strictEqual(t.lastState().connected, true);
  t.out.now = 5000 + 1001;
  t.out.outlets.length = 0;
  t.ctx.tick();
  assert.strictEqual(t.lastState().connected, false);
  assert.strictEqual(t.lastState().name, 'NO SENDER');
  assert.ok(t.midi().some((e) => e[1] === 128));
  t.out.outlets.length = 0;
  t.ctx.note(64, 70);
  assert.deepStrictEqual(t.midi(), [[0, 144, 64, 70]], 'passthrough after stale');
});

test('notifydeleted flushes held notes', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ name: 'Am', chord: [0, 3, 7] }));
  t.ctx.note(64, 100);
  t.out.outlets.length = 0;
  t.ctx.notifydeleted();
  assert.ok(t.midi().some((e) => e[1] === 128));
});

test('transport stop flushes held notes', () => {
  const t = load();
  t.ctx.init();
  t.ctx.bus(harmony({ name: 'Am', chord: [0, 3, 7] }));
  t.ctx.note(64, 100);
  t.out.outlets.length = 0;
  const song = t.live.observers.filter((o) => o._path === 'live_set' && o._cb)[0];
  assert.ok(song, 'transport observer');
  const VmArray = vm.runInContext('Array', t.ctx);
  const args = new VmArray();
  args.push('is_playing');
  args.push(0);
  song._cb(args);
  assert.ok(t.midi().some((e) => e[1] === 128));
});

test('FOLLOW transposes a riff 0-11 upward from the reference root', () => {
  const t = load();
  t.ctx.init();
  t.ctx.param('role', 3); // FOLLOW
  t.ctx.param('refroot', 0); // C
  t.ctx.bus(harmony({ chordRoot: 7, chord: [7, 11, 2], name: 'G' }));
  t.out.outlets.length = 0;
  t.ctx.note(60, 100); // C4 + 7 = G4 67
  assert.deepStrictEqual(t.midi(), [[0, 144, 67, 100]]);
});

test('BASS maps to the advertised bass pitch class in the input octave', () => {
  const t = load();
  t.ctx.init();
  t.ctx.param('role', 2);
  t.ctx.bus(harmony({ bass: 7, bassMidi: 31, chord: [0, 3, 7], name: 'Am/G' }));
  t.out.outlets.length = 0;
  t.ctx.note(62, 80); // D4 (62) octave window 60-71 -> G 67
  assert.deepStrictEqual(t.midi(), [[0, 144, 67, 80]]);
});

test('unknown parameter is posted and ignored', () => {
  const t = load();
  t.ctx.init();
  t.ctx.param('bogus', 1);
  assert.ok(t.out.posts.some((p) => /unknown parameter bogus/.test(p)));
});
