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
const { buildSmf, chordNotes } = require('./smf');

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
    if (path === 'live_set view detail_clip' || path === 'live_set view highlighted_clip_slot clip') {
      live.calls.push(['path', path]);
      this.type = 'Clip';
      this._detail = path === 'live_set view detail_clip' ? opts.detailClip : opts.highlightedClip;
      this.id = this._detail ? '7' : '0';
      return;
    }
    const m = /clip_slots (\d+)( clip)?$/.exec(path);
    this._slot = m ? live.slots[+m[1]] : null;
    if (this.type === 'Clip' && !(this._slot && this._slot.clip)) { this.id = '0'; }
  }
  LiveAPI.prototype.get = function (prop) {
    if (this._detail) {
      const d = this._detail;
      const v = { is_midi_clip: d.audio ? 0 : 1, looping: d.looping ? 1 : 0, loop_start: d.loopStart || 0, loop_end: d.loopEnd || d.length,
        start_marker: d.startMarker || 0, end_marker: d.endMarker || d.length, length: d.length, name: d.name }[prop];
      return arr([v === undefined ? 0 : v]);
    }
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
    if (this._detail) {
      assert.strictEqual(name, 'get_notes_extended');
      const from = args[3], len = args[4];
      return JSON.stringify({ notes: this._detail.notes.filter((n) => n.start_time >= from && n.start_time < from + len) });
    }
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
  ctx.patcher = {
    bringtofront: function () {},
    sendtoback: function () {},
    getnamed: (n) => {
      if (!out.named[n]) {
        out.named[n] = {
          msgs: [],
          rect: null,
          message: function () {
            const args = Array.prototype.slice.call(arguments);
            this.msgs.push(args);
            if (args[0] === 'presentation_rect') {
              this.rect = [args[1], args[2], args[3], args[4]];
            }
            // live.text often outputs the value when it receives set.
            if (args[0] === 'set' && (n === 'open_advanced' || n === 'open_source') && ctx.openpanel) {
              ctx.openpanel(n === 'open_advanced' ? 'advanced' : 'source', args[1]);
            }
          }
        };
      }
      return out.named[n];
    }
  };
  const mock = makeLive(ctx, opts);
  ctx.LiveAPI = mock.LiveAPI;
  const files = opts.files || {};
  const VmArray = vm.runInContext('Array', ctx);
  ctx.File = function (p, mode) {
    assert.strictEqual(mode, 'read');
    this._data = files[p];
    this.isopen = !!this._data;
    this.eof = this._data ? this._data.length : 0;
    this.position = 0;
    out.fileOpens = (out.fileOpens || 0) + 1;
  };
  ctx.File.prototype.readbytes = function (n) {
    assert.ok(n > 0 && n <= 1024);
    const a = new VmArray();
    // Max may hand back signed bytes; the reader must mask them.
    this._data.slice(this.position, this.position + n).forEach((b) => a.push(b > 127 ? b - 256 : b));
    this.position += a.length;
    return a;
  };
  ctx.File.prototype.close = function () { this.closed = true; out.fileCloses = (out.fileCloses || 0) + 1; };
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_chords.js'), 'utf8'), ctx, { filename: 'dooblr_chords.js' });
  const lastState = () => {
    const s = out.outlets.filter((o) => o[0] === 0 && o[1] === 'setstate');
    return s.length ? JSON.parse(s[s.length - 1][2]) : null;
  };
  const lastActive = (name) => {
    const m = (out.named[name] ? out.named[name].msgs : []).filter((x) => x[0] === 'active');
    return m.length ? m[m.length - 1][1] : undefined;
  };
  const lastMsg = (name, sel) => {
    const m = (out.named[name] ? out.named[name].msgs : []).filter((x) => x[0] === sel);
    return m.length ? m[m.length - 1] : undefined;
  };
  return { ctx, out, live: mock.live, lastState, lastActive, lastMsg };
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
  // Seed is an advanced control: it dims from the muted colour, not white.
  assert.deepStrictEqual(msgs[msgs.length - 1].slice(1, 4), [0.541, 0.569, 0.612]);
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

// ---- source: dropped file, USE CLIP, VAR / B ----------------------------------

const AM_F_C_G = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]];
const RIFF = 'Macintosh HD:/Users/dan/MIDI/my riff.mid';

