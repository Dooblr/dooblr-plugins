// dooblr-titan: Max `js` glue (classic js object, ES5 only).
// The build script prepends the engine (src/engine/*.js) to this file and
// writes the result to device/dooblr-titan/dooblr_titan.js.
//
// Messages in:
//   init                 from live.thisdevice; LiveAPI is safe after this
//   param <name> <value> from each live.* control (via [prepend param <name>])
//   tighten              improve the loaded clip and write a new clip
//   generate             build four variations of the loaded clip (no write)
//   another              new seed, rebuild the four variations
//   write                write the selected variation as a new clip
//   pick <index>         select a variation in the preview list
//   dropfile <path>      a .mid file dropped on the SOURCE drop zone
//   useclip              read the clip shown in Live's Detail View
//   clearsource          forget the source
//   savepath <path>      write the last result as a Standard MIDI File
//   savemidi             open the save dialog (outlet 3)
//   ascii <0|1>          unused here; accepted so the patcher can send it
// Outlets:
//   0  setstate <json>   to the piano-roll display (jsui)
//   1  set <seed>        to the Seed number box
//   2  setstate <json>   to the variation preview list (jsui)
//   3  bang              to savedialog

autowatch = 0;
inlets = 1;
outlets = 4;
setinletassist(0, 'init, param, tighten, generate, another, write, pick, dropfile, useclip, clearsource, savemidi, savepath');
setoutletassist(0, 'setstate <json> to the piano roll');
setoutletassist(1, 'set <seed> to the Seed number box');
setoutletassist(2, 'setstate <json> to the variation list');
setoutletassist(3, 'bang to savedialog');

var self = this;
var E = DT.engine;

var PARAM_NAMES = [
  'qstrength', 'grid', 'swing', 'groove', 'flams', 'minlen', 'minvel', 'legato', 'gapclose',
  'velmin', 'velmax', 'velcomp', 'humanize', 'scalesnap', 'root', 'mode', 'autokey',
  'amount', 'rhythm', 'melody', 'ghosts', 'accents', 'seed'
];

var settings = E.normalize(E.DEFAULTS);
var ready = false;
var statusText = 'Drop a .mid file or press USE CLIP.';
var statusLevel = 'info';
var source = null;
var sourceClipPath = null;
var lastResult = null;
var previews = [];
var selected = 0;
var MAX_FILE_BYTES = 4 * 1024 * 1024;

var BUTTON_HINTS = {
  tighten: 'TIGHTEN writes an improved copy of the loaded clip into the next empty slot. The original is not changed.',
  generate: 'Generate builds four variations of the loaded clip. Click one, then press Write.',
  another: 'Another picks a new seed and rebuilds the four variations.',
  write: 'Write puts the selected variation into the next empty clip slot.',
  useclip: 'USE CLIP reads the MIDI clip shown in the Detail View (or the selected clip). Live 11 cannot drop Session or Arrangement clips onto this device.',
  clearsource: 'CLEAR forgets the loaded clip.',
  savemidi: 'SAVE MIDI writes the last Tighten or Write result as a .mid file. Max 8 has no file drag-out from live.drop.'
};

function loadbang() { refresh(); }

function init() {
  ready = true;
  refresh();
}

function param(name, value) {
  if (!isParam(name)) {
    post('dooblr-titan: unknown parameter ' + name + '\n');
    return;
  }
  settings[name] = value;
  settings = E.normalize(settings);
  if (source && (name === 'amount' || name === 'rhythm' || name === 'melody' || name === 'ghosts' || name === 'accents' || name === 'seed' || name === 'root' || name === 'mode' || name === 'grid')) {
    rebuildPreviews(false);
  }
  refresh();
}

