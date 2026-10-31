// dooblr-prophet-ui.js - the dooblr-prophet remote panel (Max for Live jsui, ES5).
//
// Draws a compact copy of the dooblr-prophet editor and talks to the plug-in over the
// localhost OSC link (see ../README.md). The layout (pages, panels, controls), every
// parameter's name, range and text, the dimming rules and the graph data all come from
// the plug-in, so this file holds no copy of the synth's parameter list.
//
// Outlets: 0 -> udpsend (OSC to the plug-in), 1 -> MIDI bytes (pairing CCs), 2 -> udpreceive (port).

autowatch = 0;
inlets = 1;
outlets = 3;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

//==============================================================================
// Link constants (must match Source/Link/RemoteLink.h)
var REPLY_PORT_BASE = 47900, MAX_TOKEN = 16000;
var PAIR_STATUS = 0xBF, CC_HIGH = 102, CC_LOW = 103, CC_CHECK = 104; // CC on channel 16
var PAIR_INTERVAL_MS = 1000, HELLO_INTERVAL_MS = 1000, LOST_AFTER_MS = 3500;
var FX_PRESET_PARAMS = ["echoPreset", "reverbPreset"]; // param shown by each module's preset picker
var UI_VERSION = "0.3.1"; // this panel's version (posted to the Max window by liveready)
var FIND_DEBOUNCE_MS = 50;

// Palette (Source/UI/Theme.h)
var C = {
    background: 0x16181c, panel: 0x1d2026, raised: 0x252931, divider: 0x2c3038, grid: 0x262a31,
    text: 0xe6e8eb, muted: 0x8a919c, accent: 0x5fb3a1, dynamic: 0xd9a35b, spectrum: 0x9fb4c8
};
var FONT = "Lucida Grande";

// Geometry (Live's device height is 169 px)
var H = 169, HEADER_H = 24, NAV_W = 80, CONTENT_X = 88, TITLE_Y = 26, BODY_Y = 50, BODY_BOTTOM = 166;
var KNOB_W = 46, KNOB_H = 50, SMALL_W = 96, TOGGLE_H = 16, COMBO_H = 34, BUTTONS_H = 52;
var PANEL_TITLE_H = 12, PANEL_GAP = 14, GRAPH_MIN_W = 200;

//==============================================================================
var S = {
    state: "waiting",        // waiting (no Live API yet) | missing | searching | syncing | linked
    deviceOnTrack: 0,         // 0 none, 1 after this device (good), -1 before this device
    token: 0, lastPair: [-1, -1, -1], lastPairMs: 0,
    serverPort: 0, replyPort: 0, version: "", lastHeardMs: 0, lastHelloMs: 0,
    meta: [], val: [], byId: {}, pages: [], fxNames: [[], []], voicePresets: [],
    dim: {}, status: [], note: "", noteSerial: -1, noteUntil: 0,
    factory: [], user: [], presetName: "", program: -1, canUndo: 0, canRedo: 0,
    graphs: {}, live: null, meter: [0, 0],
    needsLayout: true, page: 0, items: [], menu: null, drag: null, hover: null, dirty: true, width: 0
};

function now() { return new Date().getTime(); }
function str(v) { var s = String(v); return s === "~" ? "" : s; }
function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

//==============================================================================
// Max entry points

// Nothing touches the Live API until live.thisdevice says Live is ready: the patch turns its bang
// into a "liveready" message. Calling LiveAPI earlier (at load or from loadbang) is not allowed.
var liveReady = false;

function loadbang() {}   // deliberately empty, see liveready()
function bang() {}       // nothing in the patch sends a bare bang; kept so one cannot start anything

function liveready() {
    if (!liveReady) {
        liveReady = true;
        post("dooblr-prophet panel " + UI_VERSION + "\n");
        watchDevices();
    }
    if (S.state === "waiting") S.state = "searching";
    findPlugin();
    schedule();
    redraw();
}

var ticker = null;
function schedule() {
    if (ticker !== null) return;
    ticker = new Task(tick, this);
    ticker.interval = 33;
    ticker.repeat();
}

function notifydeleted() {
    liveReady = false;   // first, so a late watcher callback cannot schedule anything
    if (S.serverPort > 0) send("/dp/bye");
    if (ticker !== null) ticker.cancel();
    if (findTask !== null) findTask.cancel();
    findPending = false;
    if (trackWatcher !== null) { try { trackWatcher.property = ""; } catch (e) {} trackWatcher = null; }
}

function tick() {
    var t = now();
    if (S.state === "searching" || S.state === "missing") {
        if (t - S.lastPairMs >= PAIR_INTERVAL_MS) pair(t);
    } else if (S.state === "syncing" || S.state === "linked") {
        if (t - S.lastHelloMs >= HELLO_INTERVAL_MS) { S.lastHelloMs = t; send("/dp/hello", S.replyPort); }
        if (t - S.lastHeardMs > LOST_AFTER_MS) lost();
    }
    var decay = Math.pow(10, -20 / 20 * 0.033);   // the meter falls 20 dB per second
    if (S.meter[0] > 0.0001 || S.meter[1] > 0.0001) { S.meter[0] *= decay; S.meter[1] *= decay; S.dirty = true; }
    if (S.noteUntil > 0 && t > S.noteUntil) { S.noteUntil = 0; S.dirty = true; }
    if (S.dirty) { S.dirty = false; mgraphics.redraw(); }
}

function redraw() { S.dirty = true; }

// Pairing: a fresh token on channel-16 CCs through this track's MIDI; the plug-in that sees it
// answers on 47900 + token. Every value differs from the last attempt, so a host that drops a
// repeated CC value cannot stall pairing.
function pair(t) {
    S.lastPairMs = t;
    var h, l, c, token, tries = 0;
    do {
        token = 1 + Math.floor(Math.random() * MAX_TOKEN);
        h = (token >> 7) & 127; l = token & 127; c = (h ^ l ^ 0x2a) & 127;
        tries++;
    } while (tries < 50 && (h === S.lastPair[0] || l === S.lastPair[1] || c === S.lastPair[2]));
    S.lastPair = [h, l, c];
    S.token = token;
    S.replyPort = REPLY_PORT_BASE + token;
    outlet(2, "port", S.replyPort);
    outlet(1, [PAIR_STATUS, CC_HIGH, h, PAIR_STATUS, CC_LOW, l, PAIR_STATUS, CC_CHECK, c]);
}

function lost() {
    S.serverPort = 0;
    S.state = S.deviceOnTrack === 0 ? "missing" : "searching";
    S.menu = null; S.drag = null;
    S.lastPairMs = 0;
    redraw();
}

function send() {
    if (S.serverPort <= 0) return;
    outlet(0, arrayfromargs(arguments));
}

//==============================================================================
// Live API: is there a dooblr-prophet on this track, and after this device?