test('a dropped .mid file becomes the source; Generate writes a variation of it', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(chordNotes(AM_F_C_G, 4)) }, slots: [0, 0, 0] });
  t.ctx.init();
  assert.strictEqual(t.lastActive('kind'), 0, 'VAR/B is dimmed without a source');
  assert.strictEqual(t.lastActive('clearsource'), 0);
  t.ctx.dropfile(RIFF);
  assert.strictEqual(t.out.fileOpens, t.out.fileCloses, 'file closed');
  const st = t.lastState();
  assert.strictEqual(st.level, 'ok', st.status);
  assert.match(st.status, /^Loaded my riff: A Natural Minor, 4 chords\.$/);
  assert.deepStrictEqual(st.source.chords, ['Am', 'F', 'C', 'G']);
  assert.strictEqual(st.heading, 'VARIATION');
  assert.deepStrictEqual(t.lastMsg('root', 'set'), ['set', 9]);
  assert.deepStrictEqual(t.lastMsg('mode', 'set'), ['set', 1]);
  assert.deepStrictEqual(t.lastMsg('length', 'set'), ['set', 4]);
  assert.match(t.lastMsg('srcinfo', 'set')[1], /my riff .* A Natural Minor .* 4 chords, 4 bars/);
  assert.strictEqual(t.lastMsg('srcbadge', 'set')[1], 'VAR \u00B7 my riff');
  assert.ok(t.out.named.srcbadge.rect[0] < 2000, 'badge is on screen when a source is loaded');
  assert.strictEqual(t.lastActive('kind'), 1);
  assert.strictEqual(t.lastActive('clearsource'), 1);
  assert.strictEqual(t.live.calls.length, 0, 'loading a source writes nothing');

  t.ctx.generate();
  const clip = t.live.slots[0].clip;
  assert.ok(clip);
  assert.strictEqual(clip.length, 16);
  assert.match(clip.name, /^my riff \u00B7 VAR \u00B7 A min/);
  assert.strictEqual(t.lastState().chords[0].changed, false);

  t.ctx.param('kind', 1);
  assert.strictEqual(t.lastMsg('srcbadge', 'set')[1], 'B \u00B7 my riff');
  assert.strictEqual(t.lastState().heading, 'B SECTION');
  assert.strictEqual(t.lastState().region.length > 0, true, 'B section state carries its region for the header');
  assert.ok(t.lastState().key.indexOf(t.lastState().region) > 0);
  t.ctx.write();
  assert.match(t.live.slots[1].clip.name, /^my riff \u00B7 B /);
  const total = t.lastState().chords.reduce((x, c) => x + c.beats, 0);
  assert.strictEqual(t.live.slots[1].clip.length, total);

  t.ctx.clearsource();
  assert.strictEqual(t.lastState().source, null);
  assert.strictEqual(t.lastState().heading, 'PROGRESSION');
  assert.ok(t.lastMsg('srcdrop', 'clear'));
  assert.strictEqual(t.lastActive('kind'), 0);
  assert.ok(t.out.named.srcbadge.rect[0] >= 2000, 'badge is parked when the source is cleared');
});

test('dropped files that are not usable MIDI report a warning and keep the old source', () => {
  const bad = buildSmf(chordNotes(AM_F_C_G, 4)).slice(0, 40); // cut mid-event
  const t = loadController({ files: { [RIFF]: buildSmf(chordNotes(AM_F_C_G, 4)), '/x/bad.mid': bad, '/x/empty.mid': buildSmf([]) } });
  t.ctx.init();
  t.ctx.dropfile('/x/loop.wav');
  assert.match(t.lastState().status, /Drop a MIDI file .* loop.wav is not one/);
  t.ctx.dropfile('/x/missing.mid');
  assert.match(t.lastState().status, /Could not read missing.mid/);
  t.ctx.dropfile(RIFF);
  t.ctx.dropfile('/x/bad.mid');
  assert.strictEqual(t.lastState().level, 'warn');
  assert.match(t.lastState().status, /^bad.mid: .*MIDI file/);
  t.ctx.dropfile('/x/empty.mid');
  assert.match(t.lastState().status, /^empty.mid: The clip has no notes/);
  assert.deepStrictEqual(t.lastState().source.chords, ['Am', 'F', 'C', 'G'], 'old source kept');
  t.ctx.dropfile();
  assert.deepStrictEqual(t.lastState().source.chords, ['Am', 'F', 'C', 'G'], 'empty drop ignored');
});

