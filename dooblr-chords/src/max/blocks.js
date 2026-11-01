// dooblr-chords: chord block display (jsui + mgraphics, ES5 only).
// In:  setstate <json>  from dooblr_chords.js
// Out: lock <index>     when a chord block is clicked

autowatch = 0;
inlets = 1;
outlets = 1;
setinletassist(0, 'setstate <json>');
setoutletassist(0, 'lock <index> when a chord block is clicked');

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var PALETTE = {
  bg: '#16181c', panel: '#1d2026', panelRaised: '#252931', divider: '#2c3038',
  textPrimary: '#e6e8eb', textMuted: '#8a919c', accent: '#5fb3a1', dynamic: '#d9a35b'
};
var BANDS = ['#6fa8dc', '#5fb3a1', '#a3c46c', '#d9a35b', '#d9776b', '#b08fd0'];
var STATUS_COLORS = { info: PALETTE.textMuted, ok: PALETTE.accent, warn: PALETTE.dynamic };
var FUNCTION_LABELS = { T: 'Tonic', PD: 'Pre-dominant', D: 'Dominant' };

var FONT = 'Arial';
var FONT_BOLD = 'Arial Bold';
var HEADER_H = 16;
var SOURCE_H = 13;   // source strip under the header when a source is loaded
var GAP = 4;
var MAX_PER_ROW = 8;

var model = { chords: [], heading: 'PROGRESSION', key: '', region: '', status: '', level: 'info', locked: 0, source: null };
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

function textWidth(str) {
  return mgraphics.text_measure(str)[0];
}

function drawText(str, x, y, font, pt, hex, alpha) {
  mgraphics.select_font_face(font);
  mgraphics.set_font_size(pt);
  setColor(hex, alpha);
  mgraphics.move_to(x, y);
  mgraphics.show_text(str);
}

// Shrinks the font until the text fits, then returns the size used.
function fitSize(str, font, pt, maxWidth, minPt) {
  mgraphics.select_font_face(font);
  var s = pt;
  mgraphics.set_font_size(s);
  while (s > minPt && textWidth(str) > maxWidth) {
    s -= 0.5;
    mgraphics.set_font_size(s);
  }
  return s;
}

function ellipsize(str, maxWidth) {
  if (textWidth(str) <= maxWidth) { return str; }
  var s = str;
  while (s.length > 1 && textWidth(s + '...') > maxWidth) { s = s.substring(0, s.length - 1); }
  return s + '...';
}

function drawPadlock(x, y, hex) {
  setColor(hex);
  mgraphics.set_line_width(1.2);
  mgraphics.arc(x + 3.5, y + 3.5, 2.3, Math.PI, 2 * Math.PI);
  mgraphics.stroke();
  mgraphics.rectangle(x + 0.5, y + 3.5, 6, 5);
  mgraphics.fill();
}

function paint() {
  var sz = size();
  var w = sz[0], h = sz[1];
  hits = [];

  setColor(PALETTE.bg);
  mgraphics.rectangle(0, 0, w, h);
  mgraphics.fill();

  // Header line: section heading, the B-section region (if any), status
  // right aligned. The key itself is on the Root and Mode menus.
  var heading = model.heading || 'PROGRESSION';
  drawText(heading, 2, 11, FONT_BOLD, 9, model.source ? PALETTE.accent : PALETTE.textMuted);
  mgraphics.select_font_face(FONT_BOLD);
  mgraphics.set_font_size(9);
  var statusLeft = 2 + textWidth(heading) + 8;
  if (model.region) {
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(9);
    var region = ellipsize(model.region, Math.max(40, (w - statusLeft) * 0.4));
    drawText(region, statusLeft, 11, FONT, 9, PALETTE.textPrimary);
    statusLeft += textWidth(region) + 12;
  }
  if (model.status) {
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(9);
    var st = ellipsize(model.status, w - statusLeft - 2);
    var sw = textWidth(st);
    drawText(st, w - 2 - sw, 11, FONT, 9, STATUS_COLORS[model.level] || PALETTE.textMuted);
  }

  var chords = model.chords || [];
  var n = chords.length;
  var top = HEADER_H;
  if (model.source) {
    drawSource(model.source, 2, top + 9, w - 4);
    top += SOURCE_H;
  }
  var areaH = h - top - 1;
  if (n === 0) {
    drawText('No progression yet.', 4, top + areaH / 2 + 4, FONT, 10, PALETTE.textMuted);
    return;
  }

  var rows = n > MAX_PER_ROW ? 2 : 1;
  var perRow = Math.ceil(n / rows);
  var bw = (w - GAP * (perRow - 1)) / perRow;
  var bh = (areaH - GAP * (rows - 1)) / rows;

  for (var i = 0; i < n; i++) {
    var row = Math.floor(i / perRow), col = i % perRow;
    var x = col * (bw + GAP), y = top + row * (bh + GAP);
    drawBlock(chords[i], i, x, y, bw, bh);
    hits.push([x, y, x + bw, y + bh]);
  }
}

