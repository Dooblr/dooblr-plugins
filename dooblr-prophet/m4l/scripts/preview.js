// Renders the device's jsui to PNG with a mock mgraphics (a canvas), fed with link messages
// captured from the plug-in's tests (DOOBLRPROPHET_LINK_DUMP=... ./DooblrProphetTests).
//   npm install @napi-rs/canvas   (preview only; the device itself needs nothing)
//   node scripts/preview.js <link-dump.json> <out.png> [width]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { createCanvas } = require('@napi-rs/canvas');

const [dumpPath, outPath, widthArg] = process.argv.slice(2);
const messages = JSON.parse(fs.readFileSync(dumpPath, 'utf8'));
const source = fs.readFileSync(path.join(__dirname, '..', 'src', 'dooblr-prophet-ui.js'), 'utf8');
const W = Number(widthArg || 0) || JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'src', 'dooblr-prophet.maxpat'), 'utf8')).patcher.devicewidth;
const H = 169;

function mockGraphics(ctx) {
  let fontName = 'sans-serif', fontSize = 10, weight = 'normal';
  const setFont = () => { ctx.font = weight + ' ' + fontSize + 'px "DejaVu Sans", sans-serif'; };
  return {
    size: [W, H], relative_coords: 0, autofill: 0,
    init() {}, redraw() {},
    set_source_rgba(r, g, b, a) { const c = 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' + Math.round(b * 255) + ',' + a + ')'; ctx.fillStyle = c; ctx.strokeStyle = c; },
    set_line_width(w) { ctx.lineWidth = w; },
    set_line_cap(c) { ctx.lineCap = c; },
    rectangle(x, y, w, h) { ctx.beginPath(); ctx.rect(x, y, w, h); },
    rectangle_rounded(x, y, w, h, ow, oh) { ctx.beginPath(); ctx.roundRect(x, y, w, h, Math.min(ow / 2, w / 2, h / 2)); },
    ellipse(x, y, w, h) { ctx.beginPath(); ctx.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 0, 0, Math.PI * 2); },
    arc(x, y, r, a1, a2) { ctx.beginPath(); ctx.arc(x, y, r, a1, a2); },
    move_to(x, y) { if (!this._open) { ctx.beginPath(); this._open = true; } ctx.moveTo(x, y); this._x = x; this._y = y; },
    line_to(x, y) { ctx.lineTo(x, y); },
    close_path() { ctx.closePath(); },
    fill() { ctx.fill(); ctx.beginPath(); this._open = false; },
    stroke() { ctx.stroke(); ctx.beginPath(); this._open = false; },
    stroke_preserve() { ctx.stroke(); },
    fill_preserve() { ctx.fill(); },
    select_font_face(name, slant, w) { fontName = name; weight = w === 'bold' ? 'bold' : 'normal'; setFont(); },
    set_font_size(s) { fontSize = s; setFont(); },
    text_measure(s) { setFont(); return [ctx.measureText(s).width, fontSize]; },
    show_text(s) { setFont(); ctx.fillText(s, this._x, this._y); ctx.beginPath(); this._open = false; },
  };
}

function render(setup) {
  const canvas = createCanvas(W, H);
  const ctx = canvas.getContext('2d');
  const sent = [];
  const context = {
    mgraphics: mockGraphics(ctx), Math, Date, String, Number, parseInt, Object, Array, JSON,
    outlet: (...a) => sent.push(a), post: () => {}, messagename: '',
    Task: function (fn) { this.repeat = () => {}; this.cancel = () => {}; },
    LiveAPI: function () { this.getcount = () => 0; this.get = () => ['']; this.id = 0; },
    arrayfromargs: (args) => Array.prototype.slice.call(args),
  };
  vm.createContext(context);
  vm.runInContext(source, context, { filename: 'dooblr-prophet-ui.js' });
  setup(context, sent);
  context.paint();
  return canvas;
}

function feed(context, sent) {
  context.bang();
  context.tick();
  const pairList = sent.find((a) => a[0] === 1);
  const token = context.S.token;
  for (const m of messages) {
    context.messagename = m[0];
    const args = m.slice(1);
    if (m[0] === '/dp/announce') args[2] = token;
    context.anything.apply(null, args);
  }
  if (!pairList) throw new Error('no pairing CCs were sent');
}

const frames = [];
for (let page = 0; page < 6; ++page) frames.push(render((c, sent) => { feed(c, sent); c.S.page = page; c.S.needsLayout = true; }));
frames.push(render((c, sent) => { feed(c, sent); c.S.page = 0; c.paint(); c.onclick(c.S.items.find((i) => i.type === 'presetbox').x + 5, 10, 1, 0, 0); }));
frames.push(render((c) => { c.bang(); }));

const gap = 6;
const sheet = createCanvas(W, frames.length * (H + gap) - gap);
const sctx = sheet.getContext('2d');
sctx.fillStyle = '#0b0c0e';
sctx.fillRect(0, 0, sheet.width, sheet.height);
frames.forEach((f, i) => sctx.drawImage(f, 0, i * (H + gap)));
fs.writeFileSync(outPath, sheet.toBuffer('image/png'));
console.log('wrote ' + outPath + ' (' + W + 'x' + sheet.height + ', ' + frames.length + ' frames)');
