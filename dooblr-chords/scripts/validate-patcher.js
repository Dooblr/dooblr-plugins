// Validate the device patcher.
//   node scripts/validate-patcher.js            validates src/patcher/dooblr-chords.maxpat
//   node scripts/validate-patcher.js --built    also validates the patcher inside the built .amxd
// Checks: unique box ids, patchlines reference existing boxes with valid
// inlet/outlet indices, known objects have the expected inlet/outlet counts,
// presentation boxes fit the device, parameter names and varnames are unique,
// and the patcher and src/max/main.js agree on message and parameter names.
'use strict';
const fs = require('fs');
const path = require('path');
const amxd = require('./amxd');

const ROOT = path.join(__dirname, '..');
const SRC = path.join(ROOT, 'src', 'patcher', 'dooblr-chords.maxpat');
const BUILT = path.join(ROOT, 'device', 'dooblr-chords', 'dooblr-chords.amxd');
const DEVICE_HEIGHT = 169;

// [inlets, outlets] for objects whose counts are fixed in Max 8.5.
const IO = {
  'live.dial': [1, 2], 'live.numbox': [1, 2], 'live.menu': [1, 3], 'live.tab': [1, 3], 'live.text': [1, 2], 'live.drop': [1, 2],
  message: [2, 1], comment: [1, 0], panel: [1, 0], jsui: [1, 1],
  'newobj:live.thisdevice': [1, 3], 'newobj:midiin': [1, 1], 'newobj:midiout': [1, 0], 'newobj:prepend': [1, 1],
  'newobj:js': [1, 2], 'newobj:route': [2, -1]
};

function ioKey(b) {
  if (b.maxclass !== 'newobj') { return b.maxclass; }
  return 'newobj:' + String(b.text || '').split(/\s+/)[0];
}