function tighten() {
  if (!guard()) { return; }
  if (!source) {
    setStatus('info', 'Load a clip first: drop a .mid file or press USE CLIP.');
    refresh();
    return;
  }
  var result;
  try {
    result = E.tighten(source, settings);
  } catch (e) {
    setStatus('warn', 'Could not tighten: ' + errText(e));
    refresh();
    return;
  }
  lastResult = result;
  writeClip(result.liveNotes, result.lengthBeats, result.clipName);
}

function generate() {
  if (!guard()) { return; }
  if (!source) {
    setStatus('info', 'Load a clip first: drop a .mid file or press USE CLIP.');
    refresh();
    return;
  }
  rebuildPreviews(true);
  setStatus('ok', 'Four variations of ' + source.name + '. Click one, then Write.');
  refresh();
}

function another() {
  if (!guard()) { return; }
  if (!source) {
    setStatus('info', 'Load a clip first, then press Generate.');
    refresh();
    return;
  }
  pickNewSeed();
  rebuildPreviews(true);
  setStatus('ok', 'Another four from seed ' + settings.seed + '.');
  refresh();
}

function write() {
  if (!guard()) { return; }
  if (!source) {
    setStatus('info', 'Load a clip first.');
    refresh();
    return;
  }
  if (!previews.length) { rebuildPreviews(true); }
  if (!previews.length) {
    setStatus('warn', 'No variation to write.');
    refresh();
    return;
  }
  var v = previews[selected] || previews[0];
  lastResult = { notes: v.notes, liveNotes: v.liveNotes, lengthBeats: v.lengthBeats, clipName: v.clipName };
  writeClip(v.liveNotes, v.lengthBeats, v.clipName);
}

function pick(index) {
  var i = Math.floor(index);
  if (i < 0 || i >= previews.length) { return; }
  selected = i;
  refresh();
}

function savemidi() {
  if (!lastResult) {
    setStatus('info', 'Tighten or Write a clip first, then SAVE MIDI.');
    refresh();
    return;
  }
  outlet(3, 'bang');
}

function savepath() {
  var path = Array.prototype.slice.call(arguments).join(' ');
  if (!path || !lastResult) { return; }
  if (!/\.(midi?)$/i.test(path)) { path = path + '.mid'; }
  try {
    var bytes = E.midiBytes(lastResult.notes, { name: lastResult.clipName, lengthBeats: lastResult.lengthBeats });
    writeFileBytes(path, bytes);
    setStatus('ok', 'Saved ' + E.baseName(path) + '.mid');
  } catch (e) {
    setStatus('warn', 'Could not save MIDI: ' + errText(e));
  }
  refresh();
}

function ascii() { refresh(); }

function dropfile() {
  var path = Array.prototype.slice.call(arguments).join(' ');
  if (!path) { return; }
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
  var src;
  try {
    src = E.sourceFromMidi(bytes, E.baseName(path));
  } catch (e2) {
    setStatus('warn', file + ': ' + errText(e2));
    sendState();
    return;
  }
  sourceClipPath = null;
  setSource(src, ready);
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
  var src;
  try {
    src = E.loadNotes(shifted, { lengthBeats: range.length, name: String(firstValue(clip.get('name')) || 'Clip') });
  } catch (e2) {
    setStatus('warn', 'Selected clip: ' + errText(e2));
    sendState();
    return;
  }
  clearDrop();
  sourceClipPath = clip.unquotedpath;
  setSource(src, true);
}

function clearsource() {
  if (!source) { return; }
  source = null;
  sourceClipPath = null;
  previews = [];
  selected = 0;
  lastResult = null;
  clearDrop();
  setStatus('info', 'Source cleared.');
  refresh();
}

function setSource(src, applyKey) {
  source = src;
  previews = [];
  selected = 0;
  lastResult = null;
  if (applyKey && settings.autokey) {
    settings.root = src.key.root;
    settings.mode = src.key.mode;
    settings = E.normalize(settings);
    setControl('root', settings.root);
    setControl('mode', settings.mode);
  }
  setStatus('ok', 'Loaded ' + src.name + ': ' + src.noteCount + ' notes, ' + src.key.name + '.');
  rebuildPreviews(false);
  refresh();
}

