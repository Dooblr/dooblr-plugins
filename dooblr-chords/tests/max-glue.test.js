// Runs the BUILT device scripts (device/dooblr-chords/*.js) in a VM with a
// mocked Max runtime (outlet, post, patcher, LiveAPI, mgraphics) and ES2015+
// built-ins removed. This checks the glue logic, not Live itself.
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { execFileSync } = require('child_process');
const { STRIP } = require('./load-engine');

const ROOT = path.join(__dirname, '..');
const DEVICE = path.join(ROOT, 'device', 'dooblr-chords');
execFileSync(process.execPath, [path.join(ROOT, 'scripts', 'build.js')], { stdio: 'ignore' });

// ---- mock Live set ------------------------------------------------------------

function makeLive(ctx, opts) {
  const VmArray = vm.runInContext('Array', ctx);
  const arr = (xs) => { const a = new VmArray(); xs.forEach((x) => a.push(x)); return a; };
  const live = {
    devicePath: opts.devicePath || 'live_set tracks 0 devices 0',
    slots: (opts.slots || [1, 0, 0]).map((h) => ({ clip: h ? { name: 'existing', notes: [{}], length: 4 } : null })),
    addNewNotesWorks: opts.addNewNotesWorks !== false,
    calls: []
  };
  function typeOf(p) {
    const t = p.split(' ');
    if (p === 'live_set') { return 'Song'; }
    const kind = t[t.length - 2];
    return { tracks: 'Track', devices: 'Device', chains: 'Chain', clip_slots: 'ClipSlot' }[kind] || (t[t.length - 1] === 'clip' ? 'Clip' : 'Unknown');
  }
  function LiveAPI(p) {
    let path = p === 'this_device' ? live.devicePath : String(p);
    if (/ canonical_parent$/.test(path)) {
      const base = path.replace(/ canonical_parent$/, '').split(' ');
      path = base.length <= 2 ? 'live_set' : base.slice(0, base.length - 2).join(' ');
    }
    this.unquotedpath = path;
    this.type = typeOf(path);
    this.id = '1';
    const m = /clip_slots (\d+)( clip)?$/.exec(path);
    this._slot = m ? live.slots[+m[1]] : null;
    if (this.type === 'Clip' && !(this._slot && this._slot.clip)) { this.id = '0'; }
  }
  LiveAPI.prototype.get = function (prop) {
    if (prop === 'has_clip') { return arr([this._slot && this._slot.clip ? 1 : 0]); }
    return arr([0]);
  };
  LiveAPI.prototype.getcount = function (prop) { return prop === 'clip_slots' && this.type === 'Track' ? live.slots.length : 0; };
  LiveAPI.prototype.set = function (prop, value) {
    live.calls.push(['set', prop, value]);
    if (this.type === 'Clip' && prop === 'name') { this._slot.clip.name = value; }
  };
  LiveAPI.prototype.call = function () {
    const args = Array.prototype.slice.call(arguments);
    live.calls.push(['call'].concat(args.map((a) => (typeof a === 'object' ? '[object]' : a))));
    const name = args[0];
    const clip = this._slot && this._slot.clip;
    if (name === 'create_clip') {
      assert.strictEqual(this.type, 'ClipSlot');
      assert.ok(!this._slot.clip, 'create_clip on a full slot');
      this._slot.clip = { name: '', length: args[1], notes: [], pending: null };
    } else if (name === 'add_new_notes') {
      if (live.addNewNotesWorks) { args[1].notes.forEach((n) => clip.notes.push(JSON.parse(JSON.stringify(n)))); }
    } else if (name === 'get_notes_extended') {
      return JSON.stringify({ notes: clip.notes });
    } else if (name === 'remove_notes_extended') {
      clip.notes = [];
    } else if (name === 'set_notes') {
      clip.pending = [];
    } else if (name === 'note') {
      clip.pending.push({ pitch: args[1], start_time: +args[2], duration: +args[3], velocity: args[4], mute: args[5] });
    } else if (name === 'done') {
      clip.notes = clip.notes.concat(clip.pending); clip.pending = null;
    }
    return 0;
  };
  return { live, LiveAPI };
}

