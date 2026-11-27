// dooblr-chords: Max `js` glue (classic js object, ES5 only).
// The build script prepends the engine (src/engine/*.js) to this file and
// writes the result to device/dooblr-chords/dooblr_chords.js.
//
// Messages in:
//   init                 from live.thisdevice; LiveAPI is safe after this
//   param <name> <value> from each live.* control (via [prepend param <name>])
//   generate             new seed, clear locks; write a clip (CLIP) or queue at next bar (LIVE)
//   reroll               new seed, keep locked chords; write a clip (CLIP) or queue at next bar (LIVE)
//   write                write the current preview as a clip (CLIP only)
//   tick                 LIVE clock (also driven by a 10ms Task after init)
//   notifydeleted/closebang  all-notes-off and stop the clock
//   lock <index>         toggle the lock on one chord (from the jsui)
//   unlockall            clear every lock
//   ascii <0|1>          use b/# instead of the flat/sharp glyphs on screen
//   dropfile <path>      a .mid file dropped on the SOURCE drop zone (live.drop)
//   useclip              read the clip shown in Live's Detail View as the source
//   clearsource          forget the source and go back to fresh progressions
//   commit               write the sounding LIVE progression to the next empty clip (tag · LIVE)
//   bus <json>           a hello (or other) message from receive dooblr_chords_bus
//   openadvanced         open the floating Advanced window (p advanced)
// With a source loaded, Generate/Reroll/Write make a VARIATION or a B SECTION
// of it (the VAR/B switch, param kind) instead of a fresh progression.
// Outlets:
//   0  setstate <json>   to the chord display (jsui)
//   1  set <seed>        to the Seed number box (no output, no feedback loop)
//   2  raw MIDI bytes    note-on 144 p v / note-off 128 p 0, to midiout

autowatch = 0;
inlets = 1;
outlets = 4;
setinletassist(0, 'init, param <name> <value>, generate, reroll, write, commit, lock <index>, dropfile <path>, useclip, clearsource, openadvanced, bus');
setoutletassist(0, 'setstate <json> to the chord display');
setoutletassist(1, 'set <seed> to the Seed number box');
setoutletassist(2, 'raw MIDI bytes to midiout');
setoutletassist(3, 'harmony bus JSON to send dooblr_chords_bus');

var self = this;
var E = DC.engine;
var S = DC.scheduler;

var PARAM_NAMES = ['root', 'mode', 'length', 'duration', 'color', 'chromatic',
  'register', 'spread', 'bass', 'velocity', 'humanize', 'seed', 'kind', 'lead', 'density', 'notes',
  'playmode', 'evolve', 'buschan', 'spice'];

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
  generate: 'Generate makes a new progression with a new seed and writes it as a clip in the next empty slot on this track. In LIVE mode it starts the new progression on the next bar and does not write a clip.',
  reroll: 'Reroll keeps the locked chords, regenerates the others with a new seed and writes a clip. In LIVE mode the new chords start on the next bar and no clip is written.',
  write: 'Write puts the progression shown below into the next empty clip slot without changing it.',
  playmode: 'CLIP writes Session clips when you press Generate. LIVE plays the generated progression as MIDI in real time, synced to Live\'s transport, and does not write clips.',
  evolve: 'EVOLVE (LIVE only) lightly varies one or two inner unlocked chords at cycle boundaries: Off, every cycle, every 2 cycles, or every 4 cycles. The first and last chords stay put. Deterministic for a given seed and cycle.',
  commit: 'TO CLIP writes the progression as it is currently playing in LIVE mode — including Evolve mutations and settings already applied — into the next empty Session clip slot. Live playback is not stopped or retriggered. It is dimmed in CLIP mode; use Generate, Reroll or Write there.',
  buschan: 'Bus Channel (A, B, C or D) is the harmony link that dooblr-chord-receiver devices listen to. Use a different letter when two dooblr-chords devices share a Set.',
  useclip: 'USE CLIP reads the MIDI clip shown in the Detail View (or the selected clip) as the source.',
  clearsource: 'CLEAR forgets the source, so Generate makes fresh progressions again.',
  kind: 'VAR writes a variation of the source: same key, length and rhythm, some chords swapped. B writes a contrasting B section that leads back to the source.',
  lead: 'LEAD writes a monophonic melody over the chord progression instead of the chords themselves. The same seed, key, length and locks shape the line.',
  spice: 'Spice is a macro for how adventurous the progression is. It sets Color and Chromatic together along a fixed curve. The individual Color and Chromatic knobs stay in the Advanced window.',
  notes: 'Notes sets the chord type: a triad (3 notes), 7th (4), 9th (5) or 11th (6). Color and Spice still colour the tones — alterations, borrowed chords and substitutions — but they do not change how many notes each chord has. When Bass is on, the bass is one of those notes: it takes the root out of the upper voicing, so a triad still has three sounding voices. A slash bass is also one of the N.'
};

