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
//   dropfile <path>      a .mid file dropped on the SOURCE drop zone (live.drop)
//   useclip              read the clip shown in Live's Detail View as the source
//   clearsource          forget the source and go back to fresh progressions
//   openpanel <name> [v] open or close the Advanced or Source overlay panel
//   closepanel           close any open overlay panel
// With a source loaded, Generate/Reroll/Write make a VARIATION or a B SECTION
// of it (the VAR/B switch, param kind) instead of a fresh progression.
// Outlets:
//   0  setstate <json>   to the chord display (jsui)
//   1  set <seed>        to the Seed number box (no output, no feedback loop)

autowatch = 0;
inlets = 1;
outlets = 2;
setinletassist(0, 'init, param <name> <value>, generate, reroll, write, lock <index>, dropfile <path>, useclip, clearsource, openpanel, closepanel');
setoutletassist(0, 'setstate <json> to the chord display');
setoutletassist(1, 'set <seed> to the Seed number box');

var self = this;
var E = DC.engine;

var PARAM_NAMES = ['root', 'mode', 'length', 'duration', 'color', 'chromatic',
  'register', 'spread', 'bass', 'velocity', 'humanize', 'seed', 'kind'];

var settings = E.normalize(E.DEFAULTS);
var locks = [];
var current = null;
var ready = false;
var statusText = 'Press Generate to write a clip into the next empty slot.';
var statusLevel = 'info';
var source = null;        // analysis of the source clip or file (DC.analysis.analyze)
var MAX_FILE_BYTES = 4 * 1024 * 1024;

// Button varnames in the patcher, for dimming with an explanation.
var BUTTON_HINTS = {
  generate: 'Generate makes a new progression with a new seed and writes it as a clip in the next empty slot on this track.',
  reroll: 'Reroll keeps the locked chords, regenerates the others with a new seed and writes a clip.',
  write: 'Write puts the progression shown below into the next empty clip slot without changing it.',
  useclip: 'USE CLIP reads the MIDI clip shown in the Detail View (or the selected clip) as the source.',
  clearsource: 'CLEAR forgets the source, so Generate makes fresh progressions again.',
  kind: 'VAR writes a variation of the source: same key, length and rhythm, some chords swapped. B writes a contrasting B section that leads back to the source.'
};

// Overlay panels. Members start hidden in the patcher; js toggles the hidden
// attribute so they cover the chord display without being saved parameters.
// The first name in each list is the backdrop.
var PANELS = {
  advanced: ['adv_bg', 'adv_title', 'adv_close', 'adv_bars_label', 'adv_seed_label', 'bass_label',
    'duration', 'seed', 'write', 'register', 'spread', 'velocity', 'humanize', 'bass'],
  source: ['src_bg', 'src_title', 'src_close', 'srcdrop', 'useclip', 'clearsource', 'kind', 'srcinfo']
};
var PANEL_NAMES = ['advanced', 'source'];
var openPanel = null;

// ---- messages -------------------------------------------------------------

function loadbang() {
  closepanel();
  refresh();
}

function init() {
  ready = true;
  closepanel();
  refresh();
}

