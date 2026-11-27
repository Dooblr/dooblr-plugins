// dooblr-chord-receiver: Max `js` glue (classic js object, ES5 only).
// Incoming MIDI notes are snapped to the harmony advertised on the global
// bus `dooblr_chords_bus` by a dooblr-chords sender on the same Channel.
//
// Messages in:
//   init                 from live.thisdevice
//   param <name> <value>
//   note <pitch> <vel>   from midiparse (vel 0 = off)
//   bus <json>           from receive dooblr_chords_bus
//   notifydeleted / closebang
// Outlets:
//   0  raw MIDI bytes    to midiout
//   1  bus JSON          to send dooblr_chords_bus (hello)
//   2  setstate <json>   to the status jsui

autowatch = 0;
inlets = 1;
outlets = 3;
setinletassist(0, 'init, param, note, bus');
setoutletassist(0, 'raw MIDI bytes to midiout');
setoutletassist(1, 'hello JSON to send dooblr_chords_bus');
setoutletassist(2, 'setstate <json> to the status display');

var self = this;
var P = DC.protocol;
var Snap = DC.snap;

var PARAM_NAMES = ['channel', 'role', 'snapdir', 'retrigger', 'refroot', 'range', 'register'];
var settings = { channel: 0, role: 0, snapdir: 0, retrigger: 1, refroot: 0, range: 0, register: 48 };
var mapper = Snap.create();
var ready = false;
var lastHeard = 0;
var connected = false;
var clockTask = null;
var deviceObs = null;
var STALE_MS = 1000;
var MIDI_OUT = 0;
var BUS_OUT = 1;
var UI_OUT = 2;

function loadbang() { sendState(); }

function init() {
  ready = true;
  startClock();
  bindDevice();
  sayHello();
  sendState();
}

function notifydeleted() { teardown(); }
function closebang() { teardown(); }

function param(name, value) {
  var i, found = false;
  for (i = 0; i < PARAM_NAMES.length; i++) { if (PARAM_NAMES[i] === name) { found = true; } }
  if (!found) { post('dooblr-chord-receiver: unknown parameter ' + name + '\n'); return; }
  var before = settings[name];
  if (name === 'register') { value = clamp(Math.round(+value || 0), 24, 84); }
  else if (name === 'channel' || name === 'role' || name === 'snapdir' || name === 'refroot') {
    value = Math.round(+value || 0);
  } else {
    value = value ? 1 : 0;
  }
  if (name === 'channel') { value = P.clampChan(value); }
  if (name === 'role') { value = clamp(value, 0, 3); }
  if (name === 'snapdir') { value = clamp(value, 0, 2); }
  if (name === 'refroot') { value = ((value % 12) + 12) % 12; }
  settings[name] = value;
  if (name === 'channel' && value !== before) {
    emit(Snap.flush(mapper));
    mapper.harmony = null;
    connected = false;
    sayHello();
  } else if (settings.retrigger && mapper.harmony && name !== 'retrigger') {
    emit(Snap.retrigger(mapper, mapOpts()));
  }
  sendState();
}

function note(pitch, vel) {
  expireIfStale();
  emit(vel ? Snap.noteOn(mapper, pitch, vel, mapOpts()) : Snap.noteOff(mapper, pitch));
}

function bus() {
  var raw = Array.prototype.slice.call(arguments).join(' ');
  var msg = P.decode(raw);
  if (!msg) { return; }
  if (msg.type === 'hello' || msg.type === 'bye') { return; }
  if (msg.type !== 'harmony') { return; }
  if (P.clampChan(msg.ch) !== settings.channel) { return; }
  var prev = mapper.harmony;
  var changed = !prev || P.identity(prev) !== P.identity(msg);
  mapper.harmony = msg;
  lastHeard = now();
  connected = true;
  if (changed && settings.retrigger) { emit(Snap.retrigger(mapper, mapOpts())); }
  sendState();
}

function tick() {
  expireIfStale();
}

function expireIfStale() {
  if (!connected) { return; }
  var t = now();
  if (t && lastHeard && (t - lastHeard) > STALE_MS) {
    connected = false;
    mapper.harmony = null;
    emit(Snap.flush(mapper));
    sendState();
  }
}

function sayHello() {
  outlet(BUS_OUT, P.encode({ type: 'hello', ch: settings.channel }));
}

function mapOpts() {
  return {
    role: settings.role,
    snapdir: settings.snapdir,
    refroot: settings.refroot,
    range: settings.range,
    register: settings.register
  };
}

function emit(evs) {
  var i, ev;
  if (!evs || !evs.length) { return; }
  for (i = 0; i < evs.length; i++) {
    ev = evs[i];
    if (ev.type === 'off') { outlet(MIDI_OUT, 128, ev.pitch, 0); }
    else { outlet(MIDI_OUT, 144, ev.pitch, ev.velocity); }
  }
}

function sendState() {
  var h = mapper.harmony;
  var state = {
    connected: connected && !!h,
    name: (connected && h && h.name) ? h.name : 'NO SENDER',
    channel: settings.channel,
    role: settings.role
  };
  outlet(UI_OUT, 'setstate', JSON.stringify(state));
}

function now() { return (typeof Date !== 'undefined' && Date.now) ? Date.now() : 0; }
function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

function startClock() {
  if (clockTask || typeof Task === 'undefined') { return; }
  try {
    clockTask = new Task(tick, self);
    clockTask.interval = 100;
    clockTask['repeat']();
  } catch (e) { clockTask = null; }
}

function stopClock() {
  if (!clockTask) { return; }
  try { clockTask.cancel(); } catch (e) {}
  clockTask = null;
}

function teardown() {
  emit(Snap.flush(mapper));
  stopClock();
  deviceObs = null;
  connected = false;
  mapper.harmony = null;
}

function bindDevice() {
  if (typeof LiveAPI === 'undefined') { return; }
  try {
    deviceObs = new LiveAPI(onDevice, 'live_set this_device');
    deviceObs.property = 'is_active';
  } catch (e) { deviceObs = null; }
  try {
    var song = new LiveAPI(onTransport, 'live_set');
    song.property = 'is_playing';
    // keep a second observer; if it fails, tick + note path still flushes
    self._songObs = song;
  } catch (e2) {}
}

function onDevice(args) {
  try {
    if (String(args[0]) === 'is_active' && Number(args[1]) === 0) {
      emit(Snap.flush(mapper));
    }
  } catch (e) {}
}

function onTransport(args) {
  try {
    if (String(args[0]) === 'is_playing' && Number(args[1]) === 0) {
      emit(Snap.flush(mapper));
    }
  } catch (e) {}
}
