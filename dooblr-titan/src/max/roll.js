// dooblr-titan: piano-roll display (jsui + mgraphics, ES5 only).
// In:  setstate <json>  from dooblr_titan.js
// No outlet used. No hint captions on the graph.

autowatch = 0;
inlets = 1;
outlets = 1;
setinletassist(0, 'setstate <json>');
setoutletassist(0, 'unused');

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var PALETTE = {
  bg: '#16181c', panel: '#1d2026', panelRaised: '#252931', divider: '#2c3038', grid: '#262a31',
  textPrimary: '#e6e8eb', textMuted: '#8a919c', accent: '#5fb3a1', dynamic: '#d9a35b'
};
var STATUS_COLORS = { info: PALETTE.textMuted, ok: PALETTE.accent, warn: PALETTE.dynamic };
var FONT = 'Arial';
var FONT_BOLD = 'Arial Bold';
var HEADER_H = 16;

var model = { source: [], result: [], lengthBeats: 4, heading: 'CLIP', status: '', level: 'info', key: '' };

function hexToRgba(hex, alpha) {
  var h = hex.charAt(0) === '#' ? hex.substring(1) : hex;
  return [parseInt(h.substring(0, 2), 16) / 255, parseInt(h.substring(2, 4), 16) / 255,
    parseInt(h.substring(4, 6), 16) / 255, alpha === undefined ? 1 : alpha];
}

function setColor(hex, alpha) {
  var c = hexToRgba(hex, alpha);
  mgraphics.set_source_rgba(c[0], c[1], c[2], c[3]);
}

function size() {
  var r = box.rect;
  return [r[2] - r[0], r[3] - r[1]];
}

function setstate(json) {
  try { model = JSON.parse(json); } catch (e) { post('dooblr-titan roll: bad state: ' + e + '\n'); }
  mgraphics.redraw();
}

function paint() {
  var s = size(), w = s[0], h = s[1];
  setColor(PALETTE.bg);
  mgraphics.rectangle(0, 0, w, h);
  mgraphics.fill();

  setColor(PALETTE.panel);
  mgraphics.rectangle(0, 0, w, HEADER_H);
  mgraphics.fill();

  mgraphics.select_font_face(FONT_BOLD);
  mgraphics.set_font_size(9);
  setColor(PALETTE.textMuted);
  mgraphics.move_to(4, 11);
  mgraphics.show_text(String(model.heading || 'CLIP').toUpperCase());

  var notes = (model.result && model.result.length) ? model.result : (model.source || []);
  var len = model.lengthBeats > 0 ? model.lengthBeats : 4;
  var plotY = HEADER_H;
  var plotH = h - HEADER_H;
  if (plotH < 4) { return; }

  var lo = 127, hi = 0, i;
  for (i = 0; i < notes.length; i++) {
    if (notes[i].pitch < lo) { lo = notes[i].pitch; }
    if (notes[i].pitch > hi) { hi = notes[i].pitch; }
  }
  if (lo > hi) { lo = 48; hi = 72; }
  if (hi - lo < 12) { lo = Math.max(0, lo - 6); hi = Math.min(127, hi + 6); }

  var bars = Math.max(1, Math.round(len / 4));
  for (i = 0; i <= bars; i++) {
    var x = (i * 4 / len) * w;
    setColor(PALETTE.grid, i % 4 === 0 ? 1 : 0.6);
    mgraphics.move_to(x, plotY);
    mgraphics.line_to(x, h);
    mgraphics.set_line_width(1);
    mgraphics.stroke();
  }

  function drawNotes(list, hex, alpha) {
    var k;
    for (k = 0; k < list.length; k++) {
      var n = list[k];
      var nx = (n.start / len) * w;
      var nw = Math.max(2, (n.duration / len) * w);
      var ny = plotY + (1 - (n.pitch - lo) / (hi - lo)) * (plotH - 3);
      setColor(hex, alpha);
      mgraphics.rectangle(nx, ny, nw, 3);
      mgraphics.fill();
    }
  }

  if (model.source && model.source.length && model.result && model.result.length) {
    drawNotes(model.source, PALETTE.textMuted, 0.35);
    drawNotes(model.result, PALETTE.accent, 0.95);
  } else {
    drawNotes(notes, PALETTE.accent, 0.95);
  }
}

function onresize() { mgraphics.redraw(); }