function loadController(opts) {
  opts = opts || {};
  const ctx = {};
  vm.createContext(ctx);
  vm.runInContext(STRIP, ctx);
  const out = { outlets: [], posts: [], named: {} };
  ctx.post = (s) => out.posts.push(String(s));
  ctx.outlet = function () { out.outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  ctx.patcher = { getnamed: (n) => {
    if (!out.named[n]) { out.named[n] = { msgs: [], message: function () { this.msgs.push(Array.prototype.slice.call(arguments)); } }; }
    return out.named[n];
  } };
  const mock = makeLive(ctx, opts);
  ctx.LiveAPI = mock.LiveAPI;
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_chords.js'), 'utf8'), ctx, { filename: 'dooblr_chords.js' });
  const lastState = () => {
    const s = out.outlets.filter((o) => o[0] === 0 && o[1] === 'setstate');
    return s.length ? JSON.parse(s[s.length - 1][2]) : null;
  };
  const lastActive = (name) => {
    const m = (out.named[name] ? out.named[name].msgs : []).filter((x) => x[0] === 'active');
    return m.length ? m[m.length - 1][1] : undefined;
  };
  return { ctx, out, live: mock.live, lastState, lastActive };
}

// ---- controller --------------------------------------------------------------

test('no Live API use before live.thisdevice, then writes into the first empty slot', () => {
  const t = loadController();
  t.ctx.loadbang();
  t.ctx.generate();
  assert.strictEqual(t.live.calls.length, 0, 'no LiveAPI calls before init');
  assert.match(t.lastState().status, /still loading/);
  assert.strictEqual(t.lastActive('generate'), 0);

  t.ctx.init();
  assert.strictEqual(t.lastActive('generate'), 1);
  t.ctx.generate();
  const clip = t.live.slots[1].clip;
  assert.ok(clip, 'clip created in slot index 1 (slot 0 is full)');
  assert.strictEqual(clip.length, 16);
  assert.strictEqual(clip.name, 'C min \u00B7 4 \u00B7 Col35 Chr10');
  assert.ok(clip.notes.length >= 4 * 3);
  clip.notes.forEach((n) => {
    assert.ok(n.pitch >= 0 && n.pitch <= 127);
    assert.ok(n.start_time >= 0 && n.start_time + n.duration <= 16 + 1e-9);
  });
  const seedSet = t.out.outlets.filter((o) => o[0] === 1);
  assert.strictEqual(seedSet[seedSet.length - 1][1], 'set');
  assert.strictEqual(t.lastState().seed, seedSet[seedSet.length - 1][2]);
  assert.match(t.lastState().status, /slot 2/);
  assert.strictEqual(t.lastState().level, 'ok');
});

test('fills slots in order and reports a full track without creating scenes', () => {
  const t = loadController({ slots: [0, 0] });
  t.ctx.init();
  t.ctx.generate();
  t.ctx.write();
  assert.ok(t.live.slots[0].clip && t.live.slots[1].clip);
  assert.deepStrictEqual(t.live.slots[0].clip.notes, t.live.slots[1].clip.notes, 'Write repeats the current progression');
  const before = t.live.calls.length;
  t.ctx.generate();
  assert.match(t.lastState().status, /Every clip slot on this track is full/);
  assert.strictEqual(t.lastState().level, 'warn');
  assert.ok(!t.live.calls.slice(before).some((c) => c[1] === 'create_clip' || c[1] === 'create_scene'));
});

test('falls back to set_notes when add_new_notes adds nothing', () => {
  const t = loadController({ addNewNotesWorks: false });
  t.ctx.init();
  t.ctx.generate();
  const clip = t.live.slots[1].clip;
  assert.ok(t.live.calls.some((c) => c[1] === 'set_notes'));
  assert.ok(t.live.calls.some((c) => c[1] === 'done'));
  assert.ok(clip.notes.length >= 12);
  assert.strictEqual(t.lastState().level, 'ok');
});

test('finds the track from inside a rack', () => {
  const t = loadController({ devicePath: 'live_set tracks 0 devices 1 chains 0 devices 0' });
  t.ctx.init();
  t.ctx.generate();
  assert.ok(t.live.slots[1].clip);
});

test('parameters update the preview without writing', () => {
  const t = loadController();
  t.ctx.init();
  t.ctx.param('mode', 0);
  t.ctx.param('root', 7);
  t.ctx.param('length', 8);
  assert.strictEqual(t.lastState().chords.length, 8);
  assert.strictEqual(t.lastState().key, 'G Major');
  assert.strictEqual(t.live.calls.length, 0);
  t.ctx.param('bogus', 1);
  assert.ok(t.out.posts.some((p) => /unknown parameter bogus/.test(p)));
});

test('lock and reroll keep locked chords, Reroll is dimmed without locks', () => {
  const t = loadController({ slots: [0, 0, 0, 0] });
  t.ctx.init();
  t.ctx.param('chromatic', 60);
  t.ctx.param('color', 70);
  t.ctx.generate();
  assert.strictEqual(t.lastActive('reroll'), 0);
  t.ctx.reroll();
  assert.match(t.lastState().status, /Lock a chord first/);
  assert.ok(!t.live.slots[1].clip, 'reroll without locks writes nothing');

  const first = t.lastState().chords;
  t.ctx.lock(0);
  t.ctx.lock(2);
  assert.strictEqual(t.lastActive('reroll'), 1);
  assert.strictEqual(t.lastState().locked, 2);
  for (let k = 0; k < 2; k++) {
    t.ctx.reroll();
    const now = t.lastState().chords;
    assert.strictEqual(now[0].name, first[0].name);
    assert.strictEqual(now[2].name, first[2].name);
    assert.ok(now[0].locked && now[2].locked && !now[1].locked);
  }
  assert.ok(t.live.slots[2].clip, 'reroll writes a clip');
  t.ctx.lock(0);
  assert.strictEqual(t.lastState().locked, 1);
  t.ctx.unlockall();
  assert.strictEqual(t.lastActive('reroll'), 0);
});

test('Seed is drawn partly ignored when every chord is locked', () => {
  const t = loadController();
  t.ctx.init();
  t.ctx.param('length', 2);
  t.ctx.lock(0);
  t.ctx.lock(1);
  const msgs = t.out.named.seed.msgs.filter((m) => m[0] === 'textcolor');
  assert.strictEqual(msgs[msgs.length - 1][4], 0.6);
  t.ctx.lock(1);
  const again = t.out.named.seed.msgs.filter((m) => m[0] === 'textcolor');
  assert.strictEqual(again[again.length - 1][4], 1);
});

test('shrinking Length drops locks beyond the end', () => {
  const t = loadController();
  t.ctx.init();
  t.ctx.param('length', 6);
  t.ctx.lock(5);
  t.ctx.param('length', 4);
  assert.strictEqual(t.lastState().locked, 0);
});

// ---- display (jsui) -----------------------------------------------------------

function loadBlocks() {
  const ctx = {};
  vm.createContext(ctx);
  vm.runInContext(STRIP, ctx);
  const ops = [];
  const outlets = [];
  const mg = new Proxy({}, { get: (o, k) => {
    if (k === 'text_measure') { return (s) => { const a = new (vm.runInContext('Array', ctx))(); a.push(String(s).length * 6, 10); return a; }; }
    if (k in o) { return o[k]; }
    return function () { ops.push([k].concat(Array.prototype.slice.call(arguments))); };
  }, set: (o, k, v) => { o[k] = v; return true; } });
  ctx.mgraphics = mg;
  ctx.box = { rect: [158, 84, 752, 163], message: () => {} };
  ctx.post = () => {};
  ctx.outlet = function () { outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_blocks.js'), 'utf8'), ctx, { filename: 'dooblr_blocks.js' });
  return { ctx, ops, outlets };
}

test('display paints 4 and 12 chords and maps clicks to lock messages', () => {
  const b = loadBlocks();
  const chords = (n) => Array.from({ length: n }, (_, i) => ({ name: 'C\u266Dmaj7', roman: '\u266DII', func: 'PD', source: 'diatonic', locked: i === 1 }));
  b.ctx.setstate(JSON.stringify({ chords: chords(4), key: 'C Natural Minor', status: 'Wrote x to slot 2.', level: 'ok', locked: 1 }));
  b.ctx.paint();
  const texts = b.ops.filter((o) => o[0] === 'show_text').map((o) => o[1]);
  assert.ok(texts.indexOf('PROGRESSION') >= 0);
  assert.strictEqual(texts.filter((s) => s === 'C\u266Dmaj7').length, 4);
  b.ctx.onclick(10, 40);
  b.ctx.onclick(594 - 5, 70);
  b.ctx.onclick(10, 2); // header: no lock
  assert.deepStrictEqual(b.outlets.map((o) => o.slice()), [[0, 'lock', 0], [0, 'lock', 3]]);

  b.ops.length = 0;
  b.ctx.setstate(JSON.stringify({ chords: chords(12), key: 'C', status: '', level: 'info', locked: 0 }));
  b.ctx.paint();
  assert.strictEqual(b.ops.filter((o) => o[0] === 'show_text' && o[1] === 'C\u266Dmaj7').length, 12);
  b.outlets.length = 0;
  b.ctx.onclick(5, 70); // second row, first block = index 6
  assert.deepStrictEqual(b.outlets[0].slice(), [0, 'lock', 6]);
});