//
// LiveAPI callbacks can fire synchronously: when an observing LiveAPI is constructed (with
// "id N") and when its .property is set. 0.3.0 created the watcher inside findPlugin() and called
// findPlugin() from the callback, so the callback re-entered findPlugin() before the watcher was
// assigned and recursed until Live aborted. Now:
//   - the watcher is created exactly once, by liveready(), and never from a callback;
//   - its callback only schedules a deferred, coalesced Task; it never calls findPlugin() itself;
//   - findPlugin() refuses to run re-entrantly or before liveready().

var trackWatcher = null, watcherStarted = false;
var findTask = null, findPending = false, inFind = false;

function watchDevices() {
    if (watcherStarted) return;
    watcherStarted = true;              // set first: a synchronous callback must not get here again
    try {
        trackWatcher = new LiveAPI(onDevicesChanged, "this_device canonical_parent");
        trackWatcher.property = "devices";
    } catch (e) {
        post("dooblr-prophet panel: cannot watch this track's devices: " + e + "\n");
    }
}

function onDevicesChanged() {
    var a = arrayfromargs(arguments);
    if (a.length > 0 && a[0] === "id") return;   // the notification on construction, not a change
    requestFind();
}

function requestFind() {
    if (!liveReady || findPending) return;      // coalesce bursts into one deferred look
    findPending = true;
    if (findTask === null) findTask = new Task(deferredFind);
    findTask.schedule(FIND_DEBOUNCE_MS);
}

function deferredFind() {
    findPending = false;
    findPlugin();
}

function findPlugin() {
    if (!liveReady || inFind) return;
    inFind = true;
    var found = 0;
    try {
        var self = new LiveAPI("this_device");
        var count = new LiveAPI("this_device canonical_parent").getcount("devices");
        var selfIndex = -1, pluginIndex = -1;
        for (var i = 0; i < count; ++i) {
            var d = new LiveAPI("this_device canonical_parent devices " + i);
            if (d.id == self.id) selfIndex = i;
            var name = String(d.get("name"));
            var cls = String(d.get("class_name"));
            if (pluginIndex < 0 && cls === "PluginDevice" && /dooblr-prophet/i.test(name)) pluginIndex = i;
        }
        found = pluginIndex < 0 ? 0 : (pluginIndex > selfIndex ? 1 : -1);
    } catch (e) {
        found = 0;
    } finally {
        inFind = false;
    }
    S.deviceOnTrack = found;
    if (S.state !== "linked" && S.state !== "syncing") S.state = found === 0 ? "missing" : "searching";
    redraw();
}

//==============================================================================
// Messages from the plug-in (udpreceive outputs the OSC address as the selector)

function anything() {
    handle(messagename, arrayfromargs(arguments));
}

function handle(address, a) {
    if (address === "/dp/announce" || address === "/dp/here") {
        if (address === "/dp/announce" && a[2] !== S.token) return;
        if (S.state === "linked" || S.state === "syncing") return;
        S.serverPort = a[0];
        S.version = str(a[1]);
        outlet(0, "port", S.serverPort);
        S.state = "syncing";
        S.lastHeardMs = S.lastHelloMs = now();
        send("/dp/hello", S.replyPort);
        redraw();
        return;
    }
    if (S.serverPort <= 0) return;
    S.lastHeardMs = now();

    switch (address) {
    case "/dp/syncstart":
        S.version = str(a[0]); S.meta = []; S.val = []; S.byId = {}; S.pages = []; S.dim = {};
        S.state = "syncing";
        break;
    case "/dp/meta":
        var choices = str(a[10]);
        S.meta[a[0]] = { id: str(a[1]), name: str(a[2]), label: str(a[3]), kind: a[4], def: a[5], steps: a[6],
                         origin: a[7], lo: a[8], hi: a[9], choices: choices === "" ? [] : choices.split("|") };
        S.byId[str(a[1])] = a[0];
        if (!S.val[a[0]]) S.val[a[0]] = { n: a[5], t: "" };
        break;
    case "/dp/page":
        S.pages[a[0]] = { name: str(a[1]), colour: parseInt(str(a[2]).slice(-6), 16), rows: parseRows(str(a[3])),
                          toggles: [], panels: [] };
        break;
    case "/dp/mode":
        if (S.pages[a[0]]) S.pages[a[0]].toggles[a[1]] = { caption: str(a[2]), id: str(a[3]), labels: str(a[4]).split("|") };
        break;
    case "/dp/panel":
        var panel = { title: str(a[2]), voicePresets: a[3] === 1, fxPicker: a[4], controls: [] };
        for (var i = 5; i + 2 < a.length; i += 3) panel.controls.push({ kind: str(a[i]), id: str(a[i + 1]), caption: str(a[i + 2]) });
        if (S.pages[a[0]]) S.pages[a[0]].panels[a[1]] = panel;
        break;
    case "/dp/fxnames": S.fxNames[a[0]] = str(a[1]).split("|"); break;
    case "/dp/voicepresets": S.voicePresets = str(a[0]).split("|").map(Number); break;
    case "/dp/layoutdone": S.page = clamp(S.page, 0, S.pages.length - 1); layout(); break;
    case "/dp/synced": S.state = "linked"; layout(); break;
    case "/dp/v":
        S.val[a[0]] = { n: a[1], t: str(a[2]) };
        if (S.drag && S.drag.idx === a[0]) S.val[a[0]].n = S.drag.local; // the mouse wins while dragging
        break;
    case "/dp/presets":
        var nf = a[0];
        S.factory = a.slice(1, 1 + nf).map(str);
        S.user = a.slice(1 + nf).map(str);
        break;
    case "/dp/preset": S.presetName = str(a[0]); S.program = a[1]; break;
    case "/dp/undo": S.canUndo = a[0]; S.canRedo = a[1]; break;
    case "/dp/dim":
        S.dim = {};
        for (var d = 0; d < a.length; ++d) S.dim[str(a[d])] = true;
        break;
    case "/dp/status":
        S.status = a.slice(0, 6).map(str);
        if (a[7] !== S.noteSerial) {
            S.noteSerial = a[7];
            S.note = str(a[6]);
            S.noteUntil = S.note === "" ? 0 : now() + 5000;
        }
        break;
    case "/dp/g/osc": S.graphs.osc = a; break;
    case "/dp/g/flt": S.graphs.flt = a; break;
    case "/dp/g/env": S.graphs.env = a; break;
    case "/dp/g/lfo": S.graphs.lfo = a; break;
    case "/dp/g/fx": S.graphs.fx = a; break;
    case "/dp/g/voc": S.graphs.voc = a; break;
    case "/dp/live":
        S.live = a;
        S.meter[0] = Math.max(S.meter[0], a[1]);
        S.meter[1] = Math.max(S.meter[1], a[2]);
        break;
    case "/dp/alive": break;
    case "/dp/gone": lost(); return;
    default: return;
    }
    redraw();
}

