// Runs the BUILT device scripts (device/dooblr-titan/*.js) in a VM with a
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
const { buildSmf, n } = require('./smf');

const ROOT = path.join(__dirname, '..');
const DEVICE = path.join(ROOT, 'device', 'dooblr-titan');
execFileSync(process.execPath, [path.join(ROOT, 'scripts', 'build.js')], { stdio: 'ignore' });

function makeLive(ctx, opts) {
  const VmArray = vm.runInContext('Array', ctx);
  const arr = (xs) => { const a = new VmArray(); xs.forEach((x) => a.push(x)); return a; };
  const live = {
    devicePath: opts.devicePath || 'live_set tracks 0 devices 0',
    slots: (opts.slots || [1, 0, 0]).map((h) => ({ clip: h ? { name: 'existing', notes: [{}], length: 4 } : null })),
    addNewNotesWorks: opts.addNewNotesWorks !== false,
    duplicateWorks: opts.duplicateWorks === true,
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
    if (prop === 'length' && this._slot && this._slot.clip) { return arr([this._slot.clip.length]); }
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
      if (name === 'get_notes_extended') {
        const from = args[3], len = args[4];
        return JSON.stringify({ notes: this._detail.notes.filter((n) => n.start_time >= from && n.start_time < from + len) });
      }
      if (name === 'duplicate_clip_to') {
        live.calls.push(['duplicate']);
        if (live.duplicateWorks) {
          const empty = live.slots.find((s) => !s.clip);
          if (empty) { empty.clip = { name: this._detail.name || 'dup', notes: JSON.parse(JSON.stringify(this._detail.notes || [])), length: this._detail.length || 4, pending: null }; }
        }
        return 0;
      }
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
  const out = { outlets: [], posts: [], named: {}, writes: {} };
  ctx.post = (s) => out.posts.push(String(s));
  ctx.outlet = function () { out.outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  ctx.patcher = { getnamed: (n) => {
    if (!out.named[n]) { out.named[n] = { msgs: [], message: function () { this.msgs.push(Array.prototype.slice.call(arguments)); } }; }
    return out.named[n];
  } };
  const mock = makeLive(ctx, opts);
  ctx.LiveAPI = mock.LiveAPI;
  const files = opts.files || {};
  const VmArray = vm.runInContext('Array', ctx);
  ctx.File = function (p, mode) {
    this._path = p;
    this._data = files[p];
    this.isopen = mode === 'read' ? !!this._data : true;
    this.eof = this._data ? this._data.length : 0;
    this.position = 0;
    this._written = [];
    out.fileOpens = (out.fileOpens || 0) + 1;
  };
  ctx.File.prototype.readbytes = function (n) {
    assert.ok(n > 0 && n <= 1024);
    const a = new VmArray();
    this._data.slice(this.position, this.position + n).forEach((b) => a.push(b > 127 ? b - 256 : b));
    this.position += a.length;
    return a;
  };
  ctx.File.prototype.writebytes = function (chunk) {
    for (let i = 0; i < chunk.length; i++) { this._written.push(chunk[i] & 0xff); }
  };
  ctx.File.prototype.close = function () {
    this.closed = true;
    out.fileCloses = (out.fileCloses || 0) + 1;
    if (this._written.length) { out.writes[this._path] = this._written.slice(); }
  };
  vm.runInContext(fs.readFileSync(path.join(DEVICE, 'dooblr_titan.js'), 'utf8'), ctx, { filename: 'dooblr_titan.js' });
  const lastRoll = () => {
    const s = out.outlets.filter((o) => o[0] === 0 && o[1] === 'setstate');
    return s.length ? JSON.parse(s[s.length - 1][2]) : null;
  };
  const lastPreviews = () => {
    const s = out.outlets.filter((o) => o[0] === 2 && o[1] === 'setstate');
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
  return { ctx, out, live: mock.live, lastRoll, lastPreviews, lastActive, lastMsg };
}

const RIFF = 'Macintosh HD:/Users/dan/MIDI/my riff.mid';
const MELODY = [n(60, 0, 0.5, 100), n(62, 0.5, 0.5, 80), n(64, 1, 0.5, 90), n(65, 1.5, 0.5, 70), n(67, 2, 1, 110), n(64, 3, 0.9, 85)];

test('no Live API use before live.thisdevice, then Tighten writes into the first empty slot', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, slots: [1, 0, 0] });
  t.ctx.loadbang();
  t.ctx.dropfile(RIFF);
  t.ctx.tighten();
  assert.strictEqual(t.live.calls.length, 0, 'no LiveAPI calls before init');
  assert.match(t.lastRoll().status, /still loading/);
  assert.strictEqual(t.lastActive('tighten'), 0);

  t.ctx.init();
  assert.strictEqual(t.lastActive('tighten'), 1);
  t.ctx.tighten();
  const clip = t.live.slots[1].clip;
  assert.ok(clip, 'clip created in slot index 1');
  assert.match(clip.name, /TIGHT/);
  assert.ok(clip.notes.length >= 1);
  clip.notes.forEach((n) => {
    assert.ok(n.pitch >= 0 && n.pitch <= 127);
    assert.ok(n.start_time >= 0);
  });
  assert.match(t.lastRoll().status, /slot 2/);
  assert.strictEqual(t.lastRoll().level, 'ok');
  assert.ok(t.lastRoll().result.length >= 1);
});

