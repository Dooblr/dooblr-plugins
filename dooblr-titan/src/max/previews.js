// dooblr-titan: variation preview list (jsui + mgraphics, ES5 only).
// In:  setstate <json>
// Out: pick <index> when a row is clicked

autowatch = 0;
inlets = 1;
outlets = 1;
setinletassist(0, 'setstate <json>');
setoutletassist(0, 'pick <index>');

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var PALETTE = {
  bg: '#16181c', panel: '#1d2026', panelRaised: '#252931', divider: '#2c3038',
  textPrimary: '#e6e8eb', textMuted: '#8a919c', accent: '#5fb3a1', dynamic: '#d9a35b'
};
var FONT = 'Arial';
var FONT_BOLD = 'Arial Bold';
var HEADER_H = 14;
var ROW_GAP = 2;

var model = { items: [], selected: 0, status: '', level: 'info' };
var hits = [];

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
  try { model = JSON.parse(json); } catch (e) { post('dooblr-titan previews: bad state: ' + e + '\n'); }
  mgraphics.redraw();
}

function paint() {
  var s = size(), w = s[0], h = s[1];
  hits = [];
  setColor(PALETTE.bg);
  mgraphics.rectangle(0, 0, w, h);
  mgraphics.fill();

  setColor(PALETTE.panel);
  mgraphics.rectangle(0, 0, w, HEADER_H);
  mgraphics.fill();
  mgraphics.select_font_face(FONT_BOLD);
  mgraphics.set_font_size(9);
  setColor(PALETTE.textMuted);
  mgraphics.move_to(4, 10);
  mgraphics.show_text('VARIATIONS');

  var items = model.items || [];
  var body = h - HEADER_H - 2;
  var rows = Math.max(4, items.length || 4);
  var rh = Math.max(16, Math.floor(body / rows) - ROW_GAP);
  var i;
  for (i = 0; i < rows; i++) {
    var y = HEADER_H + 2 + i * (rh + ROW_GAP);
    var item = items[i];
    var sel = item && (item.selected || i === model.selected);
    setColor(sel ? PALETTE.panelRaised : PALETTE.panel);
    mgraphics.rectangle(2, y, w - 4, rh);
    mgraphics.fill();
    if (sel) {
      setColor(PALETTE.accent);
      mgraphics.set_line_width(1);
      mgraphics.rectangle(2, y, w - 4, rh);
      mgraphics.stroke();
    }
    mgraphics.select_font_face(FONT_BOLD);
    mgraphics.set_font_size(10);
    setColor(item ? PALETTE.textPrimary : PALETTE.textMuted);
    mgraphics.move_to(8, y + rh * 0.65);
    mgraphics.show_text(item ? item.label : '—');
    if (item) {
      mgraphics.select_font_face(FONT);
      mgraphics.set_font_size(9);
      setColor(PALETTE.textMuted);
      var count = String(item.notes) + ' n';
      mgraphics.move_to(w - 28, y + rh * 0.65);
      mgraphics.show_text(count);
    }
    hits.push({ x: 2, y: y, w: w - 4, h: rh, index: i });
  }
}

function onclick(x, y) {
  var i;
  for (i = 0; i < hits.length; i++) {
    var h = hits[i];
    if (x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) {
      if (model.items && model.items[h.index]) { outlet(0, 'pick', h.index); }
      return;
    }
  }
}

function onresize() { mgraphics.redraw(); }