function parseRows(text) {
    return text.split("|").map(function (r) { return r.split(",").map(Number); });
}

//==============================================================================
// Parameters

function P(id) { var i = S.byId[id]; return i === undefined ? null : { idx: i, meta: S.meta[i], val: S.val[i] || { n: 0, t: "" } }; }
function choiceIndex(p) { var n = Math.max(1, p.meta.choices.length); return Math.round(p.val.n * (n - 1)); }
function choiceNorm(p, i) { var n = Math.max(2, p.meta.choices.length); return i / (n - 1); }
function isDim(id) { return S.dim[id] === true; }
function pageColour() { var p = S.pages[S.page]; return p ? p.colour : C.accent; }

//==============================================================================
// Layout: hit-tested items for the current page (rebuilt when the page or layout changes)

// Layout needs text metrics, so it runs at the start of paint().
function layout() { S.needsLayout = true; redraw(); }

function doLayout() {
    S.needsLayout = false;
    var W = mgraphics.size[0];
    S.width = W;
    S.items = [];
    headerItems(W);
    for (var i = 0; i < S.pages.length; ++i)
        S.items.push({ type: "tab", page: i, x: 6, y: HEADER_H + 6 + i * 22, w: NAV_W - 12, h: 19 });

    var page = S.pages[S.page];
    if (!page) return;

    // Mode switches on the title bar, right aligned
    var right = W - 6;
    for (var t = page.toggles.length - 1; t >= 0; --t) {
        var tg = page.toggles[t];
        var segW = 0;
        for (var l = 0; l < tg.labels.length; ++l) segW = Math.max(segW, textWidth(tg.labels[l], 8.5, true) + 18);
        var w = Math.max(90, segW * tg.labels.length);
        right -= w;
        S.items.push({ type: "seg", id: tg.id, labels: tg.labels, caption: tg.caption, x: right, y: TITLE_Y + 1, w: w, h: 16 });
        right -= textWidth(tg.caption, 8, true) + 16;
    }
    S.statusRight = right;

    // Panels in the layout's row order, side by side
    var panels = [];
    for (var r = 0; r < page.rows.length; ++r)
        for (var c = 0; c < page.rows[r].length; ++c) panels.push(page.panels[page.rows[r][c]]);

    var blocks = [], total = 0;
    for (var p = 0; p < panels.length; ++p) {
        if (!panels[p]) continue;
        var b = panelColumns(panels[p]);
        blocks.push(b);
        total += b.width + (blocks.length > 1 ? PANEL_GAP : 0);
    }
    var graphW = Math.max(GRAPH_MIN_W, W - CONTENT_X - 6 - PANEL_GAP - total);
    S.graphRect = { x: CONTENT_X, y: BODY_Y, w: graphW, h: BODY_BOTTOM - BODY_Y };
    if (S.page === 1) S.items.push({ type: "filtergraph", x: CONTENT_X, y: BODY_Y, w: graphW, h: BODY_BOTTOM - BODY_Y });

    var x = CONTENT_X + graphW + PANEL_GAP;
    S.panelBoxes = [];
    for (var k = 0; k < blocks.length; ++k) {
        S.panelBoxes.push({ title: blocks[k].title, x: x, w: blocks[k].width });
        var cx = x;
        for (var col = 0; col < blocks[k].columns.length; ++col) {
            var column = blocks[k].columns[col];
            var y = BODY_Y + PANEL_TITLE_H;
            for (var e = 0; e < column.items.length; ++e) {
                var it = column.items[e];
                it.x = cx; it.y = y; it.w = column.width;
                S.items.push(it);
                y += it.h;
            }
            cx += column.width;
        }
        x += blocks[k].width + PANEL_GAP;
    }
}

// A panel's controls as columns: leading switches and menus, knobs two to a column, then the rest
// (the editor's left-to-right order, folded into the device's height).
function panelColumns(panel) {
    var lead = [], knobs = [], tail = [];
    var seenKnob = false;
    for (var i = 0; i < panel.controls.length; ++i) {
        var ctl = panel.controls[i];
        if (S.byId[ctl.id] === undefined) continue;
        var item = ctl.kind === "knob" ? { type: "knob", id: ctl.id, caption: ctl.caption, h: KNOB_H }
                 : ctl.kind === "toggle" ? { type: "toggle", id: ctl.id, caption: ctl.caption, h: TOGGLE_H }
                 : { type: "combo", id: ctl.id, caption: ctl.caption, h: COMBO_H };
        if (item.type === "knob") { seenKnob = true; knobs.push(item); }
        else (seenKnob ? tail : lead).push(item);
        // The FX preset picker sits right after the module's On switch.
        if (panel.fxPicker >= 0 && i === 0)
            lead.push({ type: "picker", module: panel.fxPicker, id: FX_PRESET_PARAMS[panel.fxPicker], caption: "Preset", h: COMBO_H });
    }
    if (panel.voicePresets)
        tail.push({ type: "vpresets", caption: "Preset", counts: S.voicePresets, h: BUTTONS_H });

    var columns = [];
    var pushSmall = function (list) {
        var col = null;
        for (var s = 0; s < list.length; ++s) {
            if (col === null || col.used + list[s].h > 2 * KNOB_H) { col = { width: SMALL_W, items: [], used: 0 }; columns.push(col); }
            col.items.push(list[s]); col.used += list[s].h;
        }
    };
    pushSmall(lead);
    // Knobs in two rows, read left to right like the editor: the first half on top.
    var perRow = Math.ceil(knobs.length / 2);
    for (var k = 0; k < perRow; ++k) {
        var pair = [knobs[k]];
        if (k + perRow < knobs.length) pair.push(knobs[k + perRow]);
        columns.push({ width: KNOB_W, items: pair });
    }
    pushSmall(tail);

    var width = 0;
    for (var c = 0; c < columns.length; ++c) width += columns[c].width;
    return { title: panel.title, columns: columns, width: Math.max(width, textWidth(panel.title, 8, true)) };
}

function headerItems(W) {
    var x = 14 + textWidth("dooblr-prophet", 12.5, true) + 6 + textWidth("v" + (S.version || "0.0.0"), 8, false) + 14;
    var y = 4, h = 16;
    S.items.push({ type: "button", action: "undo", label: "Undo", x: x, y: y, w: 36, h: h }); x += 40;
    S.items.push({ type: "button", action: "redo", label: "Redo", x: x, y: y, w: 36, h: h }); x += 44;
    var meterX = W - 22;
    var boxX = meterX - 8 - 44;
    var sliderX = boxX - 6 - 96;
    var captionX = sliderX - 6 - textWidth("Output", 9, false);
    var buttonsRight = captionX - 14;
    var loadX = buttonsRight - 36, saveX = loadX - 40, randomX = saveX - 54;
    var presetW = clamp(randomX - 6 - x, 120, 260);
    S.items.push({ type: "presetbox", x: x, y: 3.5, w: presetW, h: 17 });
    S.items.push({ type: "button", action: "random", label: "Random", x: randomX, y: y, w: 50, h: h });
    S.items.push({ type: "button", action: "save", label: "Save", x: saveX, y: y, w: 36, h: h });
    S.items.push({ type: "button", action: "load", label: "Load", x: loadX, y: y, w: 36, h: h });
    S.items.push({ type: "output", id: "output", x: sliderX, y: 4, w: 96, h: 16 });
    S.outputBox = { x: boxX, y: 4, w: 44, h: 16 };
    S.outputCaptionX = captionX;
    S.meterRect = { x: meterX, y: 3, w: 16, h: 18 };
    S.noteRect = { x: x + presetW + 10, right: randomX - 10 };
}

