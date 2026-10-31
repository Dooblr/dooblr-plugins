// Mock Live API test for the jsui (no Max needed). The mock LiveAPI behaves at its worst:
// an observing LiveAPI calls its callback synchronously on construction ("id N") and again
// synchronously when .property is set, and get() can fire the watcher mid-query. It checks that
// nothing touches the Live API before live.thisdevice's "liveready", that the device watcher
// is created exactly once, that callbacks never recurse into findPlugin(), and that bursts of
// notifications coalesce into one deferred look.
//   node scripts/test-liveapi.js [path/to/dooblr-prophet-ui.js]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const file = process.argv[2] || path.join(__dirname, '..', 'src', 'dooblr-prophet-ui.js');
const source = fs.readFileSync(file, 'utf8');
const DEPTH_LIMIT = 40; // a real recursion would go to hundreds of levels; stop it early

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) ++failures; };

function makeWorld() {
  const w = {
    devices: [{ cls: 'MxDeviceMidiEffect', name: 'dooblr-prophet' }, { cls: 'PluginDevice', name: 'dooblr-prophet' }],
    constructions: 0, watchers: 0, depth: 0, maxDepth: 0, overflow: false,
    callbacks: [], tasks: [], scheduled: 0, fireDuringGet: false,
  };
  const fireAll = (args) => { for (const cb of w.callbacks.slice()) cb.fn.apply(cb.api, args); };
  function LiveAPI(a, b) {
    const callback = typeof a === 'function' ? a : null;
    const p = callback ? b : a;
    w.constructions++;
    w.depth++;
    w.maxDepth = Math.max(w.maxDepth, w.depth);
    try {
      if (w.depth > DEPTH_LIMIT) { w.overflow = true; throw new Error('mock: LiveAPI recursion past ' + DEPTH_LIMIT); }
      if (w.constructions > 20000) { w.overflow = true; throw new Error('mock: runaway LiveAPI calls'); }
      this.path = p;
      const m = /devices (\d+)$/.exec(p || '');
      this.id = p === 'this_device' ? 100 : (m ? 100 + Number(m[1]) : 50);
      let prop = '';
      Object.defineProperty(this, 'property', {
        get: () => prop,
        set: (v) => { prop = v; if (callback && v) callback.call(this, v, 'id', 100, 'id', 101); },
      });
      this.getcount = () => w.devices.length;
      this.get = (what) => {
        if (w.fireDuringGet) fireAll(['devices', 'id', 100]);
        const d = m ? w.devices[Number(m[1])] : null;
        return [d ? (what === 'name' ? d.name : d.cls) : ''];
      };
      if (callback) {
        w.watchers++;
        w.callbacks.push({ fn: callback, api: this });
        callback.call(this, 'id', this.id);   // synchronous, during construction
      }
    } finally {
      w.depth--;
    }
  }
  function Task(fn) {
    const t = { fn, pending: false, interval: 0 };
    this.schedule = () => { t.pending = true; w.scheduled++; };
    this.repeat = () => {};
    this.cancel = () => { t.pending = false; };
    w.tasks.push(t);
  }
  const g = new Proxy({}, { get: (o, k) => (k === 'size' ? [1000, 169] : k === 'text_measure' ? (s) => [String(s).length * 5, 10] : () => {}) });
  const outs = [];
  const ctx = {
    mgraphics: g, Math, Date, String, Number, parseInt, Object, Array, JSON, Error,
    outlet: (...a) => outs.push(a), post: () => {}, messagename: '',
    LiveAPI, Task, arrayfromargs: (args) => Array.prototype.slice.call(args),
  };
  vm.createContext(ctx);
  vm.runInContext(source, ctx, { filename: file });
  w.ctx = ctx;
  w.fire = fireAll;
  w.runTasks = () => { for (const t of w.tasks) if (t.pending) { t.pending = false; t.fn(); } };
  return w;
}

const start = (w) => (typeof w.ctx.liveready === 'function' ? w.ctx.liveready() : w.ctx.bang());

// 1. Load, loadbang, a bare bang, paint and ticks before Live is ready: no Live API at all.
{
  const w = makeWorld();
  check(w.constructions === 0, 'loading the script makes no LiveAPI calls');
  w.ctx.loadbang();
  if (typeof w.ctx.liveready === 'function') w.ctx.bang();
  w.ctx.paint();
  if (w.ctx.tick) w.ctx.tick();
  w.fire(['devices', 'id', 1]);
  w.runTasks();
  check(w.constructions === 0, 'loadbang, bang, paint and tick before liveready make no LiveAPI calls');
}

// 2. liveready with synchronous callbacks on construction and on .property.
{
  const w = makeWorld();
  let threw = null;
  try { start(w); } catch (e) { threw = e; }
  check(threw === null, 'liveready returns normally' + (threw ? ' (' + threw.message + ')' : ''));
  check(!w.overflow && w.maxDepth === 1, 'no nested LiveAPI construction (max depth ' + w.maxDepth + ')');
  check(w.watchers === 1, 'exactly one device watcher (' + w.watchers + ')');
  check(w.ctx.S.deviceOnTrack === 1, 'finds the plug-in after this device');

  start(w); start(w);
  check(w.watchers === 1, 'a repeated liveready does not add watchers');

  // 3. A burst of notifications coalesces into one deferred look; nothing runs synchronously.
  w.runTasks();
  const before = w.constructions, sched = w.scheduled;
  for (let i = 0; i < 20; ++i) w.fire(['devices', 'id', 100, 'id', 101]);
  check(w.constructions === before, 'watcher callbacks never query the Live API synchronously');
  check(w.scheduled - sched === 1, '20 notifications schedule one deferred look (' + (w.scheduled - sched) + ')');
  w.runTasks();
  check(w.constructions - before === 2 + w.devices.length, 'the deferred look runs once');
  w.fire(['id', 7]);
  check(w.scheduled - sched === 1, 'an "id" notification is ignored');

  // 4. Live notifying in the middle of a query cannot re-enter findPlugin.
  w.fireDuringGet = true;
  w.maxDepth = 0;
  w.fire(['devices']); w.runTasks();
  check(!w.overflow && w.maxDepth === 1, 'a notification during a query does not recurse');
  w.fireDuringGet = false;
  w.runTasks();

  // 5. Device changes are followed.
  w.devices = [w.devices[0]];
  w.fire(['devices', 'id', 100]); w.runTasks();
  check(w.ctx.S.deviceOnTrack === 0 && w.ctx.S.state === 'missing', 'plug-in removed: not found');
  w.devices = [{ cls: 'PluginDevice', name: 'dooblr-prophet' }, { cls: 'MxDeviceMidiEffect', name: 'x' }];
  w.fire(['devices', 'id', 100]); w.runTasks();
  check(w.ctx.S.deviceOnTrack === -1, 'plug-in before this device: reported as such');

  // 6. After deletion nothing is scheduled.
  w.ctx.notifydeleted();
  const s2 = w.scheduled;
  w.fire(['devices', 'id', 100]);
  check(w.scheduled === s2, 'no deferred look after the device is deleted');
}

console.log(failures ? failures + ' check(s) failed' : 'all Live API checks passed');
process.exit(failures ? 1 : 0);
