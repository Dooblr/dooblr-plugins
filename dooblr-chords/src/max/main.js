// dooblr-chords: Max `js` glue (classic js object, ES5 only).
// The build script prepends the engine (src/engine/*.js) to this file and
// writes the result to device/dooblr-chords/dooblr_chords.js.
//
// Messages in:
//   init                 from live.thisdevice; LiveAPI is safe after this
//   param <name> <value> from each live.* control (via [prepend param <name>])
//   generate             new seed, clear locks, write a clip
//   reroll               new seed, keep locked chords, write a clip
//   write                write the current preview as a clip
//   lock <index>         toggle the lock on one chord (from the jsui)
//   unlockall            clear every lock
//   ascii <0|1>          use b/# instead of the flat/sharp glyphs on screen
// Outlets:
//   0  setstate <json>   to the chord display (jsui)
//   1  set <seed>        to the Seed number box (no output, no feedback loop)

autowatch = 0;
inlets = 1;
outlets = 2;
setinletassist(0, 'init, param <name> <value>, generate, reroll, write, lock <index>');
setoutletassist(0, 'setstate <json> to the chord display');
setoutletassist(1, 'set <seed> to the Seed number box');

var self = this;
var E = DC.engine;

var PARAM_NAMES = ['root', 'mode', 'length', 'duration', 'color', 'chromatic',
  'register', 'spread', 'bass', 'velocity', 'humanize', 'seed'];

var settings = E.normalize(E.DEFAULTS);
var locks = [];
var current = null;
var ready = false;
var statusText = 'Press Generate to write a progression into the next empty clip slot.';
var statusLevel = 'info';

// Button varnames in the patcher, for dimming with an explanation.
var BUTTON_HINTS = {
  generate: 'Generate makes a new progression with a new seed and writes it as a clip in the next empty slot on this track.',
  reroll: 'Reroll keeps the locked chords, regenerates the others with a new seed and writes a clip.',
  write: 'Write puts the progression shown below into the next empty clip slot without changing it.'
};

// ---- messages -------------------------------------------------------------

function loadbang() {
  refresh();
}

function init() {
  ready = true;
  refresh();
}

function param(name, value) {
  if (!isParam(name)) {
    post('dooblr-chords: unknown parameter ' + name + '\n');
    return;
  }
  settings[name] = value;
  settings = E.normalize(settings);
  if (name === 'length') { trimLocks(); }
  refresh();
}

function generate() {
  if (!guard()) { return; }
  locks = [];
  pickNewSeed();
  refresh();
  writeClip();
}

function reroll() {
  if (!guard()) { return; }
  if (lockCount() === 0) {
    setStatus('info', 'Lock a chord first: click a chord block, then Reroll keeps it and changes the rest.');
    refresh();
    return;
  }
  pickNewSeed();
  refresh();
  writeClip();
}

function write() {
  if (!guard()) { return; }
  if (!current) { refresh(); }
  writeClip();
}

function lock(index) {
  var i = Math.floor(index);
  if (!current || i < 0 || i >= current.chords.length) { return; }
  locks[i] = locks[i] ? null : DC.chords.copy(current.chords[i]);
  refresh();
}

function unlockall() {
  locks = [];
  refresh();
}

function ascii(on) {
  DC.theory.setAsciiAccidentals(on ? true : false);
  refresh();
}

function anything() {
  post('dooblr-chords: unknown message ' + messagename + '\n');
}

// ---- state ------------------------------------------------------------------

function isParam(name) {
  for (var i = 0; i < PARAM_NAMES.length; i++) { if (PARAM_NAMES[i] === name) { return true; } }
  return false;
}

function trimLocks() {
  var out = [];
  for (var i = 0; i < settings.length; i++) { out.push(locks[i] || null); }
  locks = out;
}

function lockCount() {
  var n = 0;
  for (var i = 0; i < locks.length; i++) { if (locks[i]) { n++; } }
  return n;
}

function pickNewSeed() {
  settings.seed = E.newSeed(Math.random());
  outlet(1, 'set', settings.seed);
}

function guard() {
  if (ready) { return true; }
  setStatus('warn', 'The device is still loading. Try again in a moment.');
  sendState();
  return false;
}

function setStatus(level, text) {
  statusLevel = level;
  statusText = text;
}

function refresh() {
  trimLocks();
  current = E.build(settings, locks);
  updateButtons();
  sendState();
}

function sendState() {
  if (!current) { return; }
  var state = {
    chords: current.display,
    key: current.keyName,
    seed: current.settings.seed,
    status: statusText,
    level: statusLevel,
    locked: lockCount()
  };
  outlet(0, 'setstate', JSON.stringify(state));
}

function setActive(varname, on, annotation) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (!obj) { return; }
    obj.message('active', on ? 1 : 0);
    if (annotation) { obj.message('annotation', annotation); }
  } catch (e) {
    post('dooblr-chords: could not update ' + varname + ': ' + e + '\n');
  }
}

