// Pairing test for the jsui (no Max, no Live): runs the panel against a mock Live set and mock
// plug-in instances that answer /dp/ping the way RemoteLink does, with no MIDI delivered at all
// (what Live 11 did on Dan's machine). Checks the no-MIDI pairing (0.3.2) and that the MIDI path
// still works.
//   node scripts/test-pairing.js [path/to/dooblr-prophet-ui.js]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const file = process.argv[2] || path.join(__dirname, '..', 'src', 'dooblr-prophet-ui.js');
const source = fs.readFileSync(file, 'utf8');
let failures = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) ++failures; };

const panel = { cls: 'MxDeviceMidiEffect', name: 'dooblr-prophet' };
const synth = () => ({ cls: 'PluginDevice', name: 'dooblr-prophet' });

// set: { tracks: [[device...]], returns: [], master: [] }; a device may have chains: [[device...]]
function world(set, instances) {
  const w = { clock: 1000, outs: [], observers: 0, constructions: 0, set, instances, sendPort: 0, recvPort: 0 };
  const lists = { tracks: () => w.set.tracks, return_tracks: () => w.set.returns || [] };
  function resolve(p) {
    if (p === 'this_device') return { device: panel };
    if (p === 'this_device canonical_parent') return { devices: w.set.tracks[0] };
    if (p === 'live_set') return { set: true };
    let m = /^live_set (tracks|return_tracks) (\d+)(.*)$/.exec(p), node, rest;
    if (m) { node = { devices: lists[m[1]]()[Number(m[2])] }; rest = m[3]; }
    else if ((m = /^live_set master_track(.*)$/.exec(p))) { node = { devices: w.set.master || [] }; rest = m[1]; }
    else if ((m = /^this_device canonical_parent(.*)$/.exec(p))) { node = { devices: w.set.tracks[0] }; rest = m[1]; }
    else throw new Error('mock: unknown path ' + p);
    const re = / (devices|chains|return_chains) (\d+)/g; let s;
    while ((s = re.exec(rest))) {
      if (s[1] === 'devices') node = { device: node.devices[Number(s[2])] };
      else node = { devices: (node.device[s[1]] || [])[Number(s[2])] };
    }
    return node;
  }
  function LiveAPI(a, b) {
    w.constructions++;
    const cb = typeof a === 'function' ? a : null;
    const p = cb ? b : a;
    if (cb) w.observers++;
    const node = resolve(p);
    this.path = p;
    this.id = node.device ? (node.device === panel ? 100 : 200 + w.set.tracks[0].indexOf(node.device)) : 50;
    this.property = '';
    this.getcount = (what) => {
      if (node.set) return what === 'tracks' ? w.set.tracks.length : what === 'return_tracks' ? (w.set.returns || []).length : 0;
      if (what === 'devices') return (node.devices || []).length;
      return node.device && node.device[what] ? node.device[what].length : 0;
    };
    this.get = (what) => {
      const d = node.device;
      if (!d) return [''];
      if (what === 'name') return [d.name];
      if (what === 'class_name') return [d.cls];
      if (what === 'can_have_chains') return [d.chains ? 1 : 0];
      return [0];
    };
  }
  function Task(fn) { this.schedule = () => { w.pending = fn; }; this.repeat = () => {}; this.cancel = () => {}; }
  function FakeDate() { this.getTime = () => w.clock; }
  const g = new Proxy({}, { get: (o, k) => (k === 'size' ? [1000, 169] : k === 'text_measure' ? (s) => [String(s).length * 5, 10] : () => {}) });
  const ctx = {
    mgraphics: g, Math, Date: FakeDate, String, Number, parseInt, Object, Array, JSON, Error,
    post: () => {}, messagename: '', LiveAPI, Task, arrayfromargs: (args) => Array.prototype.slice.call(args),
    outlet: (n, ...a) => {
      const msg = a.length === 1 && Array.isArray(a[0]) ? a[0] : a;
      w.outs.push([n, msg]);
      if (n === 2 && msg[0] === 'port') w.recvPort = msg[1];
      else if (n === 0 && msg[0] === 'port') w.sendPort = msg[1];
      else if (n === 0) w.osc(w.sendPort, msg[0], msg.slice(1));
    },
  };
  // The mock plug-ins: answer /dp/ping on their port (as RemoteLink::handle does); /dp/hello pairs.
  w.inbox = [];
  w.osc = (to, address, args) => {
    const inst = w.instances.find((i) => i.port === to);
    if (!inst) return;
    if (address === '/dp/ping') w.inbox.push([args[0], '/dp/here', [inst.port, inst.version, inst.client ? 1 : 0].concat(inst.diag ? [5, 0, 0] : [])]);
    if (address === '/dp/hello') { inst.client = args[0]; inst.hellos = (inst.hellos || 0) + 1; }
  };
  w.deliver = () => {   // udpreceive: only packets to the port it is bound to arrive
    const box = w.inbox; w.inbox = [];
    for (const [to, address, args] of box) if (to === w.recvPort) { ctx.messagename = address; ctx.anything.apply(null, args); }
  };
  w.run = (ms) => { for (let t = 0; t < ms; t += 33) { w.clock += 33; ctx.tick(); w.deliver(); if (w.pending) { const f = w.pending; w.pending = null; f(); } } };
  vm.createContext(ctx);
  vm.runInContext(source, ctx, { filename: file });
  w.ctx = ctx;
  return w;
}