function rebuildPreviews(force) {
  if (!source) { previews = []; return; }
  if (!force && !previews.length) { return; }
  try {
    previews = E.variations(source, settings);
    if (selected >= previews.length) { selected = 0; }
  } catch (e) {
    post('dooblr-titan: ' + errText(e) + '\n');
    previews = [];
  }
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

function writeFileBytes(path, bytes) {
  var f = new File(path, 'write');
  if (!f.isopen) { throw new Error('the file could not be opened'); }
  try {
    f.eof = 0;
    var i = 0;
    while (i < bytes.length) {
      var n = Math.min(1024, bytes.length - i);
      var chunk = [];
      for (var k = 0; k < n; k++) { chunk.push(bytes[i + k] & 0xff); }
      f.writebytes(chunk);
      i += n;
    }
  } finally {
    f.close();
  }
}

function errText(e) { return (e && e.message) ? e.message : String(e); }

function anything() {
  post('dooblr-titan: unknown message ' + messagename + '\n');
}

function isParam(name) {
  for (var i = 0; i < PARAM_NAMES.length; i++) { if (PARAM_NAMES[i] === name) { return true; } }
  return false;
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
  updateButtons();
  sendState();
  updateSourceInfo();
}

function setControl(varname, value) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (obj) { obj.message('set', value); }
  } catch (e) {
    post('dooblr-titan: could not set ' + varname + ': ' + e + '\n');
  }
}

function clearDrop() {
  try {
    var obj = self.patcher.getnamed('srcdrop');
    if (obj) { obj.message('clear'); }
  } catch (e) {
    post('dooblr-titan: could not clear the drop zone: ' + e + '\n');
  }
}

function updateSourceInfo() {
  var text = 'Drop a .mid file or press USE CLIP.';
  if (source) {
    text = source.name + ' \u00B7 ' + source.key.name + ' \u00B7 ' + source.noteCount + ' notes, ' + (source.lengthBeats / 4) + ' bars';
  }
  try {
    var obj = self.patcher.getnamed('srcinfo');
    if (obj) { obj.message('set', text); }
  } catch (e) {
    post('dooblr-titan: could not update srcinfo: ' + e + '\n');
  }
}

function sendState() {
  var roll = {
    source: source ? source.notes : [],
    result: lastResult ? lastResult.notes : [],
    lengthBeats: source ? source.lengthBeats : 4,
    heading: source ? source.name : 'CLIP',
    status: statusText,
    level: statusLevel,
    key: source ? source.key.name : ''
  };
  outlet(0, 'setstate', JSON.stringify(roll));
  var list = [];
  for (var i = 0; i < previews.length; i++) {
    list.push({ label: previews[i].label, notes: previews[i].noteCount, selected: i === selected });
  }
  outlet(2, 'setstate', JSON.stringify({ items: list, selected: selected, status: statusText, level: statusLevel }));
}

function setActive(varname, on, annotation) {
  try {
    var obj = self.patcher.getnamed(varname);
    if (!obj) { return; }
    obj.message('active', on ? 1 : 0);
    if (annotation) { obj.message('annotation', annotation); }
  } catch (e) {
    post('dooblr-titan: could not update ' + varname + ': ' + e + '\n');
  }
}

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
    post('dooblr-titan: could not update ' + varname + ': ' + e + '\n');
  }
}