// The compact source chip on the main view (hidden when no source is loaded).
var BADGE_RECT = [268, 16, 124, 20];
var badgeOn = false;

// Spice macro: do not apply Spice -> Color/Chromatic during init (that would
// smash recalled Color and Chromatic on older Sets that have no Spice). After
// init, Spice writes those two with `set` (no output) and Color/Chromatic only
// update the Spice dial, never write back, so there is no feedback loop.
var spiceReady = false;
var applyingMacro = false;

// LIVE scheduler. The host polls Live's transport every 10ms with a Max Task
// and feeds { playing, beats, barBeats, tempo } into the pure js scheduler.
var sched = S.create();
var clockTask = null;
var deviceObs = null;
var lastPlayIndex = -1;
var lastPlayCycle = -1;
var MIDI_OUT = 2;
var BUS_OUT = 3;
var lastHarmonyId = '';
var lastHarmonyAt = 0;
var BOUNDARY_PARAMS = {
  root: 1, mode: 1, length: 1, duration: 1, color: 1, chromatic: 1, spice: 1, notes: 1,
  register: 1, spread: 1, bass: 1, velocity: 1, humanize: 1, density: 1, lead: 1,
  kind: 1, seed: 1
};

function isLive() { return !!settings.playmode; }


// ---- messages -------------------------------------------------------------

function loadbang() {
  refresh();
}

function init() {
  ready = true;
  spiceReady = true;
  syncSpiceFromParts();
  refresh();
  applyPlayMode(-1);
  startClock();
  bindDevice();
  broadcastHarmony(null, true);
}

function notifydeleted() { teardownLive(); }
function closebang() { teardownLive(); }

function param(name, value) {
  if (!isParam(name)) {
    post('dooblr-chords: unknown parameter ' + name + '\n');
    return;
  }
  var before = settings[name];
  if (name === 'notes') { value = E.notesFromTab(value); }
  settings[name] = value;
  settings = E.normalize(settings);
  if (name === 'kind' && settings.kind !== before && source) { locks = []; }
  if (name === 'playmode') {
    applyPlayMode(before);
    refresh();
    return;
  }
  if (name === 'evolve') {
    S.setEvolveEvery(sched, E.evolveEvery(settings.evolve));
    refresh();
    return;
  }
  if (name === 'buschan') {
    refresh();
    broadcastHarmony(null, true);
    return;
  }
  if (name === 'spice') {
    if (spiceReady) { applySpice(settings.spice); }
    refresh();
    if (isLive() && BOUNDARY_PARAMS.spice && sched.started && sched.playing) {
      S.queueBuild(sched, current, 'chord');
      setStatus('info', 'LIVE: that change applies at the next chord.');
      sendState();
    }
    return;
  }
  if ((name === 'color' || name === 'chromatic') && spiceReady) {
    syncSpiceFromParts();
  }
  refresh();
  if (isLive() && BOUNDARY_PARAMS[name] && sched.started && sched.playing) {
    S.queueBuild(sched, current, 'chord');
    setStatus('info', 'LIVE: that change applies at the next chord.');
    sendState();
  }
}

