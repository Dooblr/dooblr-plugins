// Validate the device patcher.
//   node scripts/validate-patcher.js            validates src/patcher/dooblr-chords.maxpat
//   node scripts/validate-patcher.js --built    also validates the patcher inside the built .amxd
// Checks: unique box ids (per patcher), unique varnames and parameter longnames
// (across the device including the Advanced subpatcher), patchlines, inlet/outlet
// counts, presentation boxes fit their window, and the patcher agrees with main.js
// on message and parameter names.
'use strict';
const fs = require('fs');
const path = require('path');
const amxd = require('./amxd');

const ROOT = path.join(__dirname, '..');
const SRC = path.join(ROOT, 'src', 'patcher', 'dooblr-chords.maxpat');
const BUILT = path.join(ROOT, 'device', 'dooblr-chords', 'dooblr-chords.amxd');
const DEVICE_HEIGHT = 169;
const ADVANCED_WIDTH = 560;
const ADVANCED_HEIGHT = 300;

const IO = {
  'live.dial': [1, 2], 'live.numbox': [1, 2], 'live.menu': [1, 3], 'live.tab': [1, 3], 'live.text': [1, 2], 'live.drop': [1, 2],
  message: [2, 1], comment: [1, 0], panel: [1, 0], jsui: [1, 1],
  'newobj:live.thisdevice': [1, 3], 'newobj:midiin': [1, 1], 'newobj:midiout': [1, 0], 'newobj:prepend': [1, 1],
  'newobj:js': [1, 4], 'newobj:route': [2, -1], 'newobj:thispatcher': [1, 1], 'newobj:send': [1, 0], 'newobj:receive': [1, 1],
  'newobj:outlet': [1, 0]
};

function ioKey(b) {
  if (b.maxclass !== 'newobj') { return b.maxclass; }
  return 'newobj:' + String(b.text || '').split(/\s+/)[0];
}

function allPatchers(root, label) {
  const out = [];
  function walk(p, name, width, height, isDevice) {
    out.push({ p, name, width, height, isDevice });
    (p.boxes || []).forEach((wrap) => {
      const b = wrap.box;
      if (b && b.patcher && Array.isArray(b.patcher.boxes)) {
        const vn = b.varname || (b.text || 'sub');
        const isAdv = vn === 'advanced' || /^p advanced/.test(b.text || '');
        const w = isAdv ? ADVANCED_WIDTH : width;
        const h = isAdv ? ADVANCED_HEIGHT : height;
        walk(b.patcher, name + '/' + vn, w, h, false);
      }
    });
  }
  const p = root.patcher;
  walk(p, label, p.devicewidth || 0, DEVICE_HEIGHT, true);
  return out;
}