test('a path re-sent while the Set loads keeps the saved Root, Mode and Length', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(chordNotes(AM_F_C_G, 4)) } });
  t.ctx.loadbang();
  t.ctx.param('root', 2);
  t.ctx.param('mode', 2);
  t.ctx.param('length', 4);
  t.ctx.dropfile(RIFF);
  assert.strictEqual(t.lastMsg('root', 'set'), undefined);
  assert.strictEqual(t.lastState().key, 'D Dorian');
  assert.ok(t.lastState().source);
  assert.strictEqual(t.live.calls.length, 0);
});

function clipNotes(chords, beats, offset) {
  return chordNotes(chords, beats).map((n) => ({ pitch: n.pitch, start_time: n.start + (offset || 0), duration: n.duration, velocity: 90, mute: 0 }));
}

test('USE CLIP reads the Detail View clip loop, shifted to start at 0', () => {
  const notes = clipNotes([[50, 53, 57], [43, 55, 59, 62]], 4, 8).concat(clipNotes([[48, 52, 55]], 4, 0));
  const t = loadController({ detailClip: { name: 'Chorus', length: 16, looping: true, loopStart: 8, loopEnd: 16, notes } });
  t.ctx.loadbang();
  t.ctx.useclip();
  assert.match(t.lastState().status, /still loading/);
  t.ctx.init();
  t.ctx.useclip();
  const st = t.lastState();
  assert.strictEqual(st.level, 'ok', st.status);
  assert.deepStrictEqual(st.source.chords.length, 2);
  assert.strictEqual(st.source.name, 'Chorus');
  assert.strictEqual(st.source.bars, 2);
  assert.ok(t.live.calls.some((c) => c[0] === 'path' && c[1] === 'live_set view detail_clip'));
  assert.ok(t.lastMsg('srcdrop', 'clear'), 'a clip source clears the saved file path');
});

test('USE CLIP falls back to the highlighted slot and explains audio or missing clips', () => {
  let t = loadController({ highlightedClip: { name: 'Pad', length: 8, notes: clipNotes(AM_F_C_G.slice(0, 2), 4) } });
  t.ctx.init();
  t.ctx.useclip();
  assert.strictEqual(t.lastState().source.name, 'Pad');

  t = loadController({});
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastState().status, /No clip is selected/);

  t = loadController({ detailClip: { audio: true, length: 8, notes: [] } });
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastState().status, /audio clip/);

  t = loadController({ detailClip: { name: 'Empty', length: 8, notes: [] } });
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastState().status, /Selected clip: The clip has no notes/);
});

test('with a source, locks survive Reroll and switching VAR/B clears them', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(chordNotes(AM_F_C_G, 4)) }, slots: [0, 0, 0, 0, 0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.param('kind', 1);
  t.ctx.generate();
  const first = t.lastState().chords;
  t.ctx.lock(0);
  for (let k = 0; k < 3; k++) {
    t.ctx.reroll();
    const now = t.lastState().chords;
    assert.strictEqual(now.length, first.length, 'rhythm plan kept while locked');
    assert.strictEqual(now[0].name, first[0].name);
  }
  t.ctx.param('kind', 0);
  assert.strictEqual(t.lastState().locked, 0);
});

// ---- display (jsui) -----------------------------------------------------------