function generate() {
  if (!guard()) { return; }
  locks = [];
  pickNewSeed();
  refresh();
  if (isLive()) {
    S.queueBuild(sched, current, 'bar');
    setStatus('ok', 'New progression starts at the next bar.');
    sendState();
    return;
  }
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
  if (isLive()) {
    S.queueBuild(sched, current, 'bar');
    setStatus('ok', 'Reroll starts at the next bar.');
    sendState();
    return;
  }
  writeClip();
}

function write() {
  if (!guard()) { return; }
  if (isLive()) {
    setStatus('info', 'Write is only for CLIP mode: it creates a Session clip. Switch to CLIP to use Write, or press TO CLIP to capture what is playing live.');
    sendState();
    return;
  }
  if (!current) { refresh(); }
  writeClip();
}

function commit() {
  if (!guard()) { return; }
  if (!isLive()) {
    setStatus('info', 'TO CLIP is only for LIVE mode: it writes the sounding progression to a Session clip without stopping playback. Switch to LIVE to use it.');
    sendState();
    return;
  }
  var build = soundingBuild() || current;
  if (!build) { refresh(); build = current; }
  if (!build) { return; }
  writeClip(build, { tag: 'LIVE' });
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

function openadvanced() {
  try {
    var box = self.patcher.getnamed('advanced');
    if (box) {
      box.message('open');
      box.message('front');
    }
  } catch (e) {}
  try {
    var tp = self.patcher.getnamed('thispatcher');
    if (tp) {
      tp.message('script', 'sendbox', 'advanced', 'open');
      tp.message('script', 'sendbox', 'advanced', 'front');
    }
  } catch (e2) {}
  post('dooblr-chords: advanced window open\n');
}

function applySpice(s) {
  if (applyingMacro) { return; }
  applyingMacro = true;
  try {
    settings.color = DC.spice.colorFromSpice(s);
    settings.chromatic = DC.spice.chromaticFromSpice(s);
    settings = E.normalize(settings);
    setControl('color', settings.color);
    setControl('chromatic', settings.chromatic);
  } finally {
    applyingMacro = false;
  }
}

function syncSpiceFromParts() {
  if (applyingMacro) { return; }
  var next = DC.spice.spiceFromParts(settings.color, settings.chromatic);
  settings.spice = next;
  applyingMacro = true;
  try { setControl('spice', next); } finally { applyingMacro = false; }
}

function getObj(varname) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (obj) { return obj; }
  } catch (e) {}
  try {
    var box = self.patcher.getnamed('advanced');
    if (box && box.subpatcher) {
      var sp = box.subpatcher();
      if (sp) { return sp.getnamed(varname); }
    }
  } catch (e2) {}
  return null;
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
    var obj = getObj(varname);
    if (obj) { obj.message('set', value); }
  } catch (e) {
    post('dooblr-chords: could not set ' + varname + ': ' + e + '\n');
  }
}

function clearDrop() {
  try {
    var obj = getObj('srcdrop');
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
    var obj = getObj('srcinfo');
    if (obj) { obj.message('set', text); }
  } catch (e) {
    post('dooblr-chords: could not update srcinfo: ' + e + '\n');
  }
  updateSourceBadge();
}

// Small chip on the main view so a loaded source is not hidden inside the panel.
function updateSourceBadge() {
  var text = '';
  badgeOn = false;
  if (source) {
    var d = E.sourceDisplay(source, settings.root, settings.mode);
    var kind = (settings.kind === 1) ? 'B' : 'VAR';
    text = kind + ' \u00B7 ' + d.name;
    badgeOn = true;
  }
  try {
    var obj = getObj('srcbadge');
    if (obj) {
      obj.message('set', text);
      obj.message('hidden', badgeOn ? 0 : 1);
    }
  } catch (e) {
    post('dooblr-chords: could not update srcbadge: ' + e + '\n');
  }
}

function soundingBuild() {
  if (isLive() && sched.started && sched.build) { return sched.build; }
  return current;
}