test('fills slots in order and reports a full track without creating scenes', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, slots: [0, 0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.tighten();
  t.ctx.write();
  assert.ok(t.live.slots[0].clip && t.live.slots[1].clip);
  const before = t.live.calls.length;
  t.ctx.tighten();
  assert.match(t.lastRoll().status, /Every clip slot on this track is full/);
  assert.strictEqual(t.lastRoll().level, 'warn');
  assert.ok(!t.live.calls.slice(before).some((c) => c[1] === 'create_clip' || c[1] === 'create_scene'));
});

test('falls back to set_notes when add_new_notes adds nothing', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, addNewNotesWorks: false, slots: [0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.tighten();
  assert.ok(t.live.calls.some((c) => c[1] === 'set_notes'));
  assert.ok(t.live.slots[0].clip.notes.length >= 1);
  assert.strictEqual(t.lastRoll().level, 'ok');
});

test('finds the track from inside a rack', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, devicePath: 'live_set tracks 0 devices 1 chains 0 devices 0', slots: [0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.tighten();
  assert.ok(t.live.slots[0].clip);
});

test('parameters update without writing, unknown param is posted', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) } });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.param('qstrength', 0);
  t.ctx.param('amount', 80);
  assert.strictEqual(t.live.calls.length, 0);
  t.ctx.param('bogus', 1);
  assert.ok(t.out.posts.some((p) => /unknown parameter bogus/.test(p)));
});

test('Tighten, Generate, Write and SAVE MIDI are dimmed without a source', () => {
  const t = loadController();
  t.ctx.init();
  assert.strictEqual(t.lastActive('tighten'), 0);
  assert.strictEqual(t.lastActive('generate'), 0);
  assert.strictEqual(t.lastActive('write'), 0);
  assert.strictEqual(t.lastActive('savemidi'), 0);
  assert.strictEqual(t.lastActive('useclip'), 1);
  t.ctx.tighten();
  assert.match(t.lastRoll().status, /Load a clip first/);
});

test('a dropped .mid file becomes the source; Generate lists four variations; Write saves the chosen one', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, slots: [0, 0, 0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  assert.strictEqual(t.out.fileOpens, t.out.fileCloses);
  assert.strictEqual(t.lastRoll().level, 'ok', t.lastRoll().status);
  assert.match(t.lastRoll().status, /Loaded my riff/);
  assert.ok(t.lastRoll().source.length >= 6);
  assert.deepStrictEqual(t.lastMsg('root', 'set')[1], t.lastMsg('root', 'set')[1]);
  assert.ok(t.lastMsg('srcinfo', 'set')[1].indexOf('my riff') >= 0);
  assert.strictEqual(t.live.calls.length, 0, 'loading a source writes nothing');

  t.ctx.generate();
  const pv = t.lastPreviews();
  assert.strictEqual(pv.items.length, 4);
  assert.deepStrictEqual(pv.items.map((x) => x.label), ['Displace', 'Substitute', 'Fill', 'Invert']);
  t.ctx.pick(2);
  t.ctx.write();
  const clip = t.live.slots[0].clip;
  assert.ok(clip);
  assert.match(clip.name, /VAR Fill/);
  assert.ok(clip.notes.length >= 1);
});