function param(name, value) {
  if (!isParam(name)) {
    post('dooblr-chords: unknown parameter ' + name + '\n');
    return;
  }
  var before = settings[name];
  settings[name] = value;
  settings = E.normalize(settings);
  if (name === 'kind' && settings.kind !== before && source) { locks = []; }
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
    setStatus('info', 'Lock a chord first: click a block, then press Reroll.');
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

// ---- source -----------------------------------------------------------------

function dropfile() {
  var path = Array.prototype.slice.call(arguments).join(' ');
  if (!path) { return; }               // live.drop sends an empty path when cleared
  var file = path.replace(/^.*[\/:]/, '');
  if (!E.isMidiPath(path)) {
    setStatus('warn', 'Drop a MIDI file (.mid or .midi). ' + file + ' is not one.');
    sendState();
    return;
  }
  var bytes;
  try {
    bytes = readFileBytes(path);
  } catch (e) {
    setStatus('warn', 'Could not read ' + file + ': ' + errText(e));
    sendState();
    return;
  }
  var an;
  try {
    an = E.sourceFromMidi(bytes, E.baseName(path));
  } catch (e2) {
    setStatus('warn', file + ': ' + errText(e2));
    sendState();
    return;
  }
  // While the Set is loading, live.drop re-sends its saved path before
  // live.thisdevice: keep the saved Root/Mode/Length rather than resetting them.
  setSource(an, ready);
}

function useclip() {
  if (!guard()) { return; }
  var clip = findSourceClip();
  if (!clip) {
    setStatus('warn', 'No clip is selected. Select a MIDI clip, then press USE CLIP.');
    sendState();
    return;
  }
  if (Number(firstValue(clip.get('is_midi_clip'))) !== 1) {
    setStatus('warn', 'The selected clip is an audio clip. Select a MIDI clip.');
    sendState();
    return;
  }
  var range = clipRange(clip);
  var notes;
  try {
    var raw = clip.call('get_notes_extended', 0, 128, range.start, range.length);
    if (raw instanceof Array) { raw = raw.join(' '); }
    notes = JSON.parse(String(raw)).notes || [];
  } catch (e) {
    setStatus('warn', 'Could not read the notes of the selected clip: ' + errText(e));
    sendState();
    return;
  }
  var shifted = [];
  for (var i = 0; i < notes.length; i++) {
    var n = notes[i];
    var st = Number(n.start_time) - range.start;
    if (st < -1e-6 || st >= range.length) { continue; }
    shifted.push({ pitch: n.pitch, start_time: Math.max(0, st), duration: Number(n.duration), velocity: n.velocity, mute: n.mute });
  }
  var an;
  try {
    an = E.analyzeSource(shifted, { lengthBeats: range.length, name: String(firstValue(clip.get('name')) || 'Clip') });
  } catch (e2) {
    setStatus('warn', 'Selected clip: ' + errText(e2));
    sendState();
    return;
  }
  // A clip source is not saved with the Set, so forget any dropped file.
  clearDrop();
  setSource(an, true);
}

function clearsource() {
  if (!source) { return; }
  source = null;
  locks = [];
  clearDrop();
  setStatus('info', 'Source cleared.');
  refresh();
}

function setSource(an, applyKey) {
  source = an;
  locks = [];
  if (applyKey) {
    settings.root = an.key.root;
    settings.mode = an.key.mode;
    settings.length = Math.max(2, an.segments.length);
    settings = E.normalize(settings);
    setControl('root', settings.root);
    setControl('mode', settings.mode);
    setControl('length', settings.length);
  }
  var shown = E.sourceDisplay(an, settings.root, settings.mode);
  setStatus('ok', 'Loaded ' + shown.name + ': ' + shown.detectedKey + ', ' + shown.chords.length + ' chords.');
  refresh();
}

function readFileBytes(path) {
  var f = new File(path, 'read');
  if (!f.isopen) { throw new Error('the file could not be opened'); }
  var out = [];
  try {
    var size = f.eof;
    if (size > MAX_FILE_BYTES) { throw new Error('the file is larger than 4 MB'); }
    f.position = 0;
    while (f.position < size) {
      var chunk = f.readbytes(Math.min(1024, size - f.position));
      if (!chunk || !chunk.length) { break; }
      for (var i = 0; i < chunk.length; i++) { out.push(chunk[i] & 0xff); }
    }
  } finally {
    f.close();
  }
  return out;
}

function errText(e) { return (e && e.message) ? e.message : String(e); }

function openpanel(name, value) {
  var key = String(name || '');
  if (key !== 'advanced' && key !== 'source') { return; }
  // live.text in toggle mode sends 1 when turned on and 0 when turned off.
  // A bang (no value) toggles. Opening one panel closes the other.
  if (value === 0 || value === '0') {
    if (openPanel === key) { openPanel = null; }
  } else if (value === undefined || value === 'bang') {
    openPanel = (openPanel === key) ? null : key;
  } else {
    openPanel = key;
  }
  applyPanelVisibility();
}

function closepanel() {
  openPanel = null;
  applyPanelVisibility();
}

function anything() {
  post('dooblr-chords: unknown message ' + messagename + '\n');
}

// ---- state ------------------------------------------------------------------

function isParam(name) {
  for (var i = 0; i < PARAM_NAMES.length; i++) { if (PARAM_NAMES[i] === name) { return true; } }
  return false;
}

function trimLocks(count) {
  var out = [];
  for (var i = 0; i < count; i++) { out.push(locks[i] || null); }
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
  if (!source) {
    trimLocks(settings.length);
    current = E.build(settings, locks);
  } else {
    // Keep the B-section rhythm plan while chords are locked so the lock
    // positions stay meaningful.
    var keep = (current && current.derived && lockCount() > 0) ? { rhythm: current.rhythm } : null;
    try {
      current = E.build(settings, locks, { analysis: source, keep: keep });
    } catch (e) {
      post('dooblr-chords: ' + errText(e) + '\n');
      setStatus('warn', 'Could not derive from the source: ' + errText(e) + ' Press CLEAR to go back.');
      current = E.build(settings, []);
    }
    trimLocks(current.chords.length);
  }
  updateButtons();
  sendState();
  updateSourceInfo();
}

function setControl(varname, value) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (obj) { obj.message('set', value); }
  } catch (e) {
    post('dooblr-chords: could not set ' + varname + ': ' + e + '\n');
  }
}

function clearDrop() {
  try {
    var obj = self.patcher.getnamed('srcdrop');
    if (obj) { obj.message('clear'); }
  } catch (e) {
    post('dooblr-chords: could not clear the drop zone: ' + e + '\n');
  }
}

function updateSourceInfo() {
  var text = 'Drop a .mid file or press USE CLIP.';
  if (source) {
    var d = E.sourceDisplay(source, settings.root, settings.mode);
    text = d.name + ' \u00B7 ' + d.detectedKey + ' \u00B7 ' + d.chords.length + ' chords, ' + d.bars + ' bars';
  }
  try {
    var obj = self.patcher.getnamed('srcinfo');
    if (obj) { obj.message('set', text); }
  } catch (e) {
    post('dooblr-chords: could not update srcinfo: ' + e + '\n');
  }
  updateSourceBadge();
}