function loadBlocks() {
  const ctx = {};
  vm.createContext(ctx);
  vm.runInContext(STRIP, ctx);
  const ops = [];
  const outlets = [];
  const annotations = [];
  const mg = new Proxy({}, { get: (o, k) => {
    if (k === 'text_measure') { return (s) => { const a = new (vm.runInContext('Array', ctx))(); a.push(String(s).length * 6, 10); return a; }; }
    if (k in o) { return o[k]; }
    return function () { ops.push([k].concat(Array.prototype.slice.call(arguments))); };
  }, set: (o, k, v) => { o[k] = v; return true; } });
  ctx.mgraphics = mg;
  ctx.box = { rect: [136, 99, 632, 165], message: (...a) => { annotations.push(a); } };
  ctx.post = () => {};
  ctx.outlet = function () { outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_blocks.js'), 'utf8'), ctx, { filename: 'dooblr_blocks.js' });
  return { ctx, ops, outlets, annotations };
}

test('display paints 4 and 12 chords and maps clicks to lock messages', () => {
  const b = loadBlocks();
  const chords = (n) => Array.from({ length: n }, (_, i) => ({ name: 'C\u266Dmaj7', roman: '\u266DII', func: 'PD', source: 'diatonic', locked: i === 1 }));
  b.ctx.setstate(JSON.stringify({ chords: chords(4), key: 'C Natural Minor', status: 'Wrote x to slot 2.', level: 'ok', locked: 1 }));
  b.ctx.paint();
  const texts = b.ops.filter((o) => o[0] === 'show_text').map((o) => o[1]);
  assert.ok(texts.indexOf('PROGRESSION') >= 0);
  assert.strictEqual(texts.filter((s) => s === 'C\u266Dmaj7').length, 4);
  assert.ok(texts.indexOf('Wrote x to slot 2.') >= 0);
  assert.ok(texts.indexOf('C Natural Minor') < 0, 'the key is on the menus, not repeated in the header');
  assert.ok(texts.indexOf('\u266DII') >= 0, 'one-row blocks show the Roman numeral');
  assert.ok(b.annotations.some((m) => m[0] === 'annotation' && /Status: Wrote x to slot 2\./.test(m[1])));
  b.ctx.onclick(10, 40);
  b.ctx.onclick(496 - 5, 50);
  b.ctx.onclick(10, 2); // header: no lock
  assert.deepStrictEqual(b.outlets.map((o) => o.slice()), [[0, 'lock', 0], [0, 'lock', 3]]);

  b.ops.length = 0;
  b.ctx.setstate(JSON.stringify({ chords: chords(12), key: 'C', status: '', level: 'info', locked: 0 }));
  b.ctx.paint();
  assert.strictEqual(b.ops.filter((o) => o[0] === 'show_text' && o[1] === 'C\u266Dmaj7').length, 12);
  assert.strictEqual(b.ops.filter((o) => o[0] === 'show_text' && o[1] === '\u266DII').length, 12, 'two rows still fit the numeral');
  b.outlets.length = 0;
  b.ctx.onclick(5, 60); // second row, first block = index 6
  assert.deepStrictEqual(b.outlets[0].slice(), [0, 'lock', 6]);
});

test('display shows the source strip, the VARIATION heading and changed chords', () => {
  const b = loadBlocks();
  const chords = [0, 1, 2, 3].map((i) => ({ name: 'Am' + i, roman: 'i', func: 'T', source: 'diatonic', locked: false, changed: i === 2, beats: 4 }));
  b.ctx.setstate(JSON.stringify({ chords, heading: 'VARIATION', key: 'A Natural Minor', status: '', level: 'ok', locked: 0,
    source: { name: 'riff', detectedKey: 'A Natural Minor', chords: ['Am', 'F', 'C', 'G'] } }));
  b.ctx.paint();
  const texts = b.ops.filter((o) => o[0] === 'show_text').map((o) => o[1]);
  assert.ok(texts.indexOf('VARIATION') >= 0);
  assert.ok(texts.indexOf('SOURCE') >= 0);
  assert.ok(texts.some((s) => /Am {3}F {3}C {3}G/.test(s)));
  assert.ok(!texts.some((s) => /riff/.test(s)), 'the source name is on the badge and in the Source panel, not the strip');
  assert.strictEqual(b.ops.filter((o) => o[0] === 'ellipse').length, 1, 'one changed marker');
  b.ctx.onclick(10, 20); // source strip: no lock
  b.ctx.onclick(10, 50);
  assert.deepStrictEqual(b.outlets.map((o) => o.slice()), [[0, 'lock', 0]]);
});

test('overlay panels start closed, toggle from js, and reset on init', () => {
  const t = loadController();
  const rectOf = (name) => t.out.named[name] && t.out.named[name].rect;
  const parked = (name) => { const r = rectOf(name); assert.ok(r && r[0] >= 2000, name + ' should be parked, got ' + r); };
  const shown = (name, expected) => {
    const r = rectOf(name);
    assert.ok(r && r[0] < 2000, name + ' should be on screen, got ' + r);
    if (expected) { assert.deepStrictEqual(r, expected); }
  };
  t.ctx.loadbang();
  ['adv_bg', 'adv_title', 'adv_close', 'duration', 'seed', 'write', 'register', 'spread',
    'velocity', 'humanize', 'bass', 'src_bg', 'src_title', 'src_close', 'srcdrop',
    'useclip', 'clearsource', 'kind', 'srcinfo'].forEach(parked);
  ['root', 'mode', 'length', 'color', 'chromatic', 'blocks', 'key_label'].forEach((n) => shown(n));
  assert.deepStrictEqual(t.lastMsg('open_advanced', 'set'), ['set', 0]);
  assert.deepStrictEqual(t.lastMsg('open_source', 'set'), ['set', 0]);

  t.ctx.openpanel('advanced', 1);
  shown('adv_bg', [128, 0, 512, 169]);
  shown('duration');
  shown('write');
  shown('register');
  parked('src_bg');
  parked('srcdrop');
  parked('root');
  parked('blocks');
  parked('color');
  assert.deepStrictEqual(t.lastMsg('open_advanced', 'set'), ['set', 1]);
  assert.deepStrictEqual(t.lastMsg('open_source', 'set'), ['set', 0]);
  assert.ok(t.out.posts.some((p) => /panel open advanced/.test(p)));

  // A repeated 1 is ignored (idempotent); the set-echo from the opener must not close it.
  const rectsBefore = t.out.named.adv_bg.msgs.filter((m) => m[0] === 'presentation_rect').length;
  t.ctx.openpanel('advanced', 1);
  assert.strictEqual(t.out.named.adv_bg.msgs.filter((m) => m[0] === 'presentation_rect').length, rectsBefore);
  shown('adv_bg', [128, 0, 512, 169]);

  t.ctx.openpanel('source', 1);
  shown('src_bg', [128, 0, 512, 169]);
  shown('srcdrop');
  shown('kind');
  parked('adv_bg');
  parked('write');
  parked('root');
  assert.deepStrictEqual(t.lastMsg('open_advanced', 'set'), ['set', 0]);
  assert.deepStrictEqual(t.lastMsg('open_source', 'set'), ['set', 1]);
  assert.ok(t.out.posts.some((p) => /panel open source/.test(p)));

  t.ctx.openpanel('source', 0);
  parked('src_bg');
  parked('adv_bg');
  shown('root');
  shown('blocks');
  assert.deepStrictEqual(t.lastMsg('open_source', 'set'), ['set', 0]);
  assert.ok(t.out.posts.some((p) => /panel closed/.test(p)));

  t.ctx.openpanel('advanced', 1);
  t.ctx.init();
  parked('adv_bg');
  parked('src_bg');
  shown('blocks');
  assert.deepStrictEqual(t.lastMsg('open_advanced', 'set'), ['set', 0]);

  t.ctx.openpanel('advanced');
  shown('adv_bg', [128, 0, 512, 169]);
  t.ctx.openpanel('advanced');
  parked('adv_bg');
  t.ctx.closepanel();
  parked('adv_bg');
  shown('root');
});

test('Lead toggle writes a monophonic lead clip and relabels Generate', () => {
  const t = loadController({ slots: [0, 0, 0] });
  t.ctx.init();
  assert.strictEqual(t.lastActive('density'), 0, 'Density is dimmed while Lead is off');
  t.ctx.generate();
  assert.strictEqual(t.live.slots[0].clip.name, 'C min \u00B7 4 \u00B7 Col35 Chr10');

  t.ctx.param('lead', 1);
  assert.strictEqual(t.lastActive('density'), 1);
  assert.deepStrictEqual(t.lastMsg('generate', 'text'), ['text', 'Generate Lead']);
  assert.deepStrictEqual(t.lastMsg('generate', 'texton'), ['texton', 'Generate Lead']);
  assert.strictEqual(t.lastState().heading, 'LEAD');

  t.ctx.generate();
  const clip = t.live.slots[1].clip;
  assert.ok(clip);
  assert.match(clip.name, /^Lead \u00B7 C min \u00B7 4 \u00B7 Col/);
  const notes = clip.notes.slice().sort((a, b) => a.start_time - b.start_time);
  assert.ok(notes.length > 0);
  for (let i = 1; i < notes.length; i++) {
    assert.ok(notes[i - 1].start_time + notes[i - 1].duration <= notes[i].start_time + 1e-6);
  }
  assert.strictEqual(t.lastState().heading, 'LEAD');

  t.ctx.param('lead', 0);
  assert.strictEqual(t.lastActive('density'), 0);
  assert.deepStrictEqual(t.lastMsg('generate', 'text'), ['text', 'Generate']);
  assert.strictEqual(t.lastState().heading, 'PROGRESSION');
});

test('Lead over a source names the clip Lead · <name> · VAR and keeps the source chords', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(chordNotes(AM_F_C_G, 4)) }, slots: [0, 0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.param('lead', 1);
  assert.match(t.lastState().heading, /^LEAD/);
  t.ctx.generate();
  assert.match(t.live.slots[0].clip.name, /^Lead \u00B7 my riff \u00B7 VAR \u00B7 A min/);
  assert.deepStrictEqual(t.lastState().source.chords, ['Am', 'F', 'C', 'G']);
  assert.match(t.lastState().heading, /LEAD \u00B7 VARIATION/);
});