function validate(patcher, label, mainSource) {
  const errors = [];
  const err = (m) => errors.push(label + ': ' + m);
  const p = patcher.patcher;
  if (!p || !Array.isArray(p.boxes) || !Array.isArray(p.lines)) { err('missing patcher.boxes or patcher.lines'); return errors; }

  const byId = {};
  const longnames = {};
  const varnames = {};
  const width = p.devicewidth || 0;

  p.boxes.forEach((wrap, i) => {
    const b = wrap.box;
    if (!b || !b.id) { err('box #' + i + ' has no id'); return; }
    if (byId[b.id]) { err('duplicate box id ' + b.id); }
    byId[b.id] = b;
    if (typeof b.numinlets !== 'number' || typeof b.numoutlets !== 'number') { err(b.id + ' lacks numinlets/numoutlets'); }
    if (b.numoutlets > 0 && (!Array.isArray(b.outlettype) || b.outlettype.length !== b.numoutlets)) {
      err(b.id + ' outlettype length does not match numoutlets');
    }
    const io = IO[ioKey(b)];
    if (io) {
      if (b.numinlets !== io[0]) { err(b.id + ' (' + ioKey(b) + ') should have ' + io[0] + ' inlets, has ' + b.numinlets); }
      const outs = io[1] === -1 ? String(b.text).split(/\s+/).length : io[1]; // route: one per argument + reject
      if (b.numoutlets !== outs) { err(b.id + ' (' + ioKey(b) + ') should have ' + outs + ' outlets, has ' + b.numoutlets); }
    }
    if (b.presentation) {
      const r = b.presentation_rect;
      if (!Array.isArray(r) || r.length !== 4) { err(b.id + ' presentation_rect missing'); }
      else if (r[0] < 0 || r[1] < 0 || r[0] + r[2] > width || r[1] + r[3] > DEVICE_HEIGHT) {
        err(b.id + ' presentation_rect ' + JSON.stringify(r) + ' is outside ' + width + 'x' + DEVICE_HEIGHT);
      }
    }
    if (b.varname) {
      if (varnames[b.varname]) { err('duplicate varname ' + b.varname); }
      varnames[b.varname] = true;
    }
    const v = b.saved_attribute_attributes && b.saved_attribute_attributes.valueof;
    if (v) {
      if (!v.parameter_longname) { err(b.id + ' parameter has no longname'); }
      if (longnames[v.parameter_longname]) { err('duplicate parameter_longname ' + v.parameter_longname); }
      longnames[v.parameter_longname] = true;
      if (v.parameter_type === 2 && Array.isArray(v.parameter_enum) && v.parameter_mmax !== undefined &&
          v.parameter_mmax !== v.parameter_enum.length - 1) { err(b.id + ' enum length and parameter_mmax disagree'); }
      if (b.parameter_enable !== 1) { err(b.id + ' has parameter attributes but parameter_enable is not 1'); }
    }
  });

  p.lines.forEach((wrap, i) => {
    const l = wrap.patchline;
    if (!l || !Array.isArray(l.source) || !Array.isArray(l.destination)) { err('line #' + i + ' malformed'); return; }
    const s = byId[l.source[0]], d = byId[l.destination[0]];
    if (!s) { err('line #' + i + ' source ' + l.source[0] + ' does not exist'); }
    if (!d) { err('line #' + i + ' destination ' + l.destination[0] + ' does not exist'); }
    if (s && !(l.source[1] >= 0 && l.source[1] < s.numoutlets)) { err('line #' + i + ' uses outlet ' + l.source[1] + ' of ' + s.id + ' (' + s.numoutlets + ' outlets)'); }
    if (d && !(l.destination[1] >= 0 && l.destination[1] < d.numinlets)) { err('line #' + i + ' uses inlet ' + l.destination[1] + ' of ' + d.id + ' (' + d.numinlets + ' inlets)'); }
  });

  if (p.openinpresentation !== 1) { err('openinpresentation should be 1'); }

  // Cross-check with the js controller.
  if (mainSource) {
    const jsBox = Object.keys(byId).map((k) => byId[k]).filter((b) => /^js\s/.test(b.text || ''))[0];
    if (!jsBox) { err('no [js] object'); }
    const fnDefined = (name) => new RegExp('function\\s+' + name + '\\s*\\(').test(mainSource);
    const listMatch = /var PARAM_NAMES = \[([\s\S]*?)\];/.exec(mainSource);
    const paramNames = listMatch ? listMatch[1].match(/'([a-z]+)'/g).map((s) => s.replace(/'/g, '')) : [];
    p.boxes.forEach((wrap) => {
      const b = wrap.box;
      const m = /^prepend param (\S+)$/.exec(b.text || '');
      if (m && paramNames.indexOf(m[1]) < 0) { err(b.id + ': main.js does not handle parameter ' + m[1]); }
    });
    if (jsBox) {
      p.lines.forEach((wrap) => {
        const l = wrap.patchline;
        const s = byId[l.source[0]];
        if (l.destination[0] === jsBox.id && s && s.maxclass === 'message') {
          const fn = String(s.text).split(/\s+/)[0];
          if (!fnDefined(fn)) { err(s.id + ': message "' + fn + '" has no function in main.js'); }
        }
      });
    }
  }
  return errors;
}

function main() {
  const mainSource = fs.readFileSync(path.join(ROOT, 'src', 'max', 'main.js'), 'utf8');
  const src = JSON.parse(fs.readFileSync(SRC, 'utf8'));
  let errors = validate(src, 'src', mainSource);
  if (process.argv.indexOf('--built') >= 0) {
    const built = amxd.read(fs.readFileSync(BUILT));
    if (built.type !== 'mmmm') { errors.push('built: device type is ' + built.type + ', expected mmmm (MIDI effect)'); }
    if (JSON.stringify(built.patcher) !== JSON.stringify(src)) { errors.push('built: amxd patcher differs from the source; run npm run build'); }
    errors = errors.concat(validate(built.patcher, 'built', mainSource));
  }
  const p = src.patcher;
  if (errors.length) {
    errors.forEach((e) => console.error('x ' + e));
    console.error(errors.length + ' problem(s)');
    process.exit(1);
  }
  console.log('patcher ok: ' + p.boxes.length + ' boxes, ' + p.lines.length + ' lines' + (process.argv.indexOf('--built') >= 0 ? ' (source and built amxd)' : ''));
}

if (require.main === module) { main(); }
module.exports = { validate };