//==============================================================================
// Drawing helpers

function rgba(hex, a) {
    return [((hex >> 16) & 255) / 255, ((hex >> 8) & 255) / 255, (hex & 255) / 255, a === undefined ? 1 : a];
}
function colour(hex, a) { var c = rgba(hex, a); mgraphics.set_source_rgba(c[0], c[1], c[2], c[3]); }
function mix(hexA, hexB, t) {
    var a = rgba(hexA), b = rgba(hexB), r = [];
    for (var i = 0; i < 3; ++i) r.push(Math.round((a[i] + (b[i] - a[i]) * t) * 255));
    return (r[0] << 16) | (r[1] << 8) | r[2];
}
function brighter(hex, amount) { return mix(hex, 0xffffff, amount); }
function fillRect(x, y, w, h, hex, a) { colour(hex, a); mgraphics.rectangle(x, y, w, h); mgraphics.fill(); }
function fillRound(x, y, w, h, r, hex, a) { colour(hex, a); mgraphics.rectangle_rounded(x, y, w, h, r * 2, r * 2); mgraphics.fill(); }
function strokeRound(x, y, w, h, r, hex, a, lw) {
    colour(hex, a); mgraphics.set_line_width(lw || 1);
    mgraphics.rectangle_rounded(x + 0.5, y + 0.5, w - 1, h - 1, r * 2, r * 2); mgraphics.stroke();
}
function line(x1, y1, x2, y2, hex, a, lw) {
    colour(hex, a); mgraphics.set_line_width(lw || 1);
    mgraphics.move_to(x1, y1); mgraphics.line_to(x2, y2); mgraphics.stroke();
}
function font(size, bold) { mgraphics.select_font_face(FONT, "normal", bold ? "bold" : "normal"); mgraphics.set_font_size(size); }
function textWidth(s, size, bold) { font(size, bold); return mgraphics.text_measure(String(s))[0]; }

// Text in a box: align "left" | "centre" | "right", squeezed with an ellipsis when too long.
function text(s, x, y, w, h, size, bold, hex, align, a) {
    s = String(s);
    font(size, bold);
    var tw = mgraphics.text_measure(s)[0];
    if (tw > w && s.length > 1) {
        while (s.length > 1 && mgraphics.text_measure(s + "\u2026")[0] > w) s = s.slice(0, -1);
        s += "\u2026";
        tw = mgraphics.text_measure(s)[0];
    }
    var tx = align === "centre" ? x + (w - tw) / 2 : align === "right" ? x + w - tw : x;
    colour(hex, a);
    mgraphics.move_to(tx, y + h / 2 + size * 0.36);
    mgraphics.show_text(s);
}

//==============================================================================
// Paint

function paint() {
    if (S.needsLayout || mgraphics.size[0] !== S.width) doLayout();
    var W = mgraphics.size[0];
    fillRect(0, 0, W, H, C.background);
    fillRect(0, 0, W, HEADER_H, C.panel);
    fillRect(0, HEADER_H - 1, W, 1, C.divider);

    // Title
    fillRect(8, 5, 2, 14, C.accent);
    text("dooblr-prophet", 14, 3, 140, 18, 12.5, true, C.text, "left");
    var tw = textWidth("dooblr-prophet", 12.5, true);
    text(S.version ? "v" + S.version : "", 14 + tw + 6, 5, 60, 16, 8, false, C.muted, "left");

    if (S.state !== "linked") { paintAbsent(W); return; }

    fillRect(0, HEADER_H, NAV_W, H - HEADER_H, C.panel);
    fillRect(NAV_W - 1, HEADER_H, 1, H - HEADER_H, C.divider);

    var page = S.pages[S.page];
    var pc = pageColour();
    text(page ? page.name.toUpperCase() : "", CONTENT_X, TITLE_Y, 160, 18, 10.5, true, pc, "left");
    var status = S.status[S.page] || "";
    var statusX = CONTENT_X + textWidth(page ? page.name.toUpperCase() : "", 10.5, true) + 18;
    if (status !== "") text(status, statusX, TITLE_Y, Math.max(0, S.statusRight - statusX - 10), 18, 8.5, false, C.muted, "left");
    fillRect(CONTENT_X, TITLE_Y + 21, W - CONTENT_X - 6, 1, C.divider);

    paintGraph(S.graphRect, pc);
    for (var b = 0; b < S.panelBoxes.length; ++b)
        text(S.panelBoxes[b].title.toUpperCase(), S.panelBoxes[b].x, BODY_Y, S.panelBoxes[b].w, 10, 7.5, true, pc, "left");

    for (var i = 0; i < S.items.length; ++i) paintItem(S.items[i], pc);
    paintMeter();
    if (S.noteUntil > 0 && S.note !== "")
        text(S.note, S.noteRect.x, 4, Math.max(0, S.noteRect.right - S.noteRect.x), 16, 8.5, false, C.muted, "left");
    if (S.menu) paintMenu(S.menu);
}

function paintAbsent(W) {
    var cy = (HEADER_H + H) / 2;
    var title, detail;
    if (S.state === "waiting") { title = "Starting\u2026"; detail = ""; }
    else if (S.deviceOnTrack === 0) {
        title = "dooblr-prophet not found on this track";
        detail = "Add the dooblr-prophet VST3 (0.3.0 or later) to this track, after this device.";
    } else if (S.deviceOnTrack < 0) {
        title = "dooblr-prophet is before this device";
        detail = "Move this device in front of dooblr-prophet so its MIDI reaches the plug-in.";
    } else if (S.state === "syncing") {
        title = "Connecting to dooblr-prophet\u2026"; detail = "";
    } else {
        title = "dooblr-prophet is not answering";
        detail = "It needs version 0.3.0 or later. Linking resumes on its own when it answers.";
    }
    text(title, 0, cy - 16, W, 18, 12, true, C.text, "centre");
    if (detail !== "") text(detail, 0, cy + 4, W, 16, 9, false, C.muted, "centre");
}

