// dooblr-chord-receiver status display (jsui + mgraphics, ES5 only).
autowatch = 0;
inlets = 1;
outlets = 0;
setinletassist(0, 'setstate <json>');

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var PALETTE = {
  bg: '#16181c', textPrimary: '#e6e8eb', textMuted: '#8a919c',
  accent: '#5fb3a1', warn: '#d9a35b'
};

var model = { connected: false, name: 'NO SENDER', channel: 0 };

function hexToRgba(hex, alpha) {
  var h = hex.charAt(0) === '#' ? hex.substring(1) : hex;
  return [parseInt(h.substring(0, 2), 16) / 255, parseInt(h.substring(2, 4), 16) / 255,
    parseInt(h.substring(4, 6), 16) / 255, alpha === undefined ? 1 : alpha];
}

function setColor(hex, alpha) {
  var c = hexToRgba(hex, alpha);
  mgraphics.set_source_rgba(c[0], c[1], c[2], c[3]);
}

function paint() {
  var r = box.rect, w = r[2] - r[0], h = r[3] - r[1];
  setColor(PALETTE.bg);
  mgraphics.rectangle(0, 0, w, h);
  mgraphics.fill();

  var cx = 12, cy = h / 2, rad = 6;
  setColor(model.connected ? PALETTE.accent : PALETTE.textMuted, model.connected ? 1 : 0.45);
  mgraphics.ellipse(cx - rad, cy - rad, rad * 2, rad * 2);
  mgraphics.fill();
  if (model.connected) {
    setColor('#ffffff', 0.35);
    mgraphics.ellipse(cx - 2, cy - 3, 3, 3);
    mgraphics.fill();
  }

  var label = model.name || 'NO SENDER';
  var color = model.connected ? PALETTE.textPrimary : PALETTE.warn;
  mgraphics.select_font_face('Arial Bold');
  var pt = 13;
  mgraphics.set_font_size(pt);
  while (pt > 8 && mgraphics.text_measure(label)[0] > w - 28) {
    pt -= 0.5;
    mgraphics.set_font_size(pt);
  }
  setColor(color, model.connected ? 1 : 0.85);
  mgraphics.move_to(24, cy + pt * 0.35);
  mgraphics.show_text(label);
}

function setstate(json) {
  try { model = JSON.parse(json); } catch (e) {
    post('dooblr-chord-receiver display: bad state: ' + e + '\n');
  }
  try {
    var hint = model.connected
      ? ('Connected on channel ' + 'ABCD'.charAt(model.channel || 0) + ': ' + model.name)
      : 'NO SENDER. Drop dooblr-chords on another track, set the same Channel (A–D), and generate or play a progression.';
    box.message('annotation', hint);
  } catch (e2) {}
  mgraphics.redraw();
}

function onresize() { mgraphics.redraw(); }
onresize.local = 1;
function bang() { mgraphics.redraw(); }