// Partly ignored controls are drawn at 60% opacity (the text colour alpha);
// fully ignored ones are deactivated, which uses the 38% inactive colours.
var TEXT_PRIMARY = [0.902, 0.910, 0.922];
function setPartlyIgnored(varname, partly, annotation) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (!obj) { return; }
    obj.message('textcolor', TEXT_PRIMARY[0], TEXT_PRIMARY[1], TEXT_PRIMARY[2], partly ? 0.6 : 1);
    if (annotation) { obj.message('annotation', annotation); }
  } catch (e) {
    post('dooblr-chords: could not update ' + varname + ': ' + e + '\n');
  }
}

var SEED_HINT = 'The seed makes a progression repeatable. The same seed and settings always give the same chords.';

function updateButtons() {
  var waiting = ' It is unavailable until the device has finished loading.';
  setActive('generate', ready, BUTTON_HINTS.generate + (ready ? '' : waiting));
  setActive('write', ready, BUTTON_HINTS.write + (ready ? '' : waiting));
  var canReroll = ready && lockCount() > 0;
  setActive('reroll', canReroll, BUTTON_HINTS.reroll +
    (canReroll ? '' : (ready ? ' It is dimmed because no chord is locked yet: click a chord block to lock it.' : waiting)));
  var allLocked = !!current && lockCount() === current.chords.length;
  setPartlyIgnored('seed', allLocked, SEED_HINT +
    (allLocked ? ' Every chord is locked, so the seed only changes Humanize. Unlock a chord to use it fully.' : ''));
}

// ---- Live API ---------------------------------------------------------------

function firstValue(v) {
  if (v instanceof Array) { return v.length ? v[0] : 0; }
  return v;
}

function findTrack() {
  var obj = new LiveAPI('this_device');
  for (var depth = 0; depth < 10; depth++) {
    obj = new LiveAPI(obj.unquotedpath + ' canonical_parent');
    if (!obj || Number(obj.id) === 0) { return null; }
    if (obj.type === 'Track') { return obj; }
  }
  return null;
}

function findEmptySlot(track) {
  var count = track.getcount('clip_slots');
  for (var i = 0; i < count; i++) {
    var slot = new LiveAPI(track.unquotedpath + ' clip_slots ' + i);
    if (Number(firstValue(slot.get('has_clip'))) === 0) { return { api: slot, index: i }; }
  }
  return null;
}

function countNotes(clip, lengthBeats) {
  try {
    var raw = clip.call('get_notes_extended', 0, 128, 0, lengthBeats);
    if (raw instanceof Array) { raw = raw.join(' '); }
    var parsed = JSON.parse(String(raw));
    return parsed.notes ? parsed.notes.length : -1;
  } catch (e) {
    return -1; // cannot verify
  }
}

// Live 11+: add_new_notes with a dictionary. Falls back to the older
// set_notes / notes / note / done sequence if the new call adds nothing.
function addNotes(clip, notes, lengthBeats) {
  var method = 'add_new_notes';
  try {
    clip.call('add_new_notes', { notes: notes });
  } catch (e) {
    post('dooblr-chords: add_new_notes failed: ' + e + '\n');
  }
  var n = countNotes(clip, lengthBeats);
  if (n === notes.length || (n < 0 && notes.length === 0)) { return method; }
  if (n < 0) { return method + ' (unverified)'; }

  method = 'set_notes';
  try {
    if (n > 0) { clip.call('remove_notes_extended', 0, 128, 0, lengthBeats); }
    clip.call('set_notes');
    clip.call('notes', notes.length);
    for (var i = 0; i < notes.length; i++) {
      var x = notes[i];
      clip.call('note', x.pitch, x.start_time.toFixed(4), x.duration.toFixed(4), x.velocity, x.mute ? 1 : 0);
    }
    clip.call('done');
  } catch (e2) {
    post('dooblr-chords: set_notes failed: ' + e2 + '\n');
    return null;
  }
  n = countNotes(clip, lengthBeats);
  return (n === notes.length || n < 0) ? method : null;
}

function writeClip() {
  if (!current) { return false; }
  var track = findTrack();
  if (!track) {
    setStatus('warn', 'Could not find the track this device is on, so no clip was written.');
    sendState();
    return false;
  }
  var slot = findEmptySlot(track);
  if (!slot) {
    setStatus('warn', 'Every clip slot on this track is full. Add a scene or delete a clip, then press Write.');
    sendState();
    return false;
  }
  slot.api.call('create_clip', current.lengthBeats);
  var clip = new LiveAPI(slot.api.unquotedpath + ' clip');
  if (!clip || Number(clip.id) === 0) {
    setStatus('warn', 'Live did not create a clip in slot ' + (slot.index + 1) + '.');
    sendState();
    return false;
  }
  clip.set('name', current.clipName);
  var method = addNotes(clip, current.notes, current.lengthBeats);
  if (!method) {
    setStatus('warn', 'Created a clip in slot ' + (slot.index + 1) + ' but could not add its notes. See the Max window.');
  } else {
    setStatus('ok', 'Wrote ' + current.clipName + ' to slot ' + (slot.index + 1) + '.');
  }
  sendState();
  return !!method;
}