function paintItem(it, pc) {
    switch (it.type) {
    case "tab":
        var sel = it.page === S.page;
        fillRound(it.x, it.y, it.w, it.h, 4, sel ? mix(C.raised, S.pages[it.page].colour, 0.28) : C.raised);
        if (sel) strokeRound(it.x, it.y, it.w, it.h, 4, S.pages[it.page].colour, 0.55);
        text(S.pages[it.page].name, it.x, it.y, it.w, it.h, 9, true, sel ? C.text : C.muted, "centre");
        break;
    case "button":
        var enabled = it.action === "undo" ? S.canUndo : it.action === "redo" ? S.canRedo : 1;
        var tint = it.action === "random" ? mix(C.raised, C.accent, 0.3) : C.raised;
        fillRound(it.x, it.y, it.w, it.h, 3, tint);
        text(it.label, it.x, it.y, it.w, it.h, 8.5, true, enabled ? (it.action === "random" ? C.text : C.muted) : C.muted, "centre", enabled ? 1 : 0.4);
        break;
    case "presetbox":
        paintComboBox(it.x, it.y, it.w, it.h, S.presetName, false);
        break;
    case "output":
        paintOutput(it);
        break;
    case "seg":
        paintSegmented(it, pc);
        break;
    case "knob":
        paintKnob(it, pc);
        break;
    case "toggle":
        paintToggle(it);
        break;
    case "combo":
        var p = P(it.id);
        text(it.caption, it.x + 2, it.y + 1, it.w - 6, 10, 7.5, false, C.muted, "left");
        paintComboBox(it.x + 2, it.y + 12, it.w - 8, 16, p ? p.meta.choices[choiceIndex(p)] || p.val.t : "", isDim(it.id));
        break;
    case "picker":
        var pp = P(it.id);
        var names = S.fxNames[it.module] || [];
        var dimmed = isDim(it.module === 0 ? "echoPresetPicker" : "reverbPresetPicker");
        text(it.caption, it.x + 2, it.y + 1, it.w - 6, 10, 7.5, false, C.muted, "left");
        paintComboBox(it.x + 2, it.y + 12, it.w - 8, 16, pp ? names[choiceIndex(pp)] || pp.val.t : "", dimmed);
        break;
    case "vpresets":
        var vp = P("voices");
        var current = vp ? Math.round(vp.meta.lo + vp.val.n * (vp.meta.hi - vp.meta.lo)) : -1;
        var vdim = isDim("voicesPreset");
        text(it.caption, it.x + 2, it.y + 1, it.w - 6, 10, 7.5, false, C.muted, "left");
        it.buttons = [];
        for (var i = 0; i < it.counts.length; ++i) {
            var bx = it.x + 2 + i * 25, by = it.y + 13;
            it.buttons.push({ x: bx, y: by, w: 23, h: 16, n: it.counts[i] });
            var on = current === it.counts[i];
            fillRound(bx, by, 23, 16, 3, on ? C.divider : C.raised, vdim ? 0.45 : 1);
            text(String(it.counts[i]), bx, by, 23, 16, 8.5, true, on ? C.text : C.muted, "centre", vdim ? 0.45 : 1);
        }
        break;
    }
}

function paintComboBox(x, y, w, h, label, dimmed) {
    var a = dimmed ? 0.55 : 1;
    fillRound(x, y, w, h, 3, C.raised, a);
    strokeRound(x, y, w, h, 3, C.divider, a);
    text(label, x + 5, y, w - 18, h, 8.5, false, C.text, "left", a);
    // chevron
    colour(C.muted, a); mgraphics.set_line_width(1.2);
    var cx = x + w - 8, cy = y + h / 2;
    mgraphics.move_to(cx - 3, cy - 1.5); mgraphics.line_to(cx, cy + 1.5); mgraphics.line_to(cx + 3, cy - 1.5); mgraphics.stroke();
}

function knobAngle(n) { return Math.PI * 1.2 + n * Math.PI * 1.6; } // JUCE's default rotary range

function paintKnob(it, pc) {
    var p = P(it.id);
    if (!p) return;
    var dimmed = isDim(it.id);
    text(it.caption, it.x, it.y + 1, it.w, 10, 7.5, false, C.muted, "centre");
    var cx = it.x + it.w / 2, cy = it.y + 23, r = 11;
    var lw = 2, ar = r - lw / 2;
    var start = knobAngle(0), end = knobAngle(1), angle = knobAngle(clamp(p.val.n, 0, 1));
    var origin = knobAngle(p.meta.lo < 0 && p.meta.hi > 0 ? p.meta.origin : 0);
    mgraphics.set_line_cap("round");
    colour(C.divider); mgraphics.set_line_width(lw);
    mgraphics.arc(cx, cy, ar, start - Math.PI / 2, end - Math.PI / 2); mgraphics.stroke();
    if (!dimmed && Math.abs(angle - origin) > 0.001) {
        colour(pc); mgraphics.set_line_width(lw);
        mgraphics.arc(cx, cy, ar, Math.min(origin, angle) - Math.PI / 2, Math.max(origin, angle) - Math.PI / 2); mgraphics.stroke();
    }
    colour(C.raised); mgraphics.ellipse(cx - r * 0.6, cy - r * 0.6, r * 1.2, r * 1.2); mgraphics.fill();
    line(cx + r * 0.15 * Math.sin(angle), cy - r * 0.15 * Math.cos(angle),
         cx + r * 0.55 * Math.sin(angle), cy - r * 0.55 * Math.cos(angle), dimmed ? C.muted : C.text, 1, 2);
    mgraphics.set_line_cap("butt");
    var box = { x: it.x + 3, y: it.y + 37, w: it.w - 6, h: 11 };
    if (p.meta.kind !== 2) strokeRound(box.x, box.y, box.w, box.h, 1, C.text, dimmed ? 0.25 : 0.5);
    var size = 8;
    while (size > 6 && textWidth(p.val.t, size, true) > box.w - 2) size -= 0.5; // squeeze long readouts
    text(p.val.t, box.x + 1, box.y, box.w - 2, box.h, size, true, dimmed ? C.muted : C.text, "centre");
}

function paintToggle(it) {
    var p = P(it.id);
    if (!p) return;
    var on = p.val.n >= 0.5, dimmed = isDim(it.id);
    var px = it.x + 3, py = it.y + 2.5, pw = 22, ph = 11;
    fillRound(px, py, pw, ph, 5.5, on ? C.accent : C.divider, on ? (dimmed ? 0.4 : 0.9) : 1);
    colour(C.text, 0.85);
    var kx = on ? px + pw - 5.5 : px + 5.5;
    mgraphics.ellipse(kx - 4, py + ph / 2 - 4, 8, 8); mgraphics.fill();
    text(it.caption, px + pw + 4, it.y, it.w - pw - 10, it.h, 8.5, false, dimmed ? C.muted : C.text, "left");
}