function validate(patcher, label, mainSource) {
  const errors = [];
  const err = (m) => errors.push(label + ': ' + m);
  if (!patcher.patcher || !Array.isArray(patcher.patcher.boxes) || !Array.isArray(patcher.patcher.lines)) {
    err('missing patcher.boxes or patcher.lines');
    return errors;
  }

  const longnames = {};
  const varnames = {};
  const parkX = parseParkX(mainSource);
  const panels = parsePanels(mainSource);
  const panelRects = parseRectMap(mainSource, 'PANEL_RECTS');
  const coveredRects = parseRectMap(mainSource, 'COVERED_RECTS');
  const badgeRect = parseNamedRect(mainSource, 'BADGE_RECT');
  const hasPanels = Object.keys(panels).length > 0;

  const trees = allPatchers(patcher, label);
  let sawAdvanced = false;

  trees.forEach((tree) => {
    const p = tree.p;
    const prefix = tree.name;
    const width = tree.width;
    const height = tree.height;
    const byId = {};

    if (p.openinpresentation !== 1 && (tree.isDevice || /\/advanced$/.test(tree.name))) {
      err(prefix + ': openinpresentation should be 1');
    }
    if (/\/advanced$/.test(tree.name)) { sawAdvanced = true; }

    p.boxes.forEach((wrap, i) => {
      const b = wrap.box;
      if (!b || !b.id) { err(prefix + ': box #' + i + ' has no id'); return; }
      if (byId[b.id]) { err(prefix + ': duplicate box id ' + b.id); }
      byId[b.id] = b;
      if (typeof b.numinlets !== 'number' || typeof b.numoutlets !== 'number') { err(prefix + ': ' + b.id + ' lacks numinlets/numoutlets'); }
      if (b.numoutlets > 0 && (!Array.isArray(b.outlettype) || b.outlettype.length !== b.numoutlets)) {
        err(prefix + ': ' + b.id + ' outlettype length does not match numoutlets');
      }
      const io = IO[ioKey(b)];
      if (io) {
        if (b.numinlets !== io[0]) { err(prefix + ': ' + b.id + ' (' + ioKey(b) + ') should have ' + io[0] + ' inlets, has ' + b.numinlets); }
        const outs = io[1] === -1 ? String(b.text).split(/\s+/).length : io[1];
        if (b.numoutlets !== outs) { err(prefix + ': ' + b.id + ' (' + ioKey(b) + ') should have ' + outs + ' outlets, has ' + b.numoutlets); }
      }
      if (b.presentation) {
        const r = b.presentation_rect;
        if (!Array.isArray(r) || r.length !== 4) { err(prefix + ': ' + b.id + ' presentation_rect missing'); }
        else if (hasPanels && r[0] >= parkX) {
          // parked off-screen; membership is checked after PANELS are parsed
        } else if (r[0] < 0 || r[1] < 0 || r[0] + r[2] > width || r[1] + r[3] > height) {
          err(prefix + ': ' + b.id + ' presentation_rect ' + JSON.stringify(r) + ' is outside ' + width + 'x' + height);
        }
      }
      if (b.varname) {
        if (varnames[b.varname]) { err(prefix + ': duplicate varname ' + b.varname); }
        varnames[b.varname] = true;
      }
      const v = b.saved_attribute_attributes && b.saved_attribute_attributes.valueof;
      if (v) {
        if (!v.parameter_longname) { err(prefix + ': ' + b.id + ' parameter has no longname'); }
        if (longnames[v.parameter_longname]) { err(prefix + ': duplicate parameter_longname ' + v.parameter_longname); }
        longnames[v.parameter_longname] = true;
        if (v.parameter_type === 2 && Array.isArray(v.parameter_enum) && v.parameter_mmax !== undefined &&
            v.parameter_mmax !== v.parameter_enum.length - 1) { err(prefix + ': ' + b.id + ' enum length and parameter_mmax disagree'); }
        if (b.parameter_enable !== 1) { err(prefix + ': ' + b.id + ' has parameter attributes but parameter_enable is not 1'); }
        if (!tree.isDevice) {
          err(prefix + ': ' + b.id + ' (' + (b.varname || '') + ') is a saved parameter in a subpatcher; saved live.* parameters must live in the parent patcher so older Sets keep a compatible undo history');
        }
      }
    });

    const shown = p.boxes.map((w) => w.box).filter((b) => b && b.presentation && b.maxclass !== 'panel' &&
      Array.isArray(b.presentation_rect) && b.presentation_rect.length === 4 &&
      !(hasPanels && b.presentation_rect[0] >= parkX));
    const overlaps = (a, b) => {
      const r = a.presentation_rect, q = b.presentation_rect;
      return r[0] < q[0] + q[2] && q[0] < r[0] + r[2] && r[1] < q[1] + q[3] && q[1] < r[1] + r[3];
    };
    for (let i = 0; i < shown.length; i++) {
      for (let j = i + 1; j < shown.length; j++) {
        if (overlaps(shown[i], shown[j])) {
          err(prefix + ': ' + shown[i].id + ' and ' + shown[j].id + ' overlap in presentation');
        }
      }
    }

    (p.lines || []).forEach((wrap, i) => {
      const l = wrap.patchline;
      if (!l || !Array.isArray(l.source) || !Array.isArray(l.destination)) { err(prefix + ': line #' + i + ' malformed'); return; }
      const s = byId[l.source[0]], d = byId[l.destination[0]];
      if (!s) { err(prefix + ': line #' + i + ' source ' + l.source[0] + ' does not exist'); }
      if (!d) { err(prefix + ': line #' + i + ' destination ' + l.destination[0] + ' does not exist'); }
      if (s && !(l.source[1] >= 0 && l.source[1] < s.numoutlets)) { err(prefix + ': line #' + i + ' uses outlet ' + l.source[1] + ' of ' + s.id + ' (' + s.numoutlets + ' outlets)'); }
      if (d && !(l.destination[1] >= 0 && l.destination[1] < d.numinlets)) { err(prefix + ': line #' + i + ' uses inlet ' + l.destination[1] + ' of ' + d.id + ' (' + d.numinlets + ' inlets)'); }
    });

    if (mainSource) {
      const fnDefined = (name) => new RegExp('function\\s+' + name + '\\s*\\(').test(mainSource);
      const listMatch = /var PARAM_NAMES = \[([\s\S]*?)\];/.exec(mainSource);
      const paramNames = listMatch ? listMatch[1].match(/'([a-z]+)'/g).map((s) => s.replace(/'/g, '')) : [];
      p.boxes.forEach((wrap) => {
        const b = wrap.box;
        const m = /^prepend param (\S+)$/.exec(b.text || '');
        if (m && paramNames.indexOf(m[1]) < 0) { err(prefix + ': ' + b.id + ': main.js does not handle parameter ' + m[1]); }
        if (b.maxclass === 'message') {
          const fn = String(b.text).split(/\s+/)[0];
          if (fn && !fnDefined(fn) && !/^[0-9]/.test(fn)) {
            // Only flag messages that are wired toward js / outlet (all of ours are).
            if (!fnDefined(fn)) { err(prefix + ': ' + b.id + ': message "' + fn + '" has no function in main.js'); }
          }
        }
      });
    }
  });

  if (!sawAdvanced) { err('missing [p advanced] subpatcher'); }

  // Legacy overlay-parking checks, only when main.js still defines PANELS.
  if (hasPanels) {
    const device = trees[0];
    const byId = {};
    device.p.boxes.forEach((w) => { if (w.box && w.box.id) { byId[w.box.id] = w.box; } });
    const memberOf = {};
    Object.keys(panels).forEach((name) => {
      (panels[name] || []).forEach((vn) => {
        if (memberOf[vn] && memberOf[vn] !== name) { err('varname ' + vn + ' is in more than one panel'); }
        memberOf[vn] = name;
        const b = Object.keys(byId).map((k) => byId[k]).filter((x) => x.varname === vn)[0];
        if (!b) { err('panel ' + name + ' member ' + vn + ' has no box'); }
        else if (!b.presentation) { err(b.id + ' (' + vn + ') is a panel member but is not in presentation'); }
        else if (!Array.isArray(b.presentation_rect) || b.presentation_rect[0] < parkX) {
          err(b.id + ' (' + vn + ') is a panel member but is not parked off-screen');
        }
        if (!panelRects[vn]) { err('PANEL_RECTS is missing ' + vn); }
      });
    });
    Object.keys(panelRects).forEach((vn) => {
      if (!memberOf[vn]) { err('PANEL_RECTS.' + vn + ' is not a panel member'); }
      const r = panelRects[vn];
      if (!insideDevice(r, device.width)) {
        err('PANEL_RECTS.' + vn + ' ' + JSON.stringify(r) + ' is outside ' + device.width + 'x' + DEVICE_HEIGHT);
      }
    });
    Object.keys(coveredRects).forEach((vn) => {
      const b = Object.keys(byId).map((k) => byId[k]).filter((x) => x.varname === vn)[0];
      if (!b) { err('COVERED_RECTS.' + vn + ' has no box'); }
      else if (memberOf[vn]) { err(vn + ' cannot be both a panel member and a covered main-view control'); }
      else if (!rectsEqual(b.presentation_rect, coveredRects[vn])) {
        err(b.id + ' (' + vn + ') presentation_rect ' + JSON.stringify(b.presentation_rect) +
          ' does not match COVERED_RECTS');
      }
      if (!insideDevice(coveredRects[vn], device.width)) {
        err('COVERED_RECTS.' + vn + ' ' + JSON.stringify(coveredRects[vn]) + ' is outside ' + device.width + 'x' + DEVICE_HEIGHT);
      }
    });
    if (badgeRect && !insideDevice(badgeRect, device.width)) {
      err('BADGE_RECT ' + JSON.stringify(badgeRect) + ' is outside ' + device.width + 'x' + DEVICE_HEIGHT);
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
  const adv = p.boxes.map((w) => w.box).filter((b) => b.varname === 'advanced')[0];
  const nAdv = adv && adv.patcher ? adv.patcher.boxes.length : 0;
  console.log('patcher ok: ' + p.boxes.length + ' boxes, ' + p.lines.length + ' lines' +
    (nAdv ? ', advanced ' + nAdv + ' boxes' : '') +
    (process.argv.indexOf('--built') >= 0 ? ' (source and built amxd)' : ''));
}

function parseParkX(mainSource) {
  const m = /var PARK_X = (\d+);/.exec(mainSource || '');
  return m ? Number(m[1]) : 2000;
}

function parseNamedRect(mainSource, name) {
  const m = new RegExp('var ' + name + ' = \\[([^\\]]+)\\];').exec(mainSource || '');
  if (!m) { return null; }
  return m[1].split(',').map((n) => Number(n.trim()));
}

function parseRectMap(src, name) {
  const map = {};
  if (!src) { return map; }
  const block = new RegExp('var ' + name + ' = \\{([\\s\\S]*?)\\};').exec(src);
  if (!block) { return map; }
  const re = /([A-Za-z0-9_]+):\s*\[([^\]]+)\]/g;
  let m;
  while ((m = re.exec(block[1]))) {
    map[m[1]] = m[2].split(',').map((n) => Number(n.trim()));
  }
  return map;
}

function insideDevice(r, width) {
  return Array.isArray(r) && r.length === 4 && r[0] >= 0 && r[1] >= 0 &&
    r[0] + r[2] <= width && r[1] + r[3] <= DEVICE_HEIGHT;
}

function rectsEqual(a, b) {
  return Array.isArray(a) && Array.isArray(b) && a.length === 4 && b.length === 4 &&
    a[0] === b[0] && a[1] === b[1] && a[2] === b[2] && a[3] === b[3];
}

function parsePanels(mainSource) {
  const panels = {};
  if (!mainSource) { return panels; }
  const block = /var PANELS = \{([\s\S]*?)\};/.exec(mainSource);
  if (!block) { return panels; }
  const re = /([a-z]+):\s*\[([\s\S]*?)\]/g;
  let m;
  while ((m = re.exec(block[1]))) {
    const names = m[2].match(/'([^']+)'/g);
    panels[m[1]] = names ? names.map((n) => n.slice(1, -1)) : [];
  }
  return panels;
}

module.exports = { validate };

if (require.main === module) { main(); }