test('Another changes the seed and rebuilds; same seed is deterministic', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, slots: [0, 0, 0, 0] });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.param('seed', 11);
  t.ctx.generate();
  t.ctx.write();
  const first = JSON.stringify(t.live.slots[0].clip.notes);
  t.ctx.another();
  const seedSet = t.out.outlets.filter((o) => o[0] === 1 && o[1] === 'set');
  assert.ok(seedSet.length);
  assert.notStrictEqual(seedSet[seedSet.length - 1][2], 11);
  t.ctx.write();
  const second = JSON.stringify(t.live.slots[1].clip.notes);
  assert.notStrictEqual(second, first);
});

test('dropped files that are not usable MIDI report a warning and keep the old source', () => {
  const bad = buildSmf(MELODY).slice(0, 40);
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY), '/x/bad.mid': bad, '/x/empty.mid': buildSmf([]) } });
  t.ctx.init();
  t.ctx.dropfile('/x/loop.wav');
  assert.match(t.lastRoll().status, /Drop a MIDI file .* loop.wav is not one/);
  t.ctx.dropfile('/x/missing.mid');
  assert.match(t.lastRoll().status, /Could not read missing.mid/);
  t.ctx.dropfile(RIFF);
  t.ctx.dropfile('/x/bad.mid');
  assert.strictEqual(t.lastRoll().level, 'warn');
  assert.match(t.lastRoll().status, /^bad.mid: .*MIDI file/);
  t.ctx.dropfile('/x/empty.mid');
  assert.match(t.lastRoll().status, /^empty.mid: The clip has no notes/);
  assert.ok(t.lastRoll().source.length >= 6, 'old source kept');
  t.ctx.dropfile();
  assert.ok(t.lastRoll().source.length >= 6, 'empty drop ignored');
});

test('a path re-sent while the Set loads keeps the saved Root and Mode', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) } });
  t.ctx.loadbang();
  t.ctx.param('root', 2);
  t.ctx.param('mode', 2);
  t.ctx.dropfile(RIFF);
  assert.strictEqual(t.lastMsg('root', 'set'), undefined);
  assert.ok(t.lastRoll().source.length >= 1);
  assert.strictEqual(t.live.calls.length, 0);
});

function clipNotes(notes, offset) {
  return notes.map((x) => ({ pitch: x.pitch, start_time: x.start + (offset || 0), duration: x.duration, velocity: x.velocity, mute: 0 }));
}

test('USE CLIP reads the Detail View clip loop, shifted to start at 0', () => {
  const notes = clipNotes(MELODY, 8);
  const t = loadController({ detailClip: { name: 'Chorus', length: 16, looping: true, loopStart: 8, loopEnd: 12, notes } });
  t.ctx.loadbang();
  t.ctx.useclip();
  assert.match(t.lastRoll().status, /still loading/);
  t.ctx.init();
  t.ctx.useclip();
  const st = t.lastRoll();
  assert.strictEqual(st.level, 'ok', st.status);
  assert.ok(st.source.length >= 1);
  assert.ok(t.live.calls.some((c) => c[0] === 'path' && c[1] === 'live_set view detail_clip'));
  assert.ok(t.lastMsg('srcdrop', 'clear'), 'a clip source clears the saved file path');
});

test('USE CLIP falls back to the highlighted slot and explains audio or missing clips', () => {
  let t = loadController({ highlightedClip: { name: 'Pad', length: 8, notes: clipNotes(MELODY.slice(0, 3)) } });
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastRoll().status, /Loaded Pad/);

  t = loadController({});
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastRoll().status, /No clip is selected/);

  t = loadController({ detailClip: { audio: true, length: 8, notes: [] } });
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastRoll().status, /audio clip/);

  t = loadController({ detailClip: { name: 'Empty', length: 8, notes: [] } });
  t.ctx.init();
  t.ctx.useclip();
  assert.match(t.lastRoll().status, /Selected clip: The clip has no notes/);
});