function paintSegmented(it, pc) {
    var p = P(it.id);
    if (!p) return;
    text(it.caption, it.x - textWidth(it.caption, 8, true) - 8, it.y, textWidth(it.caption, 8, true), it.h, 8, true, C.muted, "right");
    var r = it.h / 2;
    fillRound(it.x, it.y, it.w, it.h, r, C.panel);
    strokeRound(it.x, it.y, it.w, it.h, r, brighter(C.grid, 0.12));
    var n = it.labels.length, sel = Math.round(p.val.n * (n - 1)), sw = (it.w - 4) / n;
    for (var i = 0; i < n; ++i) {
        var sx = it.x + 2 + i * sw;
        if (i === sel) {
            fillRound(sx, it.y + 2, sw, it.h - 4, r - 2, pc, 0.92);
            text(it.labels[i], sx, it.y, sw, it.h, 8.5, true, C.background, "centre");
        } else {
            text(it.labels[i], sx, it.y, sw, it.h, 8.5, true, C.muted, "centre", 0.7);
        }
    }
}

function paintOutput(it) {
    var p = P("output");
    if (!p) return;
    text("Output", S.outputCaptionX, it.y, 50, it.h, 9, false, C.muted, "left");
    var ty = it.y + it.h / 2, x0 = it.x + 5, x1 = it.x + it.w - 5;
    var tx = x0 + (x1 - x0) * clamp(p.val.n, 0, 1);
    fillRound(x0 - 2, ty - 2, x1 - x0 + 4, 4, 2, C.divider);
    fillRound(x0 - 2, ty - 2, tx - x0 + 2, 4, 2, C.accent);
    colour(C.text); mgraphics.ellipse(tx - 5, ty - 5, 10, 10); mgraphics.fill();
    var b = S.outputBox;
    fillRect(b.x, b.y, b.w, b.h, C.background);
    strokeRound(b.x, b.y, b.w, b.h, 1, C.text, 0.6);
    text(p.val.t, b.x, b.y, b.w, b.h, 8.5, true, C.text, "centre");
}

function paintMeter() {
    var m = S.meterRect;
    for (var ch = 0; ch < 2; ++ch) {
        var x = m.x + ch * 9;
        fillRect(x, m.y, 7, m.h, C.divider);
        var db = 20 * Math.log(Math.max(1e-6, S.meter[ch])) / Math.LN10;
        var level = clamp((db + 60) / 66, 0, 1);
        if (level > 0) fillRect(x, m.y + m.h * (1 - level), 7, m.h * level, db > 0 ? C.dynamic : C.accent);
    }
}

//==============================================================================
// Graphs (data from /dp/g/*, the editor's own maths)

function plotFrame(r) {
    fillRect(r.x, r.y, r.w, r.h, C.background);
    for (var i = 1; i < 4; ++i) fillRect(r.x, Math.round(r.y + r.h * i / 4), r.w, 1, C.grid);
    fillRect(r.x, r.y + r.h - 1, r.w, 1, C.divider);
}

function curve(values, from, count, r, toY, hex, lw, fillTo) {
    colour(hex); mgraphics.set_line_width(lw || 1.5);
    for (var i = 0; i < count; ++i) {
        var x = r.x + r.w * i / (count - 1), y = toY(values[from + i]);
        if (i === 0) mgraphics.move_to(x, y); else mgraphics.line_to(x, y);
    }
    if (fillTo === undefined) { mgraphics.stroke(); return; }
    mgraphics.stroke_preserve();
    mgraphics.line_to(r.x + r.w, fillTo); mgraphics.line_to(r.x, fillTo); mgraphics.close_path();
    colour(hex, 0.12); mgraphics.fill();
}

function paintGraph(r, pc) {
    if (!r) return;
    switch (S.page) {
    case 0: graphOsc(r, pc); break;
    case 1: graphFilter(r, pc); break;
    case 2: graphEnv(r, pc); break;
    case 3: graphLfo(r, pc); break;
    case 4: graphVoices(r, pc); break;
    case 5: graphFx(r, pc); break;
    }
}

function graphOsc(r, pc) {
    var g = S.graphs.osc;
    var half = (r.w - 8) / 2;
    var plots = [{ x: r.x, y: r.y + 10, w: half, h: r.h - 10 }, { x: r.x + half + 8, y: r.y + 10, w: half, h: r.h - 10 }];
    var colours = [pc, C.spectrum];
    for (var k = 0; k < 2; ++k) {
        var p = plots[k];
        text("OSC " + (k + 1), p.x, r.y, 40, 9, 7, true, colours[k], "left");
        plotFrame(p);
        if (!g) continue;
        var n = g[0] + 1;
        var toY = function (v) { return p.y + 3 + (p.h - 6) * (1 - (v + 1) / 2); };
        curve(g, 1 + k * n, n, p, toY, colours[k], 1.5);
    }
}

var FILTER = { minHz: 20, maxHz: 20000, minDb: -48, maxDb: 24, handle: 4.5 };
function filterPlot(r) { return { x: r.x, y: r.y, w: r.w, h: r.h }; }
function hzToX(hz, p) { return p.x + p.w * Math.log(hz / FILTER.minHz) / Math.log(FILTER.maxHz / FILTER.minHz); }
function xToHz(x, p) { return FILTER.minHz * Math.pow(FILTER.maxHz / FILTER.minHz, clamp((x - p.x) / p.w, 0, 1)); }
function resoToY(res, p) { var i = FILTER.handle + 1; return p.y + p.h - i - (p.h - 2 * i) * clamp(res, 0, 1); }
function yToReso(y, p) { var i = FILTER.handle + 1; return clamp((p.y + p.h - i - y) / (p.h - 2 * i), 0, 1); }

function graphFilter(r, pc) {
    var p = filterPlot(r);
    fillRect(p.x, p.y, p.w, p.h, C.background);
    var dbToY = function (db) { return p.y + p.h * (FILTER.maxDb - db) / (FILTER.maxDb - FILTER.minDb); };
    var marks = [100, 1000, 10000];
    for (var i = 0; i < marks.length; ++i) fillRect(Math.round(hzToX(marks[i], p)), p.y, 1, p.h, C.grid);
    for (var db = -36; db <= 12; db += 12) fillRect(p.x, Math.round(dbToY(db)), p.w, 1, db === 0 ? C.divider : C.grid);
    var g = S.graphs.flt;
    if (!g) return;
    var n = g[0] + 1;
    curve(g, 5 + n, n, p, dbToY, C.spectrum, 1.5);
    curve(g, 5, n, p, dbToY, pc, 1.5, p.y + p.h);
    var handles = [[g[3], g[4], C.spectrum, "hpf"], [g[1], g[2], pc, "lpf"]];
    for (var h = 0; h < 2; ++h) {
        var hx = hzToX(handles[h][0], p), hy = resoToY(handles[h][1], p);
        var hot = S.drag && S.drag.type === "filtergraph" && S.drag.which === handles[h][3];
        colour(handles[h][2]); mgraphics.ellipse(hx - FILTER.handle, hy - FILTER.handle, FILTER.handle * 2, FILTER.handle * 2); mgraphics.fill();
        colour(C.text, hot ? 1 : 0.8); mgraphics.set_line_width(1.2);
        mgraphics.ellipse(hx - FILTER.handle, hy - FILTER.handle, FILTER.handle * 2, FILTER.handle * 2); mgraphics.stroke();
    }
}

