// Generate src/patcher/dooblr-titan.maxpat (the device source of truth).
// Run: node scripts/make-patcher.js
'use strict';
const fs = require('fs');
const path = require('path');

const W = 860, H = 252;
const BG = [0.086, 0.094, 0.11, 1];
const PANEL = [0.114, 0.125, 0.149, 1];
const RAISED = [0.145, 0.161, 0.192, 1];
const DIVIDER = [0.173, 0.188, 0.22, 1];
const TEXT = [0.902, 0.91, 0.922, 1];
const MUTED = [0.541, 0.569, 0.612, 1];
const ACCENT = [0.373, 0.702, 0.631, 1];
const INACTIVE = 0.38;

let n = 1;
function id() { return 'obj-' + (n++); }

function box(b) { return { box: b }; }

function panel(rect, color) {
  return box({
    maxclass: 'panel', numinlets: 1, numoutlets: 0, mode: 0, border: 0, rounded: 0,
    background: 1, ignoreclick: 1, bgcolor: color, bgfillcolor_type: 'color', bgfillcolor_color: color,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function comment(text, rect, opts) {
  opts = opts || {};
  return box({
    maxclass: 'comment', numinlets: 1, numoutlets: 0, text: text,
    fontname: opts.bold ? 'Arial Bold' : 'Arial', fontsize: opts.size || 9,
    textcolor: opts.color || MUTED, ignoreclick: 1, background: 0,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect,
    varname: opts.varname
  });
}

function paramAttrs(longname, type, extra) {
  extra = extra || {};
  const v = { parameter_longname: longname, parameter_shortname: extra.short || longname, parameter_type: type };
  if (extra.enum) { v.parameter_enum = extra.enum; v.parameter_mmax = extra.enum.length - 1; }
  if (extra.min !== undefined) { v.parameter_mmin = extra.min; }
  if (extra.max !== undefined) { v.parameter_mmax = extra.max; }
  if (extra.initial !== undefined) { v.parameter_initial = [].concat(extra.initial); v.parameter_initial_enable = 1; }
  if (extra.unit !== undefined) { v.parameter_unitstyle = extra.unit; }
  v.parameter_invisible = extra.invisible !== undefined ? extra.invisible : 0;
  return v;
}

function liveText(varname, label, rect, opts) {
  opts = opts || {};
  const hero = !!opts.hero;
  const toggle = opts.mode === 1;
  const onColor = hero ? ACCENT : (toggle ? ACCENT : RAISED);
  const textOff = hero ? BG : TEXT;
  const textOn = hero ? TEXT : (toggle ? BG : TEXT);
  const fs = opts.fontsize || (hero ? 13 : 9);
  return box({
    maxclass: 'live.text', varname: varname, numinlets: 1, numoutlets: 2, outlettype: ['', ''],
    parameter_enable: 1, mode: toggle ? 1 : 0,
    text: toggle ? (opts.text || 'Off') : label,
    texton: toggle ? (opts.texton || 'On') : label,
    fontname: hero || opts.bold ? 'Arial Bold' : 'Arial', fontsize: fs,
    activebgcolor: onColor, activebgoncolor: toggle ? ACCENT : RAISED,
    activetextcolor: textOff, activetextoncolor: textOn,
    bgcolor: [onColor[0], onColor[1], onColor[2], INACTIVE],
    bgoncolor: toggle ? [ACCENT[0], ACCENT[1], ACCENT[2], INACTIVE] : [RAISED[0], RAISED[1], RAISED[2], INACTIVE],
    textcolor: [textOff[0], textOff[1], textOff[2], INACTIVE],
    textoncolor: [textOn[0], textOn[1], textOn[2], INACTIVE],
    bordercolor: DIVIDER, focusbordercolor: ACCENT,
    saved_attribute_attributes: {
      valueof: paramAttrs(opts.longname || label, 2, {
        enum: ['off', 'on'], initial: toggle ? (opts.initial !== undefined ? opts.initial : 1) : undefined,
        invisible: toggle ? 0 : 2
      })
    },
    annotation_name: opts.longname || label, annotation: opts.hint || label,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function liveDial(varname, label, rect, opts) {
  opts = opts || {};
  const accent = opts.dynamic ? [0.851, 0.639, 0.357, 1] : ACCENT;
  return box({
    maxclass: 'live.dial', varname: varname, numinlets: 1, numoutlets: 2, outlettype: ['', 'float'],
    parameter_enable: 1, showname: 1, shownumber: 1,
    dialcolor: DIVIDER, activedialcolor: accent, needlecolor: TEXT, activeneedlecolor: TEXT,
    panelcolor: RAISED, textcolor: TEXT, fontname: 'Arial', fontsize: 9,
    saved_attribute_attributes: {
      valueof: paramAttrs(label, 1, { min: opts.min, max: opts.max, initial: opts.initial, unit: opts.unit !== undefined ? opts.unit : 5 })
    },
    annotation_name: label, annotation: opts.hint || label,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function liveMenu(varname, label, rect, opts) {
  return box({
    maxclass: 'live.menu', varname: varname, numinlets: 1, numoutlets: 3, outlettype: ['', '', 'float'],
    parameter_enable: 1, fontname: 'Arial', fontsize: 10,
    activebgcolor: RAISED, bgcolor: [RAISED[0], RAISED[1], RAISED[2], INACTIVE],
    textcolor: TEXT, activetextcolor: TEXT, tricolor: MUTED, activetricolor: MUTED, bordercolor: DIVIDER,
    saved_attribute_attributes: {
      valueof: paramAttrs(label, 2, { enum: opts.enum, initial: opts.initial })
    },
    annotation_name: label, annotation: opts.hint || label,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function liveNumbox(varname, label, rect, opts) {
  return box({
    maxclass: 'live.numbox', varname: varname, numinlets: 1, numoutlets: 2, outlettype: ['', 'float'],
    parameter_enable: 1, fontname: 'Arial', fontsize: 9,
    activebgcolor: RAISED, textcolor: MUTED, tricolor: MUTED, activetricolor: MUTED,
    activeslidercolor: MUTED, bordercolor: DIVIDER,
    saved_attribute_attributes: {
      valueof: paramAttrs(label, 1, { min: opts.min, max: opts.max, initial: opts.initial, unit: 0, invisible: 1 })
    },
    annotation_name: label, annotation: opts.hint || label,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function liveDrop(varname, rect, opts) {
  return box({
    maxclass: 'live.drop', varname: varname, numinlets: 1, numoutlets: 2, outlettype: ['', ''],
    parameter_enable: 1, legend: opts.legend, fontname: 'Arial', fontsize: 9,
    bgcolor: RAISED, bordercolor: ACCENT, focusbordercolor: ACCENT, textcolor: MUTED,
    saved_attribute_attributes: { valueof: { parameter_longname: opts.longname, parameter_shortname: 'Source', parameter_invisible: 0 } },
    annotation_name: opts.longname, annotation: opts.hint,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function jsui(filename, rect, opts) {
  return box({
    maxclass: 'jsui', filename: filename, numinlets: 1, numoutlets: 1, outlettype: [''],
    parameter_enable: 0, border: 0, annotation_name: opts.name, annotation: opts.hint,
    id: id(), patching_rect: rect, presentation: 1, presentation_rect: rect
  });
}

function hidden(text, maxclass, io, xy, extra) {
  extra = extra || {};
  const b = {
    maxclass: maxclass, text: text, numinlets: io[0], numoutlets: io[1],
    id: id(), patching_rect: [xy[0], xy[1], extra.w || 140, extra.h || 22]
  };
  if (io[1] > 0) { b.outlettype = extra.outlettype || Array(io[1]).fill(''); }
  if (extra.saved) { b.saved_object_attributes = extra.saved; }
  return box(b);
}

function line(src, srcOut, dst, dstIn) {
  return { patchline: { source: [src, srcOut], destination: [dst, dstIn] } };
}

function main() {
  n = 1;
  const boxes = [];
  const lines = [];

  boxes.push(panel([0, 0, W, H], BG));
  boxes.push(panel([0, 0, 136, H], PANEL));
  boxes.push(panel([12, 10, 3, 30], ACCENT));
  boxes.push(comment('dooblr-titan', [20, 8, 112, 18], { bold: true, size: 14, color: TEXT }));
  boxes.push(comment('v0.1.0', [20, 26, 50, 14], { size: 9 }));

  const tightenBtn = liveText('tighten', 'Tighten', [12, 46, 112, 34], { hero: true, longname: 'Tighten', hint: 'Tighten writes an improved copy of the loaded clip into the next empty slot.' });
  const generateBtn = liveText('generate', 'Generate', [12, 84, 112, 26], { bold: true, fontsize: 11, longname: 'Generate', hint: 'Generate builds four variations of the loaded clip.' });
  const anotherBtn = liveText('another', 'Another', [12, 114, 68, 22], { fontsize: 10, longname: 'Another', hint: 'Another picks a new seed and rebuilds the four variations.' });
  const writeBtn = liveText('write', 'Write', [84, 114, 40, 22], { fontsize: 9, longname: 'Write', hint: 'Write puts the selected variation into the next empty clip slot.' });
  const saveBtn = liveText('savemidi', 'Save MIDI', [12, 140, 112, 20], { fontsize: 9, longname: 'Save MIDI', hint: 'Save MIDI writes the last Tighten or Write result as a .mid file.' });
  boxes.push(tightenBtn, generateBtn, anotherBtn, writeBtn, saveBtn);
  boxes.push(comment('Seed', [12, 172, 28, 14], { size: 9 }));
  const seed = liveNumbox('seed', 'Seed', [42, 170, 82, 18], { min: 1, max: 99999, initial: 1, hint: 'The seed makes a variation repeatable.' });
  boxes.push(seed);

  boxes.push(comment('TIGHTEN', [146, 6, 80, 12], { bold: true, size: 10 }));
  const qstrength = liveDial('qstrength', 'Q Str', [146, 18, 50, 62], { min: 0, max: 100, initial: 100, hint: 'Quantize strength. 0 leaves timing, 100 snaps to the grid.' });
  const swing = liveDial('swing', 'Swing', [198, 18, 50, 62], { min: 0, max: 100, initial: 0, hint: 'Swing delays offbeats. 100 puts the offbeat on the last triplet of the pair.' });
  const minlen = liveDial('minlen', 'MinLen', [250, 18, 50, 62], { min: 0, max: 100, initial: 8, hint: 'Drop notes shorter than this (0 to a sixteenth).' });
  const minvel = liveDial('minvel', 'MinVel', [302, 18, 50, 62], { min: 1, max: 127, initial: 8, unit: 0, hint: 'Drop notes quieter than this velocity.' });
  const gapclose = liveDial('gapclose', 'Gap', [354, 18, 50, 62], { min: 0, max: 100, initial: 0, hint: 'Close same-pitch holes up to this size. Needs Legato on.' });
  const humanize = liveDial('humanize', 'Human', [406, 18, 50, 62], { min: 0, max: 100, initial: 6, hint: 'Seeded velocity jitter after compression.' });
  boxes.push(qstrength, swing, minlen, minvel, gapclose, humanize);

  const grid = liveMenu('grid', 'Grid', [146, 84, 78, 20], {
    enum: ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T'], initial: 3,
    hint: 'Quantize grid, including triplet values.'
  });
  const groove = liveText('groove', 'Groove', [228, 84, 44, 20], { mode: 1, longname: 'Groove', hint: 'Keep the clip\'s consistent micro-timing while pulling outliers onto it.' });
  const flams = liveText('flams', 'Flams', [276, 84, 44, 20], { mode: 1, longname: 'Flams', hint: 'Merge same-pitch hits that start almost together; keep the louder one.' });
  const legato = liveText('legato', 'Legato', [324, 84, 44, 20], { mode: 1, longname: 'Legato', hint: 'Trim same-pitch overlaps and optionally close small gaps.' });
  const scalesnap = liveText('scalesnap', 'Snap', [372, 84, 44, 20], { mode: 1, initial: 0, longname: 'Scale Snap', hint: 'Move every pitch onto the chosen scale.' });
  const autokey = liveText('autokey', 'Auto', [420, 84, 44, 20], { mode: 1, longname: 'Auto Key', hint: 'Detect the key when a clip is loaded.' });
  boxes.push(grid, groove, flams, legato, scalesnap, autokey);

  const velmin = liveDial('velmin', 'VelMin', [146, 110, 50, 62], { min: 1, max: 127, initial: 64, unit: 0, hint: 'Bottom of the target velocity range.' });
  const velmax = liveDial('velmax', 'VelMax', [198, 110, 50, 62], { min: 1, max: 127, initial: 112, unit: 0, hint: 'Top of the target velocity range.' });
  const velcomp = liveDial('velcomp', 'Comp', [250, 110, 50, 62], { min: 0, max: 100, initial: 40, dynamic: true, hint: 'How hard velocities are compressed into Vel Min..Max.' });
  const root = liveMenu('root', 'Root', [304, 128, 44, 20], {
    enum: ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'], initial: 0,
    hint: 'Key centre for Scale Snap and variations.'
  });
  const mode = liveMenu('mode', 'Mode', [352, 128, 112, 20], {
    enum: ['Major', 'Natural Minor', 'Dorian', 'Phrygian', 'Lydian', 'Mixolydian', 'Harmonic Minor'], initial: 0,
    hint: 'Scale for Scale Snap and in-scale substitutions.'
  });
  boxes.push(velmin, velmax, velcomp, root, mode);

  boxes.push(comment('VARIATIONS', [516, 6, 90, 12], { bold: true, size: 10 }));
  const amount = liveDial('amount', 'Amount', [516, 18, 50, 62], { min: 0, max: 100, initial: 40, hint: 'Master variation amount. 0 leaves the clip unchanged.' });
  const rhythm = liveDial('rhythm', 'Rhythm', [570, 18, 46, 58], { min: 0, max: 100, initial: 50, hint: 'Shift, thin and fill.' });
  const melody = liveDial('melody', 'Melody', [620, 18, 46, 58], { min: 0, max: 100, initial: 50, hint: 'In-scale substitutions, octave jumps, inversion, retrograde.' });
  const ghosts = liveDial('ghosts', 'Ghosts', [516, 82, 46, 58], { min: 0, max: 100, initial: 30, hint: 'Quiet fills at the end of a phrase.' });
  const accents = liveDial('accents', 'Accent', [566, 82, 46, 58], { min: 0, max: 100, initial: 40, hint: 'Velocity accents on downbeats, quieter offbeats.' });
  boxes.push(amount, rhythm, melody, ghosts, accents);
  const previews = jsui('dooblr_previews.js', [516, 144, 188, 100], { name: 'Variations', hint: 'Click a variation, then press Write.' });
  boxes.push(previews);

  boxes.push(comment('SOURCE', [716, 6, 80, 12], { bold: true, size: 10 }));
  const srcdrop = liveDrop('srcdrop', [716, 20, 136, 36], {
    legend: 'Drop a .mid file', longname: 'Source File',
    hint: 'Drop a MIDI file (.mid) from Finder or Live\'s file browser. Session and Arrangement clips cannot be dropped in Live 11; use USE CLIP.'
  });
  const useclip = liveText('useclip', 'USE CLIP', [716, 60, 84, 20], { bold: true, fontsize: 10, longname: 'Use Clip', hint: 'Read the MIDI clip shown in the Detail View (or the selected clip).' });
  const clearsource = liveText('clearsource', 'CLEAR', [804, 60, 48, 20], { fontsize: 8.5, longname: 'Clear Source', hint: 'Forget the loaded clip.' });
  boxes.push(srcdrop, useclip, clearsource);
  boxes.push(comment('Drop a .mid file or press USE CLIP.', [716, 84, 136, 28], { size: 9, varname: 'srcinfo' }));
  const roll = jsui('dooblr_roll.js', [716, 116, 136, 128], { name: 'Clip', hint: 'Notes of the loaded clip, and of the last Tighten or Write result.' });
  boxes.push(roll);

  // Hidden patching objects, x >= 900
  const thisdev = hidden('live.thisdevice', 'newobj', [1, 3], [900, 20], { w: 90, outlettype: ['bang', 'int', 'int'] });
  const initMsg = hidden('init', 'message', [2, 1], [900, 52], { w: 34 });
  const js = hidden('js dooblr_titan.js', 'newobj', [1, 4], [900, 420], {
    w: 200, outlettype: ['', '', '', ''], saved: { filename: 'dooblr_titan.js', parameter_enable: 0 }
  });
  boxes.push(thisdev, initMsg, js);

  const paramBoxes = [
    ['qstrength', qstrength], ['grid', grid], ['swing', swing], ['groove', groove], ['flams', flams],
    ['minlen', minlen], ['minvel', minvel], ['legato', legato], ['gapclose', gapclose],
    ['velmin', velmin], ['velmax', velmax], ['velcomp', velcomp], ['humanize', humanize],
    ['scalesnap', scalesnap], ['root', root], ['mode', mode], ['autokey', autokey],
    ['amount', amount], ['rhythm', rhythm], ['melody', melody], ['ghosts', ghosts], ['accents', accents],
    ['seed', seed]
  ];
  paramBoxes.forEach((pair, i) => {
    const pre = hidden('prepend param ' + pair[0], 'newobj', [1, 1], [900 + (i % 5) * 160, 90 + Math.floor(i / 5) * 28], { w: 150 });
    boxes.push(pre);
    lines.push(line(pair[1].box.id, 0, pre.box.id, 0));
    lines.push(line(pre.box.id, 0, js.box.id, 0));
  });

  const buttons = [
    [tightenBtn, 'tighten'], [generateBtn, 'generate'], [anotherBtn, 'another'], [writeBtn, 'write'],
    [useclip, 'useclip'], [clearsource, 'clearsource'], [saveBtn, 'savemidi']
  ];
  buttons.forEach((pair, i) => {
    const rt = hidden('route bang 1', 'newobj', [2, 3], [900 + i * 120, 260], { w: 80, outlettype: ['', '', ''] });
    const msg = hidden(pair[1], 'message', [2, 1], [900 + i * 120, 292], { w: 80 });
    boxes.push(rt, msg);
    lines.push(line(pair[0].box.id, 0, rt.box.id, 0));
    lines.push(line(rt.box.id, 0, msg.box.id, 0));
    lines.push(line(rt.box.id, 1, msg.box.id, 0));
    lines.push(line(msg.box.id, 0, js.box.id, 0));
  });

  const dropPre = hidden('prepend dropfile', 'newobj', [1, 1], [900, 330], { w: 110 });
  boxes.push(dropPre);
  lines.push(line(srcdrop.box.id, 0, dropPre.box.id, 0));
  lines.push(line(dropPre.box.id, 0, js.box.id, 0));

  const midiin = hidden('midiin', 'newobj', [1, 1], [1700, 20], { w: 50, outlettype: ['int'] });
  const midiout = hidden('midiout', 'newobj', [1, 0], [1700, 52], { w: 55 });
  boxes.push(midiin, midiout);
  lines.push(line(midiin.box.id, 0, midiout.box.id, 0));

  const savedialog = hidden('savedialog mid', 'newobj', [1, 1], [1700, 90], { w: 100 });
  const savePre = hidden('prepend savepath', 'newobj', [1, 1], [1700, 122], { w: 120 });
  boxes.push(savedialog, savePre);
  lines.push(line(js.box.id, 3, savedialog.box.id, 0));
  lines.push(line(savedialog.box.id, 0, savePre.box.id, 0));
  lines.push(line(savePre.box.id, 0, js.box.id, 0));

  lines.push(line(thisdev.box.id, 0, initMsg.box.id, 0));
  lines.push(line(initMsg.box.id, 0, js.box.id, 0));
  lines.push(line(js.box.id, 0, roll.box.id, 0));
  lines.push(line(js.box.id, 1, seed.box.id, 0));
  lines.push(line(js.box.id, 2, previews.box.id, 0));
  lines.push(line(previews.box.id, 0, js.box.id, 0));

  boxes.push(box({
    maxclass: 'comment', numinlets: 1, numoutlets: 0,
    text: 'Controls send param <name> <value> to the js. Drop sends dropfile; buttons send tighten/generate/another/write/useclip/clearsource/savemidi. The js sends setstate to the roll and previews, set seed, and bang to savedialog.',
    fontsize: 10, id: id(), patching_rect: [900, 460, 520, 48]
  }));

  const patcher = {
    patcher: {
      fileversion: 1,
      appversion: { major: 8, minor: 5, revision: 8, architecture: 'x64', modernui: 1 },
      classnamespace: 'box',
      rect: [100, 100, 1400, 720],
      openrect: [0, 0, 0, H],
      bglocked: 0,
      openinpresentation: 1,
      default_fontsize: 10,
      default_fontface: 0,
      default_fontname: 'Arial',
      gridonopen: 1,
      gridsize: [8, 8],
      gridsnaponopen: 1,
      objectsnaponopen: 1,
      statusbarvisible: 2,
      toolbarvisible: 1,
      lefttoolbarpinned: 0, toptoolbarpinned: 0, righttoolbarpinned: 0, bottomtoolbarpinned: 0,
      toolbars_unpinned_last_save: 0,
      tallnewobj: 0, boxanimatetime: 200, enablehscroll: 1, enablevscroll: 1,
      devicewidth: W,
      description: 'Tightens MIDI clips and writes musical variations into Session View.',
      digest: '', tags: '', style: '', subpatcher_template: '', assistshowspatchername: 0,
      boxes: boxes,
      lines: lines,
      dependency_cache: [],
      latency: 0, is_mpe: 0, minimum_live_version: '', minimum_max_version: '',
      platform_compatibility: 0, autosave: 0
    }
  };

  const dest = path.join(__dirname, '..', 'src', 'patcher', 'dooblr-titan.maxpat');
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, JSON.stringify(patcher, null, '\t') + '\n');
  console.log('wrote ' + dest + ' (' + boxes.length + ' boxes, ' + lines.length + ' lines)');
}

main();