// Small chip on the main view so a loaded source is not hidden inside the panel.
function updateSourceBadge() {
  var text = '';
  var hide = true;
  if (source) {
    var d = E.sourceDisplay(source, settings.root, settings.mode);
    var kind = (settings.kind === 1) ? 'B' : 'VAR';
    text = kind + ' \u00B7 ' + d.name;
    hide = false;
  }
  try {
    var obj = self.patcher.getnamed('srcbadge');
    if (!obj) { return; }
    obj.message('set', text);
    obj.hidden = hide ? 1 : 0;
  } catch (e) {
    post('dooblr-chords: could not update srcbadge: ' + e + '\n');
  }
}

function setHidden(varname, hide) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (!obj) { return; }
    obj.hidden = hide ? 1 : 0;
  } catch (e) {
    post('dooblr-chords: could not hide ' + varname + ': ' + e + '\n');
  }
}

function applyPanelVisibility() {
  var i, j, name, members, show;
  for (i = 0; i < PANEL_NAMES.length; i++) {
    name = PANEL_NAMES[i];
    members = PANELS[name];
    show = openPanel === name;
    for (j = 0; j < members.length; j++) { setHidden(members[j], !show); }
  }
  // Keep the opener buttons in sync (they are visible on the main view).
  setControl('open_advanced', openPanel === 'advanced' ? 1 : 0);
  setControl('open_source', openPanel === 'source' ? 1 : 0);
}

function sendState() {
  if (!current) { return; }
  var state = {
    chords: current.display,
    heading: current.derived ? current.derived.heading : 'PROGRESSION',
    key: current.keyName + (current.derived && current.derived.regionText ? ' \u00B7 ' + current.derived.regionText : ''),
    // The Root and Mode menus already show the key, so the display only
    // draws the region a B section moves to.
    region: (current.derived && current.derived.regionText) ? current.derived.regionText : '',
    seed: current.settings.seed,
    status: statusText,
    level: statusLevel,
    locked: lockCount(),
    source: (current.derived ? current.derived.source : null),
    derived: (current.derived ? current.derived.label : null)
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
// `base` is the control's normal text colour: advanced controls (Seed) use
// the muted colour, so dimming must not turn them bright.
var TEXT_PRIMARY = [0.902, 0.910, 0.922];
var TEXT_MUTED = [0.541, 0.569, 0.612];
function setPartlyIgnored(varname, partly, annotation, base) {
  var c = base || TEXT_PRIMARY;
  try {
    var obj = self.patcher.getnamed(varname);
    if (!obj) { return; }
    obj.message('textcolor', c[0], c[1], c[2], partly ? 0.6 : 1);
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
  setActive('useclip', ready, BUTTON_HINTS.useclip + (ready ? '' : waiting));
  setActive('clearsource', !!source, BUTTON_HINTS.clearsource + (source ? '' : ' It is dimmed because no source is loaded.'));
  setActive('kind', !!source, BUTTON_HINTS.kind + (source ? '' : ' It is dimmed because no source is loaded: drop a .mid file or press USE CLIP first.'));
  setPartlyIgnored('seed', allLocked, SEED_HINT +
    (allLocked ? ' Every chord is locked, so the seed only changes Humanize. Unlock a chord to use it fully.' : ''), TEXT_MUTED);
}

// ---- Live API ---------------------------------------------------------------

// The clip in the Detail View, else the clip in the highlighted slot.
function findSourceClip() {
  var paths = ['live_set view detail_clip', 'live_set view highlighted_clip_slot clip'];
  for (var i = 0; i < paths.length; i++) {
    try {
      var c = new LiveAPI(paths[i]);
      if (c && Number(c.id) !== 0) { return c; }
    } catch (e) {
      post('dooblr-chords: ' + paths[i] + ': ' + e + '\n');
    }
  }
  return null;
}

// The looped region if looping, else start to end marker. Beats.
function clipRange(clip) {
  var looping = Number(firstValue(clip.get('looping'))) === 1;
  var a = Number(firstValue(clip.get(looping ? 'loop_start' : 'start_marker')));
  var b = Number(firstValue(clip.get(looping ? 'loop_end' : 'end_marker')));
  if (!(b > a)) { a = 0; b = Number(firstValue(clip.get('length'))) || 16; }
  return { start: a, length: b - a };
}

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
    setStatus('warn', 'Could not find this track, so no clip was written.');
    sendState();
    return false;
  }
  var slot = findEmptySlot(track);
  if (!slot) {
    setStatus('warn', 'Every clip slot on this track is full. Add a scene, then press Write.');
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
    setStatus('warn', 'Clip in slot ' + (slot.index + 1) + ' has no notes: see the Max window.');
  } else {
    setStatus('ok', 'Wrote ' + current.clipName + ' to slot ' + (slot.index + 1) + '.');
  }
  sendState();
  return !!method;
}