function graphEnv(r, pc) {
    var p = { x: r.x, y: r.y + 10, w: r.w, h: r.h - 10 };
    text("FILTER", p.x, r.y, 40, 9, 7, true, C.dynamic, "left");
    text("AMP", p.x + 44, r.y, 30, 9, 7, true, pc, "left");
    plotFrame(p);
    var g = S.graphs.env;
    if (!g) return;
    var n = g[0] + 1, span = g[1], keyUp = g[2];
    var kx = p.x + p.w * clamp(keyUp / span, 0, 1);
    for (var y = p.y; y < p.y + p.h; y += 4) fillRect(Math.round(kx), y, 1, 2, C.muted, 0.6);
    var toY = function (v) { return p.y + 2 + (p.h - 4) * (1 - v); };
    curve(g, 4, n, p, toY, C.dynamic, 1.3);
    curve(g, 4 + n, n, p, toY, pc, 1.5, p.y + p.h - 1);
}

function graphLfo(r, pc) {
    var p = { x: r.x, y: r.y, w: r.w, h: r.h };
    plotFrame(p);
    var g = S.graphs.lfo;
    if (!g) return;
    var n = g[0] + 1;
    var toY = function (v) { return p.y + 4 + (p.h - 8) * (1 - (v + 1) / 2); };
    curve(g, 2, n, p, toY, pc, 1.5);
    if (S.live) {
        var y = toY(clamp(S.live[0], -1, 1));
        fillRect(p.x, Math.round(y), p.w, 1, pc, 0.35);
        colour(C.text); mgraphics.ellipse(p.x + p.w - 7, y - 3, 6, 6); mgraphics.fill();
    }
}

var NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
function graphVoices(r, pc) {
    var g = S.graphs.voc, live = S.live;
    var maxVoices = g ? g[1] : 10, inUse = g ? g[0] : 0;
    var gap = 4, w = (r.w - gap * (maxVoices - 1)) / maxVoices;
    for (var i = 0; i < maxVoices; ++i) {
        var x = r.x + i * (w + gap), used = i < inUse;
        fillRound(x, r.y, w, r.h, 3, used ? C.panel : C.background);
        strokeRound(x, r.y, w, r.h, 3, used ? C.divider : C.grid);
        var note = live ? live[4 + i] : -1, level = live ? live[4 + maxVoices + i] : 0;
        if (used && note >= 0 && level > 0) {
            var bh = (r.h - 16) * clamp(level, 0, 1);
            fillRound(x + 3, r.y + r.h - 3 - bh, w - 6, bh, 2, pc, 0.7);
            text(NOTE_NAMES[note % 12] + (Math.floor(note / 12) - 1), x, r.y + 2, w, 10, 7, true, C.text, "centre");
        }
    }
}

function graphFx(r, pc) {
    var p = { x: r.x, y: r.y + 10, w: r.w, h: r.h - 10 };
    var g = S.graphs.fx;
    text("ECHO", p.x, r.y, 30, 9, 7, true, C.dynamic, "left", g && g[2] ? 1 : 0.4);
    text("REVERB", p.x + 34, r.y, 40, 9, 7, true, pc, "left", g && g[1] ? 1 : 0.4);
    plotFrame(p);
    if (!g) return;
    var span = g[0], reverbOn = g[1], pre = g[3], np = g[4];
    var cy = p.y + p.h / 2, amp = p.h / 2 - 3;
    fillRect(p.x, Math.round(cy), p.w, 1, C.divider);
    if (reverbOn) {
        colour(pc); mgraphics.set_line_width(1.2);
        var xOf = function (t) { return p.x + p.w * clamp(t / span, 0, 1); };
        mgraphics.move_to(xOf(pre), cy);
        for (var i = 0; i <= np; ++i) mgraphics.line_to(xOf(pre + (span - pre) * i / np), cy - amp * g[5 + i]);
        for (var j = np; j >= 0; --j) mgraphics.line_to(xOf(pre + (span - pre) * j / np), cy + amp * g[5 + j]);
        mgraphics.close_path();
        mgraphics.stroke_preserve();
        colour(pc, 0.18); mgraphics.fill();
    }
    for (var k = 5 + np + 1; g[2] && k + 2 < g.length; k += 3) {
        var tx = p.x + p.w * clamp(g[k] / span, 0, 1);
        if (g[k] > span) break;
        line(tx, cy, tx, cy - g[k + 1] * amp * clamp(g[k + 2], 0, 1), C.dynamic, 1, 2);
    }
}

function paintMenu(m) {
    fillRound(m.x, m.y, m.w, m.h, 4, C.raised);
    strokeRound(m.x, m.y, m.w, m.h, 4, C.divider);
    for (var i = 0; i < m.cells.length; ++i) {
        var c = m.cells[i];
        if (c.header) { text(c.label, c.x + 4, c.y, c.w - 8, c.h, 7.5, true, C.muted, "left"); continue; }
        if (c.selected) fillRound(c.x, c.y, c.w, c.h, 2, C.accent, 0.35);
        if (S.hover === c) fillRound(c.x, c.y, c.w, c.h, 2, C.accent, 0.2);
        text(c.label, c.x + 6, c.y, c.w - 10, c.h, 8.5, false, C.text, "left");
    }
}

//==============================================================================
// Menus (drawn inside the device: jsui has no native pop-up menu)

function openMenu(entries, columnWidth) {
    var W = mgraphics.size[0];
    var m = { x: NAV_W + 4, y: HEADER_H + 2, w: W - NAV_W - 10, h: H - HEADER_H - 5, cells: [], entries: entries, offset: 0, colW: columnWidth || 130 };
    S.menu = m;
    layoutMenu(m);
    redraw();
}

function layoutMenu(m) {
    var rowH = 13, rows = Math.floor((m.h - 8) / rowH), cols = Math.max(1, Math.floor((m.w - 8) / m.colW));
    var capacity = rows * cols, list = m.entries.slice(m.offset);
    var more = list.length > capacity;
    if (more) list = list.slice(0, capacity - 1);
    m.cells = [];
    for (var i = 0; i < list.length; ++i) {
        var e = list[i];
        m.cells.push({ x: m.x + 4 + Math.floor(i / rows) * m.colW, y: m.y + 4 + (i % rows) * rowH, w: m.colW - 4, h: rowH,
                       label: e.label, header: e.header, selected: e.selected, run: e.run });
    }
    if (more || m.offset > 0) {
        var k = list.length;
        var next = more ? m.offset + capacity - 1 : 0;
        m.cells.push({ x: m.x + 4 + Math.floor(k / rows) * m.colW, y: m.y + 4 + (k % rows) * rowH, w: m.colW - 4, h: rowH,
                       label: more ? "More \u203a" : "\u2039 Back to start", run: function () { m.offset = next; layoutMenu(m); S.menu = m; } });
    }
}