function updateButtons() {
  var waiting = ' It is unavailable until the device has finished loading.';
  var need = ' Load a MIDI clip first.';
  var has = ready && !!source;
  setActive('tighten', has, BUTTON_HINTS.tighten + (ready ? (source ? '' : need) : waiting));
  setActive('generate', has, BUTTON_HINTS.generate + (ready ? (source ? '' : need) : waiting));
  setActive('another', has, BUTTON_HINTS.another + (ready ? (source ? '' : need) : waiting));
  setActive('write', has, BUTTON_HINTS.write + (ready ? (source ? '' : need) : waiting));
  setActive('useclip', ready, BUTTON_HINTS.useclip + (ready ? '' : waiting));
  setActive('clearsource', !!source, BUTTON_HINTS.clearsource + (source ? '' : ' It is dimmed because no source is loaded.'));
  setActive('savemidi', !!lastResult, BUTTON_HINTS.savemidi + (lastResult ? '' : ' Tighten or Write first.'));
  setPartlyIgnored('grid', settings.qstrength === 0, 'Grid is the quantize division, including triplets.', TEXT_PRIMARY);
  setPartlyIgnored('swing', settings.qstrength === 0, 'Swing delays offbeats. At 100 the offbeat sits on the last triplet of the pair.', TEXT_PRIMARY);
  setPartlyIgnored('groove', settings.qstrength === 0, 'Groove keeps the clip\'s consistent micro-timing while pulling outliers in.', TEXT_PRIMARY);
  setPartlyIgnored('gapclose', settings.legato === 0, 'Gap Close lengthens notes to fill small same-pitch holes. It needs Legato on.', TEXT_MUTED);
  var velIdle = settings.velcomp === 0 && settings.humanize === 0;
  setPartlyIgnored('velmin', velIdle, 'Vel Min is the bottom of the target velocity range.', TEXT_MUTED);
  setPartlyIgnored('velmax', velIdle, 'Vel Max is the top of the target velocity range.', TEXT_MUTED);
}

function findSourceClip() {
  var paths = ['live_set view detail_clip', 'live_set view highlighted_clip_slot clip'];
  for (var i = 0; i < paths.length; i++) {
    try {
      var c = new LiveAPI(paths[i]);
      if (c && Number(c.id) !== 0) { return c; }
    } catch (e) {
      post('dooblr-titan: ' + paths[i] + ': ' + e + '\n');
    }
  }
  return null;
}

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
    return -1;
  }
}

function addNotes(clip, notes, lengthBeats) {
  var method = 'add_new_notes';
  try {
    clip.call('add_new_notes', { notes: notes });
  } catch (e) {
    post('dooblr-titan: add_new_notes failed: ' + e + '\n');
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
    post('dooblr-titan: set_notes failed: ' + e2 + '\n');
    return null;
  }
  n = countNotes(clip, lengthBeats);
  return (n === notes.length || n < 0) ? method : null;
}

function writeClip(notes, lengthBeats, name) {
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
  var duplicated = false;
  if (sourceClipPath) {
    try {
      var srcClip = new LiveAPI(sourceClipPath);
      if (srcClip && Number(srcClip.id) !== 0) {
        srcClip.call('duplicate_clip_to', slot.api.id);
        duplicated = Number(firstValue(slot.api.get('has_clip'))) === 1;
      }
    } catch (e) {
      post('dooblr-titan: duplicate_clip_to: ' + e + '\n');
    }
  }
  if (!duplicated) {
    slot.api.call('create_clip', lengthBeats);
  }
  var clip = new LiveAPI(slot.api.unquotedpath + ' clip');
  if (!clip || Number(clip.id) === 0) {
    setStatus('warn', 'Live did not create a clip in slot ' + (slot.index + 1) + '.');
    sendState();
    return false;
  }
  if (duplicated) {
    try { clip.call('remove_notes_extended', 0, 128, 0, Number(firstValue(clip.get('length'))) || lengthBeats); } catch (e3) {}
  }
  clip.set('name', name);
  var method = addNotes(clip, notes, lengthBeats);
  if (!method) {
    setStatus('warn', 'Clip in slot ' + (slot.index + 1) + ' has no notes: see the Max window.');
  } else {
    setStatus('ok', 'Wrote ' + name + ' to slot ' + (slot.index + 1) + '.');
  }
  sendState();
  return !!method;
}