// 1. One instance, after the panel, no MIDI: pairs by ping.
{
  const inst = { port: 47812, version: '0.3.0' };
  const w = world({ tracks: [[panel, synth()], [{ cls: 'PluginDevice', name: 'Other synth' }]] }, [inst]);
  w.ctx.liveready();
  w.run(1500);
  const pings = w.outs.filter(([n, m]) => n === 0 && m[0] === '/dp/ping').length;
  check(pings >= 64, 'pings every link port (' + pings + ')');
  check(w.ctx.S.state === 'syncing' || w.ctx.S.state === 'linked', 'pairs with no MIDI (state ' + w.ctx.S.state + ')');
  check(w.sendPort === 47812 && inst.client === w.recvPort, 'udpsend points at the instance and hello carries the reply port');
  check(w.observers === 1, 'the set count adds no observers (' + w.observers + ')');
  const ccs = w.outs.filter(([n]) => n === 1).map(([, m]) => m);
  const first = ccs[0] || [];
  const ok = first.length === 18 && [102, 103, 104, 16, 17, 18].every((cc, i) => first[i * 3] === 0xBF && first[i * 3 + 1] === cc)
    && first[2] === first[11] && first[5] === first[14] && first[8] === first[17] && first[8] === ((first[2] ^ first[5] ^ 0x2a) & 127);
  check(ok, 'pairing MIDI: both CC sets on channel 16, same token and check');
}

// 2. Two instances in the set (another track): no guessing.
{
  const a = { port: 47810, version: '0.3.1', diag: true }, b = { port: 47811, version: '0.3.1', diag: true };
  const w = world({ tracks: [[panel, synth()], [synth()]] }, [a, b]);
  w.ctx.liveready(); w.run(3000);
  check(w.ctx.S.state === 'searching' && !a.client && !b.client, 'two instances: no pairing without MIDI');
  check(w.ctx.S.why === 'several', 'reported as several (' + w.ctx.S.why + ')');
  // ... but the MIDI path still works: the instance that saw the token announces.
  w.inbox.push([w.recvPort, '/dp/announce', [47811, '0.3.1', w.ctx.S.token]]);
  w.deliver();
  check(w.ctx.S.state === 'syncing' && w.sendPort === 47811, 'an announce with the token pairs');
}

// 3. One instance in Live, but a second answering (another host): no guessing.
{
  const w = world({ tracks: [[panel, synth()]] }, [{ port: 47810, version: '0.3.1' }, { port: 47811, version: '0.3.1' }]);
  w.ctx.liveready(); w.run(3000);
  check(w.ctx.S.state === 'searching', 'two replies, one in the set: waits');
}

// 4. The one instance inside a rack chain on another track counts.
{
  const w = world({ tracks: [[panel, synth()], [{ cls: 'InstrumentGroupDevice', name: 'Rack', chains: [[synth()]] }]] }, [{ port: 47810, version: '0.3.1' }]);
  w.ctx.liveready(); w.run(3000);
  check(w.ctx.S.state === 'searching' && w.ctx.S.why === 'several', 'a second instance inside a rack is counted');
}

// 5. Already paired to another panel: waits; plug-in before the panel: no pairing.
{
  const w = world({ tracks: [[panel, synth()]] }, [{ port: 47810, version: '0.3.1', client: 50000 }]);
  w.ctx.liveready(); w.run(3000);
  check(w.ctx.S.state === 'searching' && w.ctx.S.why === 'taken', 'a taken instance is left alone');
  const w2 = world({ tracks: [[synth(), panel]] }, [{ port: 47810, version: '0.3.1' }]);
  w2.ctx.liveready(); w2.run(3000);
  check(w2.ctx.S.state === 'searching' && w2.ctx.S.deviceOnTrack === -1, 'plug-in before the panel: no pairing');
}

// 6. A stray /dp/here or an announce with a stale token never pairs.
{
  const w = world({ tracks: [[panel, synth()], [synth()]] }, []);
  w.ctx.liveready(); w.run(1100);
  w.inbox.push([w.recvPort, '/dp/announce', [47810, '0.3.1', w.ctx.S.token + 1]]); w.deliver();
  check(w.ctx.S.state === 'searching', 'a wrong token is ignored');
  // 7. Losing the link: back to pairing.
  w.instances.push({ port: 47810, version: '0.3.1' });
  w.inbox.push([w.recvPort, '/dp/announce', [47810, '0.3.1', w.ctx.S.token]]); w.deliver();
  check(w.ctx.S.state === 'syncing', 'paired by announce');
  w.ctx.messagename = '/dp/gone'; w.ctx.anything();
  check(w.ctx.S.state === 'searching' && w.ctx.S.serverPort === 0, '/dp/gone: searching again');
}

console.log(failures ? failures + ' check(s) failed' : 'all pairing checks passed');
process.exit(failures ? 1 : 0);