function sendState() {
  var view = soundingBuild();
  if (!view) { return; }
  var playIndex = (isLive() && sched.started) ? sched.chordIndex : -1;
  var state = {
    chords: view.display,
    heading: view.heading || (view.derived ? view.derived.heading : 'PROGRESSION'),
    key: view.keyName + (view.derived && view.derived.regionText ? ' \u00B7 ' + view.derived.regionText : ''),
    // The Root and Mode menus already show the key, so the display only
    // draws the region a B section moves to.
    region: (view.derived && view.derived.regionText) ? view.derived.regionText : '',
    seed: view.settings.seed,
    status: statusText,
    level: statusLevel,
    locked: lockCount(),
    source: (view.derived ? view.derived.source : null),
    derived: (view.derived ? view.derived.label : null),
    playIndex: playIndex,
    playmode: isLive() ? 1 : 0,
    lead: settings.lead ? 1 : 0
  };
  outlet(0, 'setstate', JSON.stringify(state));
  lastPlayIndex = playIndex;
  lastPlayCycle = sched.cycle;
}

function setActive(varname, on, annotation) {
  try {
    var obj = getObj(varname);
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
    var obj = getObj(varname);
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
  var live = isLive();
  setActive('generate', ready, BUTTON_HINTS.generate + (ready ? '' : waiting));
  setActive('write', ready && !live, live
    ? BUTTON_HINTS.write + ' It is dimmed because LIVE mode plays the progression in real time. Press TO CLIP to write the sounding progression, or switch to CLIP to use Write.'
    : BUTTON_HINTS.write + (ready ? '' : waiting));
  setActive('commit', ready && live, live
    ? BUTTON_HINTS.commit
    : BUTTON_HINTS.commit + ' It is dimmed in CLIP mode: switch to LIVE to write the sounding progression.');
  var canReroll = ready && lockCount() > 0;
  setActive('reroll', canReroll, BUTTON_HINTS.reroll +
    (canReroll ? '' : (ready ? ' It is dimmed because no chord is locked yet: click a chord block to lock it.' : waiting)));
  var allLocked = !!current && lockCount() === current.chords.length;
  setActive('useclip', ready, BUTTON_HINTS.useclip + (ready ? '' : waiting));
  setActive('clearsource', !!source, BUTTON_HINTS.clearsource + (source ? '' : ' It is dimmed because no source is loaded.'));
  setActive('kind', !!source, BUTTON_HINTS.kind + (source ? '' : ' It is dimmed because no source is loaded: drop a .mid file or press USE CLIP first.'));
  setPartlyIgnored('seed', allLocked, SEED_HINT +
    (allLocked ? ' Every chord is locked, so the seed only changes Humanize. Unlock a chord to use it fully.' : ''), TEXT_MUTED);
  var leadOn = !!settings.lead;
  setActive('density', ready && leadOn, leadOn
    ? 'Density sets how busy the lead line is: higher values use shorter notes and fewer rests.'
    : 'Density sets how busy the lead line is. It is dimmed because Lead is off: turn Lead on to use it.');
  setActive('notes', ready && !leadOn, leadOn
    ? BUTTON_HINTS.notes + ' It is dimmed because Lead is on: a lead line is monophonic, so the chord-type count does not apply.'
    : BUTTON_HINTS.notes);
  setActive('playmode', ready, BUTTON_HINTS.playmode + (ready ? '' : waiting));
  setActive('evolve', ready && live, live
    ? BUTTON_HINTS.evolve
    : BUTTON_HINTS.evolve + ' It is dimmed in CLIP mode: Evolve only runs while the device is playing a live progression.');
  setActive('buschan', ready, BUTTON_HINTS.buschan + (ready ? '' : waiting));
  setGenerateLabel(leadOn, live);
}

function setGenerateLabel(leadOn, live) {
  if (live === undefined) { live = isLive(); }
  var label = live ? (leadOn ? 'New Lead' : 'New Progression') : (leadOn ? 'Generate Lead' : 'Generate');
  try {
    var obj = getObj('generate');
    if (!obj) { return; }
    obj.message('text', label);
    obj.message('texton', label);
  } catch (e) {
    post('dooblr-chords: could not update the Generate label: ' + e + '\n');
  }
}


// ---- LIVE transport ---------------------------------------------------------

function applyPlayMode(previous) {
  var live = isLive();
  S.setEvolveEvery(sched, E.evolveEvery(settings.evolve));
  if (current) { S.setBuild(sched, current); }
  S.setLive(sched, live);
  if (live) {
    startClock();
    if (previous === 0 || previous === -1) {
      setStatus('info', 'LIVE: plays on the next bar when the transport is running. Incoming MIDI still passes through. TO CLIP writes the sounding progression to a Session slot.');
    }
  } else {
    silenceLive();
    startClock();
    if (previous === 1) {
      setStatus('info', 'CLIP: Generate writes a clip into the next empty slot. Receivers still follow this progression from the transport.');
    }
  }
}

function startClock() {
  if (clockTask || typeof Task === 'undefined') { return; }
  try {
    clockTask = new Task(tick, self);
    clockTask.interval = 10;
    clockTask['repeat'](); // Max Task API; bracket form so the ES5 scanner does not see String.repeat
  } catch (e) {
    clockTask = null;
    post('dooblr-chords: could not start the LIVE clock: ' + e + '\n');
  }
}

function stopClock() {
  if (!clockTask) { return; }
  try { clockTask.cancel(); } catch (e) {}
  clockTask = null;
}

function teardownLive() {
  silenceLive();
  stopClock();
  deviceObs = null;
}

function silenceLive() {
  S.setLive(sched, false);
  var out = S.process(sched, { playing: false, beats: 0, barBeats: 4, tempo: 120 }, {});
  emitMidi(out);
  if (isLive()) { S.setLive(sched, true); }
}

function bindDevice() {
  if (typeof LiveAPI === 'undefined') { return; }
  try {
    deviceObs = new LiveAPI(onDevice, 'live_set this_device');
    deviceObs.property = 'is_active';
  } catch (e) {
    deviceObs = null;
  }
}

function onDevice(args) {
  try {
    var prop = args && args[0];
    var val = args && args[1];
    if (String(prop) === 'is_active' && Number(val) === 0) { silenceLive(); }
  } catch (e) {}
}

function readTransport() {
  if (typeof LiveAPI === 'undefined') { return null; }
  try {
    var api = new LiveAPI('live_set');
    if (!api || Number(api.id) === 0) { return null; }
    var playing = Number(firstValue(api.get('is_playing'))) === 1;
    var beats = Number(firstValue(api.get('current_song_time')));
    var tempo = Number(firstValue(api.get('tempo'))) || 120;
    var num = Number(firstValue(api.get('signature_numerator'))) || 4;
    var den = Number(firstValue(api.get('signature_denominator'))) || 4;
    if (!(den > 0)) { den = 4; }
    var barBeats = num * (4 / den);
    if (!(barBeats > 0)) { barBeats = 4; }
    if (isNaN(beats)) { beats = 0; }
    return { playing: playing, beats: beats, barBeats: barBeats, tempo: tempo };
  } catch (e) {
    return null;
  }
}

function emitMidi(out) {
  if (!out) { return; }
  var evs, i, ev;
  if (out.events && out.events.length) {
    evs = out.events;
  } else {
    evs = [];
    if (out.offs) { for (i = 0; i < out.offs.length; i++) { evs.push({ type: 'off', pitch: out.offs[i].pitch, velocity: 0 }); } }
    if (out.ons) { for (i = 0; i < out.ons.length; i++) { evs.push({ type: 'on', pitch: out.ons[i].pitch, velocity: out.ons[i].velocity }); } }
  }
  for (i = 0; i < evs.length; i++) {
    ev = evs[i];
    if (ev.type === 'off') { outlet(MIDI_OUT, 128, ev.pitch, 0); }
    else { outlet(MIDI_OUT, 144, ev.pitch, ev.velocity); }
  }
}

function tick() {
  var clockSnap = readTransport();
  if (isLive()) {
    if (!clockSnap) { broadcastHarmony(null, false); return; }
    if (current && !sched.build) { S.setBuild(sched, current); }
    var hooks = {
      evolve: function (prev, cycle) {
        var next = E.evolveBuild(prev, cycle, locks);
        if (next) { current = next; }
        return next;
      }
    };
    var out = S.process(sched, clockSnap, hooks);
    emitMidi(out);
    if (sched.build) { current = sched.build; }
    if (out.chordIndex !== lastPlayIndex || out.cycle !== lastPlayCycle || out.started !== !!sched.started) {
      sendState();
    }
  }
  broadcastHarmony(clockSnap, false);
}

// Tests and a manual bang can step the clock without a Task.
function clock(playing, beats, barBeats, tempo) {
  var snap = {
    playing: !!playing,
    beats: typeof beats === 'number' ? beats : 0,
    barBeats: barBeats > 0 ? barBeats : 4,
    tempo: tempo > 0 ? tempo : 120
  };
  var out;
  if (isLive()) {
    if (current && !sched.build) { S.setBuild(sched, current); }
    var hooks = {
      evolve: function (prev, cycle) {
        var next = E.evolveBuild(prev, cycle, locks);
        if (next) { current = next; }
        return next;
      }
    };
    out = S.process(sched, snap, hooks);
    emitMidi(out);
    if (sched.build) { current = sched.build; }
    sendState();
  }
  broadcastHarmony(snap, true);
  return out;
}

function broadcastHarmony(clockSnap, force) {
  var P = DC.protocol;
  var build = soundingBuild() || current;
  if (!build || !P) { return; }
  var playing = !!(clockSnap && clockSnap.playing);
  var beats = (clockSnap && typeof clockSnap.beats === 'number') ? clockSnap.beats : 0;
  var opts = { playing: playing, beats: beats, channel: settings.buschan };
  if (isLive() && sched.started && sched.chordIndex >= 0) {
    opts.index = sched.chordIndex;
    opts.playing = true;
  }
  var payload = P.fromBuild(build, opts);
  if (!payload) { return; }
  var id = P.identity(payload);
  var now = (typeof Date !== 'undefined' && Date.now) ? Date.now() : 0;
  if (!force && id === lastHarmonyId && now && (now - lastHarmonyAt) < 250) { return; }
  lastHarmonyId = id;
  lastHarmonyAt = now;
  outlet(BUS_OUT, P.encode(payload));
}

// Incoming bus messages (hello from a receiver on this channel).
function bus() {
  var raw = Array.prototype.slice.call(arguments).join(' ');
  var msg = DC.protocol.decode(raw);
  if (!msg) { return; }
  if (msg.type === 'hello' && DC.protocol.clampChan(msg.ch) === settings.buschan) {
    broadcastHarmony(readTransport(), true);
  }
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

function writeClip(build, opts) {
  build = build || current;
  opts = opts || {};
  if (!build) { return false; }
  var track = findTrack();
  if (!track) {
    setStatus('warn', 'Could not find this track, so no clip was written.');
    sendState();
    return false;
  }
  var slot = findEmptySlot(track);
  if (!slot) {
    setStatus('warn', 'Every clip slot on this track is full. Add a scene, then press Write or TO CLIP.');
    sendState();
    return false;
  }
  slot.api.call('create_clip', build.lengthBeats);
  var clip = new LiveAPI(slot.api.unquotedpath + ' clip');
  if (!clip || Number(clip.id) === 0) {
    setStatus('warn', 'Live did not create a clip in slot ' + (slot.index + 1) + '.');
    sendState();
    return false;
  }
  var name = build.clipName || '';
  if (opts.tag) { name = name + ' · ' + opts.tag; }
  clip.set('name', name);
  var method = addNotes(clip, build.notes, build.lengthBeats);
  if (!method) {
    setStatus('warn', 'Clip in slot ' + (slot.index + 1) + ' has no notes: see the Max window.');
  } else {
    setStatus('ok', 'Wrote ' + name + ' to slot ' + (slot.index + 1) + '.');
  }
  sendState();
  return !!method;
}