function presetMenu() {
    var entries = [{ label: "FACTORY", header: true }];
    for (var i = 0; i < S.factory.length; ++i)
        entries.push({ label: S.factory[i], selected: S.program === i, run: (function (k) { return function () { send("/dp/program", k); }; })(i) });
    if (S.user.length > 0) {
        entries.push({ label: "USER", header: true });
        for (var j = 0; j < S.user.length; ++j)
            entries.push({ label: S.user[j], selected: S.presetName === S.user[j] && S.program < 0,
                           run: (function (k) { return function () { send("/dp/userpreset", k); }; })(j) });
    }
    openMenu(entries, 130);
}

function choiceMenu(id) {
    var p = P(id);
    if (!p) return;
    var sel = choiceIndex(p), entries = [];
    for (var i = 0; i < p.meta.choices.length; ++i)
        entries.push({ label: p.meta.choices[i], selected: i === sel,
                       run: (function (k) { return function () { send("/dp/set", p.idx, choiceNorm(p, k)); }; })(i) });
    openMenu(entries, 110);
}

function fxMenu(module, id) {
    var p = P(id), names = S.fxNames[module] || [], sel = p ? choiceIndex(p) : -1, entries = [];
    for (var i = 0; i < names.length; ++i)
        entries.push({ label: names[i], selected: i === sel, run: (function (k) { return function () { send("/dp/fxpreset", module, k); }; })(i) });
    openMenu(entries, 130);
}

//==============================================================================
// Mouse

function hit(x, y) {
    for (var i = S.items.length - 1; i >= 0; --i) {
        var it = S.items[i];
        if (x >= it.x && x < it.x + it.w && y >= it.y && y < it.y + it.h) return it;
    }
    return null;
}

function onclick(x, y, but, cmd, shift) {
    if (S.state !== "linked") return;
    if (S.menu) {
        var m = S.menu;
        S.menu = null;
        for (var i = 0; i < m.cells.length; ++i) {
            var c = m.cells[i];
            if (!c.header && c.run && x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) { c.run(); break; }
        }
        redraw();
        return;
    }
    var it = hit(x, y);
    if (!it) return;
    var p;
    switch (it.type) {
    case "tab": S.page = it.page; S.menu = null; layout(); break;
    case "button":
        if (it.action === "undo" && S.canUndo) send("/dp/undo");
        if (it.action === "redo" && S.canRedo) send("/dp/redo");
        if (it.action === "random") send("/dp/random");
        if (it.action === "save") send("/dp/save");
        if (it.action === "load") send("/dp/load");
        break;
    case "presetbox": presetMenu(); break;
    case "seg":
        p = P(it.id);
        if (p) send("/dp/set", p.idx, choiceNorm(p, clamp(Math.floor((x - it.x - 2) / ((it.w - 4) / it.labels.length)), 0, it.labels.length - 1)));
        break;
    case "toggle":
        p = P(it.id);
        if (p) send("/dp/set", p.idx, p.val.n >= 0.5 ? 0 : 1);
        break;
    case "combo": choiceMenu(it.id); break;
    case "picker": fxMenu(it.module, it.id); break;
    case "vpresets":
        for (var b = 0; it.buttons && b < it.buttons.length; ++b) {
            var bb = it.buttons[b];
            if (x >= bb.x && x < bb.x + bb.w && y >= bb.y && y < bb.y + bb.h) send("/dp/voices", bb.n);
        }
        break;
    case "knob":
    case "output":
        p = P(it.id);
        if (!p) break;
        S.drag = { type: "knob", idx: p.idx, x: x, y: y, start: p.val.n, local: p.val.n, horizontal: it.type === "output", item: it };
        if (it.type === "output") {
            // A click on the track jumps there, like the editor's slider.
            var n = clamp((x - it.x - 5) / (it.w - 10), 0, 1);
            S.drag.start = S.drag.local = n;
        }
        send("/dp/begin", p.idx);
        if (it.type === "output") { send("/dp/set", p.idx, S.drag.local); S.val[p.idx].n = S.drag.local; }
        break;
    case "filtergraph":
        startFilterDrag(x, y);
        break;
    }
    redraw();
}

function startFilterDrag(x, y) {
    var g = S.graphs.flt;
    if (!g) return;
    var p = filterPlot(S.graphRect);
    var best = null, bestD = 10;
    var handles = [["lpf", g[1], g[2]], ["hpf", g[3], g[4]]];
    for (var i = 0; i < 2; ++i) {
        var d = Math.sqrt(Math.pow(x - hzToX(handles[i][1], p), 2) + Math.pow(y - resoToY(handles[i][2], p), 2));
        if (d < bestD) { bestD = d; best = handles[i][0]; }
    }
    if (best === null) return;
    var cut = P(best + "Cutoff"), res = P(best + "Reso");
    if (!cut || !res) return;
    S.drag = { type: "filtergraph", which: best, cut: cut.idx, res: res.idx };
    send("/dp/begin", cut.idx);
    send("/dp/begin", res.idx);
}

function ondrag(x, y, but, cmd, shift) {
    var d = S.drag;
    if (!d) return;
    if (d.type === "filtergraph") {
        var p = filterPlot(S.graphRect);
        if (but) {
            send("/dp/setplain", d.cut, xToHz(x, p));
            send("/dp/setplain", d.res, yToReso(y, p));
        } else {
            send("/dp/end", d.cut);
            send("/dp/end", d.res);
            S.drag = null;
        }
        redraw();
        return;
    }
    if (but) {
        var n;
        if (d.horizontal) n = clamp((x - d.item.x - 5) / (d.item.w - 10), 0, 1);
        else n = clamp(d.start + (d.y - y) / (shift ? 600 : 150), 0, 1);
        if (n !== d.local) {
            d.local = n;
            S.val[d.idx].n = n;
            send("/dp/set", d.idx, n);
        }
    } else {
        send("/dp/end", d.idx);
        S.drag = null;
    }
    redraw();
}

function ondblclick(x, y) {
    if (S.state !== "linked" || S.menu) return;
    var it = hit(x, y);
    if (!it) return;
    if (it.type === "knob" || it.type === "output") {
        var p = P(it.id);
        if (p) send("/dp/default", p.idx);
    }
}

function onidle(x, y) {
    if (!S.menu) return;
    var over = null;
    for (var i = 0; i < S.menu.cells.length; ++i) {
        var c = S.menu.cells[i];
        if (!c.header && x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) over = c;
    }
    if (over !== S.hover) { S.hover = over; redraw(); }
}

function onidleout() { if (S.hover) { S.hover = null; redraw(); } }

function onresize() { layout(); redraw(); }