function drawBlock(ch, i, x, y, bw, bh) {
  var band = BANDS[i % BANDS.length];

  setColor(ch.locked ? PALETTE.panelRaised : PALETTE.panel);
  mgraphics.rectangle_rounded(x, y, bw, bh, 4, 4);
  mgraphics.fill();

  // Band stripe along the top edge.
  setColor(band, ch.locked ? 1 : 0.85);
  mgraphics.rectangle(x + 1, y, bw - 2, 3);
  mgraphics.fill();

  // A chord the variation or B section changed from the source gets a dot.
  if (ch.changed) {
    setColor(PALETTE.dynamic);
    mgraphics.ellipse(x + 4, y + 6, 4, 4);
    mgraphics.fill();
  }

  if (ch.locked) {
    setColor(band);
    mgraphics.set_line_width(1.5);
    mgraphics.rectangle_rounded(x + 0.75, y + 0.75, bw - 1.5, bh - 1.5, 4, 4);
    mgraphics.stroke();
    drawPadlock(x + bw - 11, y + 6, band);
  }

  var name = ch.name || '';
  var roman = ch.roman || '';
  var pad = ch.locked ? 14 : 6;
  // Tall blocks: 14 pt name over a 9.5 pt numeral. Short blocks (two rows,
  // or a source strip above) use smaller type; the numeral is dropped only
  // when there is no room for it at all.
  var tall = bh >= 44;
  var namePt = fitSize(name, FONT_BOLD, tall ? 14 : 11, bw - 2 * pad, 7);
  var nameW = textWidth(name);
  var romanPt = tall ? 9.5 : 8;
  var showRoman = bh >= 21;
  var nameY = showRoman ? y + 3 + (bh - 3) * (tall ? 0.5 : 0.48) + namePt * 0.2 : y + 3 + (bh - 3) / 2 + namePt * 0.35;
  drawText(name, x + (bw - nameW) / 2, nameY, FONT_BOLD, namePt, PALETTE.textPrimary);
  if (showRoman) {
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(romanPt);
    var r = ellipsize(roman, bw - 8);
    drawText(r, x + (bw - textWidth(r)) / 2, y + bh - (tall ? 8 : 3), FONT, romanPt, PALETTE.textMuted);
  }
}

// "SOURCE   Am9   Fmaj9   Dsus4   Em7" (name and key are in the SOURCE
// column's info line, so the strip shows only the chords to compare with).
function drawSource(src, x, y, maxW) {
  drawText('SOURCE', x, y, FONT_BOLD, 8, PALETTE.textMuted);
  mgraphics.select_font_face(FONT_BOLD);
  mgraphics.set_font_size(8);
  var cx = x + textWidth('SOURCE') + 8;
  mgraphics.select_font_face(FONT);
  mgraphics.set_font_size(9);
  var chords = (src.chords || []).join('   ');
  drawText(ellipsize(chords, x + maxW - cx), cx, y, FONT, 9, PALETTE.textPrimary, 0.8);
}

function setstate(json) {
  try {
    model = JSON.parse(json);
  } catch (e) {
    post('dooblr-chords display: bad state: ' + e + '\n');
  }
  updateHint();
  mgraphics.redraw();
}

function updateHint() {
  var n = model.locked || 0;
  var text = n
    ? 'Click a chord to lock or unlock it. Locked chords stay the same when you press Reroll. ' + n + ' locked.'
    : 'Click a chord to lock it. Locked chords stay the same when you press Reroll.';
  // The header may cut a long status short; the hover text has all of it.
  if (model.status) { text += ' Status: ' + model.status; }
  try { box.message('annotation', text); } catch (e) { /* not fatal */ }
}

function onclick(x, y) {
  for (var i = 0; i < hits.length; i++) {
    var r = hits[i];
    if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) {
      outlet(0, 'lock', i);
      return;
    }
  }
}
onclick.local = 1;

function ondblclick(x, y) { onclick(x, y); }
ondblclick.local = 1;

function onresize() { mgraphics.redraw(); }
onresize.local = 1;

function bang() { mgraphics.redraw(); }