test('USE CLIP then Tighten duplicates the source clip when Live supports it', () => {
  const t = loadController({
    detailClip: { name: 'Take', length: 4, notes: clipNotes(MELODY) },
    duplicateWorks: true,
    slots: [0, 0]
  });
  t.ctx.init();
  t.ctx.useclip();
  t.ctx.tighten();
  assert.ok(t.live.calls.some((c) => c[0] === 'duplicate' || c[1] === 'duplicate_clip_to'));
  assert.ok(t.live.slots[0].clip);
  assert.match(t.live.slots[0].clip.name, /TIGHT/);
  assert.ok(t.live.slots[0].clip.notes.length >= 1);
});

test('SAVE MIDI writes a Standard MIDI File of the last result', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) }, slots: [0] });
  t.ctx.init();
  t.ctx.savemidi();
  assert.ok(!t.out.outlets.some((o) => o[0] === 3), 'no dialog without a result');
  t.ctx.dropfile(RIFF);
  t.ctx.tighten();
  t.ctx.savemidi();
  assert.ok(t.out.outlets.some((o) => o[0] === 3 && o[1] === 'bang'));
  t.ctx.savepath('/tmp/out');
  const bytes = t.out.writes['/tmp/out.mid'];
  assert.ok(bytes && bytes.length > 20);
  assert.strictEqual(String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]), 'MThd');
  assert.match(t.lastRoll().status, /Saved out/);
});

test('CLEAR forgets the source and dims Write', () => {
  const t = loadController({ files: { [RIFF]: buildSmf(MELODY) } });
  t.ctx.init();
  t.ctx.dropfile(RIFF);
  t.ctx.clearsource();
  assert.strictEqual(t.lastRoll().source.length, 0);
  assert.ok(t.lastMsg('srcdrop', 'clear'));
  assert.strictEqual(t.lastActive('tighten'), 0);
});

function loadJsui(file, rect) {
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
  ctx.box = { rect: rect, message: () => {} };
  ctx.post = () => {};
  ctx.outlet = function () { outlets.push(Array.prototype.slice.call(arguments)); };
  ctx.setinletassist = ctx.setoutletassist = () => {};
  vm.runInContext(fs.readFileSync(path.join(DEVICE, file), 'utf8'), ctx, { filename: file });
  return { ctx, ops, outlets };
}

test('piano roll paints notes and never draws hint copy', () => {
  const b = loadJsui('dooblr_roll.js', [716, 116, 852, 244]);
  b.ctx.setstate(JSON.stringify({
    heading: 'riff', source: [{ pitch: 60, start: 0, duration: 1, velocity: 100 }],
    result: [{ pitch: 64, start: 1, duration: 1, velocity: 90 }], lengthBeats: 4, status: 'Wrote x', level: 'ok'
  }));
  b.ctx.paint();
  const texts = b.ops.filter((o) => o[0] === 'show_text').map((o) => o[1]);
  assert.ok(texts.indexOf('RIFF') >= 0);
  assert.ok(!texts.some((s) => /drop|click|drag|hint|empty/i.test(s)), 'graph hint: ' + texts.join('|'));
  assert.ok(b.ops.some((o) => o[0] === 'rectangle' && o[3] > 2));
});

test('preview list paints four rows and maps clicks to pick', () => {
  const b = loadJsui('dooblr_previews.js', [516, 144, 704, 244]);
  b.ctx.setstate(JSON.stringify({
    items: [
      { label: 'Displace', notes: 6, selected: true },
      { label: 'Substitute', notes: 6, selected: false },
      { label: 'Fill', notes: 8, selected: false },
      { label: 'Invert', notes: 5, selected: false }
    ], selected: 0
  }));
  b.ctx.paint();
  const texts = b.ops.filter((o) => o[0] === 'show_text').map((o) => o[1]);
  assert.ok(texts.indexOf('VARIATIONS') >= 0);
  assert.ok(texts.indexOf('Displace') >= 0);
  assert.ok(texts.indexOf('Fill') >= 0);
  b.ctx.onclick(10, 50);
  assert.ok(b.outlets.length >= 1);
  assert.strictEqual(b.outlets[0][1], 'pick');
});
