# dooblr plugin suite: design system and engineering guide

Use this guide when you build a new dooblr plugin. Every hex code and number below is copied from source code. Each one carries a file reference.

- Read from **dooblr-kick v0.3.5** (`~/Dropbox/dooblr_plugins/dooblr-kick`, short form **K**) and **dooblr-q v0.8.0** (`~/Dropbox/dooblr_plugins/dooblr-q`, short form **Q**).
- `K:Source/UI/Theme.h:9` means line 9 of that file in dooblr-kick.
- Snapshot taken 2026-10-27. If the code changes, the code wins over this document.
- The machine-readable tokens are in `dooblr-design-tokens.json`, in the same folder.
- **Addendum 2026-10-28:** components and tokens read from **dooblr-vocals v0.2.0** (`~/Documents/programming/audio/dooblr-vocals` (moved 2026-10-28 to `~/Dropbox/dooblr_plugins/dooblr-vocals`), short form **V**) were added. Nothing from the K/Q snapshot was changed; where vocals departs from a rule below, the rule stands and the deviation is listed in §11.4. See §14 for the changelog. **Addendum 2026-10-31:** §12 adds generic, reusable patterns first built in dooblr-soothe 0.1.1–0.1.2 (short form **S**). **Addendum 2026-11-01:** §13 documents dooblr-feedback 0.1.1 (short form **F**).

## Table of contents
1. [Suite identity](#1-suite-identity)
2. [Visual language](#2-visual-language)
   - 2.1 Palette · 2.2 Band / layer colours · 2.3 Meter colours · 2.4 Typography · 2.5 Spacing and sizes · 2.6 Radii · 2.7 Strokes · 2.8 Glows, alphas and dimming · 2.9 Control styles and states
3. [Graphs and visualizers](#3-graphs-and-visualizers)
4. [Layout and responsiveness](#4-layout-and-responsiveness)
5. [Interaction conventions](#5-interaction-conventions)
6. [Engineering conventions](#6-engineering-conventions)
7. [Product rules](#7-product-rules)
8. [New-plugin checklist and folder template](#8-new-plugin-checklist-and-folder-template)
9. [Appendix: token table](#9-appendix-token-table)
10. [Open questions / not determinable from code](#10-open-questions--not-determinable-from-code)
11. [dooblr-vocals (V)](#11-dooblr-vocals-v)
12. [Reusable patterns (S)](#12-reusable-patterns-s)
13. [dooblr-feedback (F)](#13-dooblr-feedback-f)
   - 13.1 Layout · 13.2 Notch markers · 13.3 Calm labels · 13.4 Notch table · 13.5 IIR notch bank · 13.6 Room notes and presets · 13.7 Safety Duck badge · 13.8 Tokens
14. [Changelog](#14-changelog)

---

## 1. Suite identity

| Item | dooblr-q | dooblr-kick | Source |
|---|---|---|---|
| Product name (hyphenated, lowercase) | `dooblr-q` | `dooblr-kick` | `CMakeLists.txt` `PRODUCT_NAME` |
| CMake target (CamelCase) | `DooblrQ` | `DooblrKick` | `CMakeLists.txt` `juce_add_plugin` |
| Version | 0.8.0 | 0.3.5 | `project(... VERSION ...)` |
| Company | `dooblr` | `dooblr` | `COMPANY_NAME` |
| Bundle ID | `com.dooblr.dooblrq` | `com.dooblr.dooblrkick` | `BUNDLE_ID` |
| Manufacturer code | `Dblr` | `Dblr` | `PLUGIN_MANUFACTURER_CODE` |
| Plugin code | `Dblq` | `Dbkk` | `PLUGIN_CODE` |
| VST3 category | `Fx EQ Dynamics` | `Instrument Drum` (IS_SYNTH, MIDI in) | `VST3_CATEGORIES` |
| C++ namespace | `dooblrq::…` (`dooblrq::ui`) | `dooblrkick::…` (`dooblrkick::ui`, `::dsp`, `::presets`) | `Source/UI/Theme.h` |
| Subtitle in header | "Dynamic parametric EQ" (version shown in a separate right-hand label) | "v" + `JucePlugin_VersionString` (since v0.3.4 the version is the subtitle) | Q `PluginEditor.cpp:67-68`, K `PluginEditor.cpp:436` |

**Naming scheme for a new plugin "foo":**
- Product `dooblr-foo`, target `DooblrFoo`, namespace `dooblrfoo`.
- Bundle `com.dooblr.dooblrfoo`, manufacturer code `Dblr`, a new unique 4-char plugin code starting `Db…`.
- CMake options and cache variables use the `DOOBLRFOO_` prefix, e.g. `DOOBLRKICK_JUCE_TAG`.

**Character:**
- Dark, flat, low-chroma UI with one teal accent and a few warm or cool secondary hues.
- Technical but calm.
- No skeuomorphism, no bitmaps, no drop shadows (see §2.8). Everything is drawn in vector form by a shared `DooblrLookAndFeel : juce::LookAndFeel_V4`.
- Kick's README cites D16 PunchBox and Sonic Academy Kick 3 as *inspiration* only and uses no material from them. Q carries a trademark disclaimer and uses none of the brands' artwork. Keep that rule: no third-party artwork, names only as compatibility references.

### 1.1 Third member: dooblr-vocals (added 2026-10-28)

| | dooblr-vocals (V) |
|---|---|
| Product / target | `dooblr-vocals` / `DooblrVocals` (`V:CMakeLists.txt`) |
| Version | 0.2.0 |
| Manufacturer / plugin code | `Dblr` / `Dbvx` |
| Bundle ID | `com.dooblr.vocals` (**deviates** from the `com.dooblr.dooblr<name>` scheme, see §11.4) |
| Formats | VST3 **and Standalone** (`DOOBLRVOCALS_PLUGIN_FORMATS "VST3;Standalone"`) |
| VST3 categories | `Fx PitchShift Dynamics`; not a synth, no MIDI |
| Namespaces | `dooblrvocals::ui`, `dooblrvocals::dsp`, `dooblrvocals::presets` |
| stateVersion | 1 (`V:Source/Parameters.h:12`) |
| User presets | `~/Music/dooblr-vocals/Presets`, extension `.dvocal` (`V:Source/Presets/Presets.h:33`) |

Details are in §11.

---

## 2. Visual language

### 2.1 Palette (`Palette::` in `Source/UI/Theme.h`; identical in both plugins)

| Token | Hex (ARGB) | Role | Ref |
|---|---|---|---|
| `background` | `0xff16181c` | Editor background, graph plots, dark text on coloured fills | K/Q `Theme.h:9` |
| `panel` | `0xff1d2026` | Header, nav column, controls area, Q response-curve backdrop, slide-toggle track | `Theme.h:10` |
| `panelRaised` | `0xff252931` | Buttons, combos, popups, text editors, knob centre cap, fader cap, tooltip/readout boxes | `Theme.h:11` |
| `divider` | `0xff2c3038` | 1 px separators, combo outline, knob track (unfilled arc), toggle-off pill, TextButton "on" colour | `Theme.h:12` |
| `grid` | `0xff262a31` | Graph grid lines, meter troughs, splitter rule | `Theme.h:13` |
| `textPrimary` | `0xffe6e8eb` | Values, titles, active handles, pointers, playhead | `Theme.h:14` |
| `textMuted` | `0xff8a919c` | Captions, labels, axis labels, disabled things | `Theme.h:15` |
| `accent` | `0xff5fb3a1` | Brand teal: header bar, knob fill default, EQ curve, waveform, toggles-on, Body amp curve | `Theme.h:16` |
| `dynamic` | `0xffd9a35b` | Dynamics / processing (FX) pages, Solo-on, Body pitch curve, Q "DYNAMICS" heading | `Theme.h:17` |
| `spectrum` | `0xff9fb4c8` | Analyzer spectrum, compressor "comp in" legend | `Theme.h:18` |
| `frozen` | `0xffe58fc0` | Frozen spectrum (Q), peak-hold line (K EQ) | `Theme.h:19` |
| `master` | `0xff8fa8e8` | **kick only**: Envelope page (master envelope, dry/wet) | K `Theme.h:20` |
| `fx` | `0xffb59be6` | **vocals only** (added 2026-10-28): Delay and Reverb (time-FX) pages | V `Theme.h:21` |

> Vocals note: `Theme.h:9-20` in V is byte-identical to the rows above. V adds `fx` (a genuinely new role: time-based effects) and reuses `master` for its **Harmony** page (the extra voices). The `master` row still describes kick; V's reuse is listed as a deviation in §11.4.

Derived colours used repeatedly:
- `divider.brighter(0.4)`: fader cap outline at rest.
- `divider.brighter(0.2)`: minor fader ticks; Q 0 dB grid line.
- `grid.brighter(0.25)`: slide-toggle outline.
- `grid.brighter(0.3)`: KEY-switch outline.

### 2.2 Band / layer colours (`bandColour(i)`, `Theme.cpp:8-9`, cycled by index)

| Index | Hex | Q (EQ band) | K (layer / page) |
|---|---|---|---|
| 0 | `0xff6fa8dc` | Band 1 | Click |
| 1 | `0xff5fb3a1` | Band 2 | Body |
| 2 | `0xffa3c46c` | Band 3 | Sub |
| 3 | `0xffd9a35b` | Band 4 | Tail |
| 4 | `0xffd9776b` | Band 5 | Noise |
| 5 | `0xffb08fd0` | Band 6 | n/a |

Kick page colours:
- Layer pages use `bandColour(layer)`.
- The Envelope page uses `master`.
- Drive, EQ, Dynamics and Limiter use `dynamic`.

Kick's EQ display labels its three bands with `bandColour(0..2)` on the handles.

### 2.3 Meter colours (kick header meter, `K:Source/UI/OutputMeter.cpp:11-13`)

| Token | Hex | Zone |
|---|---|---|
| `meterGreen` | `0xff5fbf8a` | below −6 dBFS |
| `meterYellow` | `0xffe2c25a` | −6 dBFS up to 0 dBFS |
| `meterRed` | `0xffe0574f` | at or above 0 dBFS; also the clip light |

Each zone blends over the 1 dB just below its threshold (`blendDb = 1`). The meter range is −48…+3 dB (`OutputMeter.h:24-26`).

### 2.4 Typography

The suite uses **JUCE's default sans-serif font** throughout. No custom typeface is embedded (no `Typeface`/`setDefaultSansSerif` anywhere). The single exception is Q's analyzer readout, which uses `Font::getDefaultMonospacedFontName()` at 11.5 pt (Q `PluginEditor.cpp:320`). Sizes are in JUCE font height units.

| Use | Size | Weight / colour | Ref |
|---|---|---|---|
| Plugin title (`JucePlugin_Name`) | 24 | bold, textPrimary | K `PluginEditor.cpp:434`, Q `:66` |
| Subtitle / "Output" caption | 13 | plain, textMuted | K `:436,438`, Q `:67,70` |
| Q version label | 12 | plain, textMuted, right-aligned | Q `PluginEditor.cpp:68` |
| Toggle (pill) label | 13 | textPrimary (textMuted disabled) | `Theme.cpp:179` |
| Knob / combo / fader captions | 12 | textMuted | K `ControlPanel.cpp:54,107,380`, Q `PluginEditor.cpp:405` |
| Section headings (ALL CAPS): "LAYERS", "PROCESSING", panel titles, "BAND n", "DYNAMICS" | 11 | bold; textMuted or the section colour | K `PluginEditor.cpp:1313`, `ControlPanel.cpp:370`, Q `PluginEditor.cpp:612` |
| Graph tooltips / readouts / waveform info | 11 | textPrimary or textMuted | K `EnvelopeEditor.cpp:1289`, `WaveformView.cpp:355` |
| Sample file name label | 11 | textMuted | K `PluginEditor.cpp:629` |
| Slide toggle / KEY / Solo "S" | 10.5 | bold | K `Switches.cpp:136,242`, `PluginEditor.cpp:206` |
| Q band-handle numbers | 10.5 | bold | Q `ResponseCurveComponent.cpp:758` |
| Q grid labels | 10.5 | textMuted | Q `ResponseCurveComponent.cpp:601` |
| Graph axis labels (kick) | 10 | textMuted | K `EnvelopeEditor.cpp:1225`, `OutputVisuals.cpp:48` (default) |
| Q spectrum dB labels | 9.5 | spectrum @ 0.45 | Q `ResponseCurveComponent.cpp:263-264` |
| Unit captions ("dB", "dBFS"), fader scale | 9 | textMuted / spectrum @ 0.55 | K `OutputVisuals.cpp:421-422`, `Theme.cpp:113` |
| Nav tab label | `jlimit(12, 15, height*0.40)` | textPrimary | K `PluginEditor.cpp:332,344` |

Text conventions:
- Section headings and graph titles are UPPERCASE, e.g. "BODY PITCH ENVELOPE", "MASTER ENVELOPE  +  DRY / WET".
- Captions are Title-case single words ("Level", "Point", "Time").
- Readouts put two spaces between fields, e.g. `"12.0 ms  440 Hz"` and `"<Band>  <Hz>  <dB>  Q 0.71"`.
- dB values are shown as `+x.x dB`, and silence below −119 dB is shown as `-inf dB` (K `OutputVisuals.cpp:33-40`).

### 2.5 Spacing and sizes (kick unless noted)

**Window**
- Minimum 940×600, default 1080×660, maximum 2400×1400.
  - K `PluginEditor.cpp:693-694`; Q `PluginEditor.h:52-57`.
  - The Q README says 980×640, but the code says 1080×660.

**Header** (K `PluginEditor.cpp:1337-1381`)
- 60 px tall, with contents inset 32 px horizontally and 10 px vertically.
- Fixed-size buttons, 24 px tall:
  - Undo and Redo: 50 px each, 4 px apart, followed by a 12 px gap.
  - Save and Load: 46 px each.
  - Random: 64 px, after an 8 px gap.
  - Export: 64 px.
  - Play: 30 px.
- Preset combo: 26 px tall, width 110 + up to 60 px.
- Title column: 120 + up to 52 px; the title takes the top 3/5 and the subtitle the bottom 2/5.
- Output slider: 88 + up to 24 px of spare width, then everything left over, capped at 200 px.
- Output caption: 52 px.
- Meter: 20 px wide, with a 10 px gap before Play.
- Q's header (Q `PluginEditor.cpp:623-645`):
  - Title column 172 px; version label 52 px.
  - Stereo-link toggle 112×24; phase-mode combo 136×26.
  - Q adds a toolbar row 38 px tall, inset 16 px horizontally and 6 px vertically: Spectrum 104, Source 100, Tilt 140, Clear 64, Freeze 76, Peak 70, readout 150 px.

**Nav column** (kick, `PluginEditor.cpp:1383-1420`)
- 150 px wide, inset 12 px.
- Group titles are 20 px tall, with a 10 px gap between the LAYERS and PROCESSING groups.
- Tab pitch is `max(28, (h − 2·20 − 10)/numPages)`, and the gap between tabs is `jlimit(2, 4, pitch/14)`.

**Controls area** (kick, `PluginEditor.cpp:1427-1455`)
- Height is `min(Σ panel heights + 10 each, + 14, totalHeight − 200)`, inset 20 px horizontally and 10 px vertically, with 10 px between panels.
- On layer pages the left column holds the level fader: `LevelFader::width` (56) + 12 px.
- Q (`PluginEditor.cpp:666-709`):
  - The controls area is a fixed 200 px tall, inset 20×12.
  - The band-select row is 40 px, inset 16×6.
  - The response curve fills the rest, inset 16×10.

**ControlPanel grid** (`K:Source/UI/ControlPanel.h:71`, `.cpp:202-347`)
- Panel title height 20.
- Knob cell 70×84; it shrinks to as little as **56** px so a panel stays on one row before it wraps.
- Combo width 128, toggle width 104, gap 6 px.
- Inside a cell:
  - Captions: 16 px tall.
  - Combo: 28 px row reduced by 1 px.
  - Toggle and extra controls: top trimmed 18 px, then 26 px tall.
- Knob text box: below the knob, knob width × 16. The knob diameter is capped by `maxKnobDiameter` = 46.

**Display area** (kick)
- Inset 16×10.
- Envelope/waveform split: splitter 12 px wide; minimum envelope width 340 and minimum waveform width 200 (`PluginEditor.h:147`, `Switches.h:128`).
- Title bar 24 px with a 180 px title area, then a 4 px gap.
- The waveform starts 28 px below the top.
- Mix display (Envelope page): `jlimit(72, 180, h·2/5)` tall, with a 6 px gap above it.

**Component sizes**
- Slide toggle 132×22 (`Switches.h:56`).
- KEY switch 56×62 with an 18×36 track (`Switches.h:86`, `.cpp:228-231`).
- Tab on/off switch 24×13 (K `PluginEditor.h:30`, Q `PluginEditor.h:21`).
- Solo 24 px column in the tab, drawn as an 18×16 box.
- Chain-drag handle area 26 px on the right of the tab.
- Envelope point-knob strip: `120 + 3·70 + 3·6` px (`ControlPanel.h:150`).
- K peak-hold button: 96 px wide in the title bar.

**Graph insets**
- K envelope plot: left 44, bottom 18, top 10, right 12 (`EnvelopeEditor.cpp:1192`), plus an 18 px time ruler under the plot (`EnvelopeEditor.h:156`).
- K EQ plot: left 34, right 30, top 6, bottom 18 (`OutputVisuals.cpp:372`).
- K waveform: `reduced(8, 22)` (`WaveformView.cpp:267`).
- History strips: left 30, top 4, bottom 14 (`OutputVisuals.cpp:792,1118`).

### 2.6 Radii

| Element | Radius | Ref |
|---|---|---|
| Toggle pill (28×14) | 7 | `Theme.cpp:171` |
| Tab switch / slide toggle / KEY track / thumbs | height/2 (fully round) | K `PluginEditor.cpp`, `Switches.cpp:123,134,247` |
| Fader groove (5 px) | 2.5 | `Theme.cpp:144` |
| Fader cap (12 px tall) | 3 | `Theme.cpp:154` |
| Solo box 18×16 | 3 | K `PluginEditor.cpp` (SoloButton) |
| Header meter body | 3; clip lights 1 | `OutputMeter.cpp:123,164` |
| Graph plot background | 3 | `EnvelopeEditor.cpp:1214` |
| Nav tab off-dim overlay | 4 | K `PluginEditor.cpp:325,348` |
| Readout boxes (Q width tooltip, K limiter readout) | 4 | Q `ResponseCurveComponent.cpp:807`, K `OutputVisuals.cpp:1143` |
| Q bandwidth handle pill 7×16 | 3 | Q `ResponseCurveComponent.cpp:794-798` |
| Splitter grip pill 8×34 | 4; rule 1.5 | `Switches.cpp:340-345` |
| Slot backdrop (Synth/Sample swap region) | 5 | `ControlPanel.cpp:366` |

### 2.7 Strokes

| Element | Width | Notes / Ref |
|---|---|---|
| Separators (header bottom, nav right, controls top) | 1 px fill | K `PluginEditor.cpp:1305-1308` |
| Brand bar (header, left) | 3 px wide, x+20, inset 14 px top and bottom, `accent` | K `:1310-1311`, Q `paint()` |
| Knob arc | `max(2, radius·0.12)`, rounded caps, curved joints | `Theme.cpp:56` |
| Knob pointer | 2 px, from 0.15r to 0.55r | `Theme.cpp:82-86` |
| Envelope curve | 2.0, curved/rounded | `EnvelopeEditor.cpp:1242` |
| Effective (scaled) master curve | 1.25 @ textPrimary 0.55 | `:1247-1248` |
| Selected-node ring | 1.2, diameter = node + 6 | `:1280` |
| Playhead line | 1.5 px rect @ 0.85·α | `:1343` |
| Waveform edges | 1.25, beveled/butt | `WaveformView.cpp:353` |
| Waveform centre line | exactly 1 physical pixel | `WaveformView.cpp:347` |
| K EQ curve / compressor curve | 2.0 | `OutputVisuals.cpp:579,950` |
| K spectrum / peak-hold line | 1.0 | `OutputVisuals.cpp:566,572` |
| Q EQ curve | 2.0 | Q `ResponseCurveComponent.cpp:682` |
| Q live dynamic curve | 1.8 | `:729` |
| Q frozen spectrum | 1.6; live spectrum 1.1; peak dashed 1.0 | `:240-259` |
| Q selected-handle ring | 1.2, handle + 3 px | `:764` |
| Outlines (toggle tracks, KEY, focus rings, fader cap) | 1.0 | `Switches.cpp:130,251,256`, `Theme.cpp:156` |
| Power glyph | 1.6 | K `PluginEditor.cpp:257-261` |
| KEY disabled slash | 1.2 | `Switches.cpp:273` |

### 2.8 Glows, alphas and dimming

**No shadows or `GlowEffect`s are used anywhere** (no `DropShadow`/`GlowEffect` in either code base). All "glow" comes from translucent fills:

- **Playhead glow** (K `EnvelopeEditor.cpp:1323-1358`):
  - The played region gets `textPrimary` @ 0.035·α.
  - A trail of 12 strips, each 2 px, behind the line, at alpha `0.09·fade²`.
  - The line is 1.5 px @ 0.85·α.
  - A dot on the curve: a 14 px halo in the curve colour @ 0.25·α and a 7 px core in textPrimary @ α.
  - α itself follows the playing level: −60…0 dB maps to 0.1…1.0 (`EnvelopeEditor.h:140`). It fades out over 300 ms after the hit (`playheadFadeMs`).
- **Curve fills under lines:**
  - Envelope: 0.10.
  - K EQ: 0.12 when on, 0.06 when off.
  - Q EQ: 0.08.
  - K spectrum fill 0.16 with a 0.55 line; Q live spectrum fill 0.11 with a 0.32 line; Q frozen fill 0.05 with a 0.8 line.
  - Q dynamic region: 0.32.
- **Slot backdrop:** the panel colour @ 0.06 (`ControlPanel.cpp:365`).
- **Disabled / off looks:**
  - Disabled toggle-on: accent 0.4. Disabled KEY switch: everything ×0.35 plus a slash. Disabled slide-toggle thumb: 0.35.
  - Off stage tab: a `background` overlay @ 0.35 and text alpha ×0.45.
  - Bypassed master envelope: curve ×0.4 (`EnvelopeEditor.cpp:1198`).
  - Q off band: a hollow `panel` disc with a textMuted @ 0.7 ring.
  - Disabled knob: no value arc and a textMuted pointer.
- **Selection highlights:**
  - Popup highlight: accent 0.35. Text selection: accent 0.4 (`Theme.cpp:21,32,42`).

### 2.9 Control styles and states

All controls are drawn by `DooblrLookAndFeel` (identical `Theme.cpp` in both plugins, apart from kick's extra vertical-fader override) or by small custom components.

**Rotary knob** (`Slider::RotaryHorizontalVerticalDrag`, `Theme.cpp:46-87`)
- Geometry: bounds reduced by 4; radius = `min(w, h, maxKnobDiameter − 8)/2`.
- Track: full arc in `rotarySliderOutlineColourId` (divider).
- Value arc: in `rotarySliderFillColourId`. Kick sets it to the page or band colour (`ControlPanel.cpp:34`); the default is accent. Bipolar ranges (min < 0 < max) fill from 0; unipolar ranges fill from the start.
- Centre cap: a `panelRaised` disc of diameter 1.2·r.
- Pointer: textPrimary.
- States:
  - Disabled: no value arc, textMuted pointer.
  - Hover/drag: no special drawing.
- Text box: below the knob, transparent background and outline, textPrimary text.

**Toggle button (pill switch)** (`Theme.cpp:161-182`)
- A 28×14 pill, radius 7, in a 30 px left column.
- On: accent @ 0.9 (0.4 disabled). Off: divider.
- Knob: 10×10, 7 px from the end; textPrimary, or ×0.85 when not hovered.
- Label: 13 pt, textPrimary (textMuted disabled), inset 6 px.

**Vertical fader** (kick `LevelFader`, `Theme.cpp:89-159`)
- Groove 5 px wide, radius 2.5, in the outline colour, filled from the bottom with `trackColourId` (the layer colour) when enabled.
- Cap: `min(30, w−4)`, or 22 with the `faderScale` property; 12 tall, radius 3, panelRaised.
- Cap outline 1 px: the fill colour on hover or drag, otherwise `divider.brighter(0.4)`.
- Grip line: `(capWidth−10)×1.5`, textPrimary (textMuted disabled).
- Scale with `faderScale`, at 9 pt:
  - Marks at +12, +6, 0, −6, −12, −24, −48 and the range start, drawn as "-∞".
  - The 0 dB mark is a 7 px tick in textMuted with a textPrimary label. Other marks are 4 px ticks in `divider.brighter(0.2)` with textMuted labels.
  - The scale shifts 5 px right to make room for labels.
- Without `faderScale`: ticks every 12 dB, 9 px long at 0 and 5 px otherwise, in divider.
- Taper (README): the top 75% covers −24…+12 dB with 0 dB at 78%; the next 15% covers −48…−24; the bottom 10% covers −48…off. Level range −60…+12 dB (`Parameters.h:82`).

**Horizontal output slider** (header)
- `TextBoxRight` 70×20; track accent @ 0.7, background divider; double-click returns to 0 dB (Q `PluginEditor.cpp:75-79`, kick equivalent).

**TextButton** (Undo, Redo, Save, Load, Random, Export, Q toolbar)
- Background panelRaised, "on" colour divider, text textMuted when off and textPrimary when on (`Theme.cpp:33-36`). Corners use the JUCE V4 default.
- Q Freeze-on uses frozen @ 0.35 (Q `PluginEditor.cpp:294`).

**ComboBox / PopupMenu**
- Background panelRaised, outline divider, text textPrimary, arrow textMuted.
- Popup background panelRaised, highlight accent @ 0.35 (`Theme.cpp:26-32`).
- Kick presets combo has section headings "Factory" and "User".

**Tab on/off switch** (`LayerTabSwitch`, 24×13; K and Q `PluginEditor.cpp`)
- Fully round pill.
- On: band colour @ 1.0 when hovered, 0.9 otherwise; the knob is `background`.
- Off: background @ 0.6 with a 1 px textMuted outline (0.8 hovered, 0.45 otherwise); the knob is textMuted (0.9 hovered, 0.6 otherwise).
- Knob size is height − 4.

**Solo button** (kick)
- An 18×16 box, radius 3; `dynamic` when on, background @ 0.6 when off; "S" in bold at 10.5 pt.

**Processor power button** (kick FX tabs)
- A circle that shrinks by 0.6 px when pressed.
- On: page colour @ 1.0 when hovered, 0.92 otherwise.
- Off: background @ 0.65 with a textMuted outline (0.85 hovered, 0.5 otherwise).
- Glyph: an arc from 40° to 320° plus a stem, stroke 1.6, inset 0.27·width (`PluginEditor.cpp:241-261`).

**Nav tab** (kick `NavButton`)
- Label size is `jlimit(12, 15, h·0.4)`.
- Off stage: a background @ 0.35 overlay with radius 4, and text ×0.45. The tab stays selectable.
- Chain number badge: a 16 px circle with a bold 10 pt number.
- Drag handle: a 26 px area on the right showing a 12×10 "hamburger" (3 bars, 1.6 px tall) at 0.95 alpha on hover and 0.55 otherwise, with `DraggingHandCursor`.

**Slide toggle** (kick Pitch | Amp, `Switches.cpp:68-197`)
- Size 132×22. The track is reduced by 1, in `panel`, with a 1 px outline: `grid.brighter(0.25)`, or the thumb colour @ 0.9 when focused.
- Thumb: half the track width, reduced by 2, fully rounded, tinted by interpolating between the left and right colours (dynamic for Pitch, accent for Amp) @ 0.9 (0.35 disabled).
- Labels: 10.5 bold. The label under the thumb is `background`, the other one textMuted, cross-interpolated with the thumb position.
- Thumb animation: 180 ms ease-out cubic. A drag needs 4 px before it moves the thumb.
- Keys: Space/Return toggle; Left/Right pick a side.
- It is a view setting only: no undo, not automatable.

**KEY switch** (kick, `Switches.cpp:200-275`)
- Size 56×62. "KEY" caption 10.5 bold, in the layer colour when on, otherwise textMuted.
- Vertical 18×36 track: grid blended to colour @ 0.55 by thumb position; outline `grid.brighter(0.3)`, or textMuted on hover.
- Thumb is up when on: knob = track width − 4, textPrimary when up and textMuted when down; it shrinks 0.75 when pressed.
- Animation 140 ms, ease-out cubic.
- Focus ring: 2 px outside the track, colour @ 0.8.
- Disabled: alpha 0.35 plus a diagonal slash.
- It is a `ToggleButton` bound with `ButtonAttachment`, so it is automatable and undoable.

**Play icon button** (kick)
- No text, tooltip "Play".
- Triangle: h = min(box)/2, w = 0.9h, nudged right by 0.08w; stroked 1.2 with rounded joins.
- Colour: textPrimary, accent on hover, `accent.darker(0.2)` when pressed, textMuted @ 0.4 when disabled.

**Splitter bar** (kick)
- 12 px wide, with `LeftRightResizeCursor`.
- Rule: 1 px grid, or 3 px accent @ 0.35 when hot.
- Grip: an 8×34 `panel` pill with 5 dots of 2.6 px, 6 px apart; accent when hot, textMuted @ 0.8 otherwise.
- Double-click resets.

**Header output meter** (kick)
- 20 px wide, `panel` body with radius 3.
- Two bars with a 2 px gap; the trough is grid. The lit part is painted one pixel row at a time in that row's zone colour (no gradient allocation).
- 0 dBFS tick: textMuted @ 0.5.
- Peak-hold line: 1.5 px, the zone colour `.brighter(0.2)`.
- Clip lights 4 px tall, latching red until clicked.
- Ballistics: instant attack, 26 dB/s release, 700 ms hold, then 18 dB/s hold decay (`OutputMeter.h:24-26`).

**Q band handles**
- Radius 8.
- Enabled: band colour (1.0 when selected, 0.8 otherwise) with the number in background colour, 10.5 bold.
- Disabled: hollow.
- The selected handle gets a ring 3 px outside it.
- Bandwidth pills: 7×16 at 0.9, joined by a dashed line (2/3 pattern) @ 0.35.

**K EQ handles**
- Radius 7, or 8 when active, with a background ring 3 px wider.
- Letter label (L/M/H initial) in background colour at 10 pt.

**Envelope nodes**
- Radius 5 (hit radius 9, line-hit 7), with a background-coloured ring 2 px wider.
- Active nodes use textPrimary.
- Curve-tension diamond: 8×8, textPrimary when active, textMuted @ 0.7 otherwise.
- The hover/drag readout is 11 pt in a 130×16 box.

**Tooltips**
- Always plain sentences that name the shortcut, e.g. "Undo (Cmd-Z)", "Redo (Cmd-Shift-Z)" (Ctrl-Z/Ctrl-Y elsewhere), "Play", "Drag to resize; double-click to reset".
- Parameter controls use `parameter->getName(64)` as the tooltip.


#### Vocals components (V, added 2026-10-28; adoptable suite-wide)

These are generic. Any dooblr plugin may reuse them; copy from V and keep the numbers. Tokens: `components.*`, `colorRecipes.*`, `layout.vocals` in the JSON.

**SegmentedToggle** (`V:ControlPanel.h:102-147`, `ControlPanel.cpp:411-566`): a large, parameter-bound N-way switch for a *mode* the user must never miss (V: KEYED | CHROMATIC on the Pitch page).
- Bound with `juce::ParameterAttachment`, so it is automatable. A selection is one `setValueAsCompleteGesture` (one undo step). Clicking the active segment, or any segment while disabled, does nothing.
- This is the parameter counterpart of the slide toggle. The slide toggle stays a view setting (no undo, not automatable); use the SegmentedToggle when the value *is* a parameter.
- Commit on mouse-up inside the pressed segment (drag off to cancel). Only the pressed segment shows hover while the button is down.
- Keys: Left/Right pick a side; Space/Return toggle (two segments). Wants keyboard focus; `PointingHandCursor`. Accessibility role `toggleButton`, title = labels joined with " / ", value = the active label, `valueChanged` notification on change.
- Size: minimum 236×32. Pitch-bar placement: height `jlimit(34, 40, h/16)`, width `jlimit(244, 300, bar/3)`, then a 14 px gap.
- Paint: track reduced 0.5, radius `min(8, 0.3·h)`, fill `panel`, 1 px outline `grid.brighter(0.35)` (`colour @ 0.9` when focused). Segments equal width, reduced 3, radius −2. Active: `colour @ 0.92` (1.0 hovered, 0.4 disabled) with `background` text. Inactive: no fill, `textMuted @ 0.7` text; hovered `colour @ 0.16` fill (0.30 pressed) and `textPrimary` text. Label font `jlimit(13, 17, 0.40·h)` bold, kerning 0.06 (the first use of kerning in the suite).

**Dimmed (ignored) control** (`V:ControlPanel.cpp:190-197`, `PluginEditor.cpp:612-626`): `ControlPanel::setDimmed(id, dimmed, alpha)` sets the alpha of the control *and* its caption.
- 0.38 + disabled: the current mode ignores the control entirely.
- 0.6 + enabled: the current mode ignores it but something else still uses it (V: Key/Scale in CHROMATIC while Harmony is on still set the harmony scale).
- The tooltip always says why, e.g. "Chromatic: ignored (switch to KEYED to use it)". Restore the parameter-name tooltip when it is live again.

**Vertical level + GR meter** (`V:OutputVisuals.h` `LimiterMeter`, `OutputVisuals.cpp:1098-1262`): a section meter, distinct from the header meter.
- Range −36…+12 dB; GR bar 0…24 dB. Level bar 24 px, GR bar 12 px, 10 px apart. Body `panel` r4; troughs `background` r2; trim top 24, bottom 20, sides 8.
- Same ballistics and zone colours as the header meter (§2.3). GR readout release 12 dB/s, snapping to 0 above −0.05 dB.
- Ticks 3 px in `grid`; dB labels 9.5 pt, GR labels 9 pt, `textMuted`; 0 dB tick `textMuted @ 0.5`. Peak hold 2 px in the zone colour `.brighter(0.2)`.
- Ceiling: `frozen` 1.5 px when the limiter is on, `textMuted @ 0.7` dashed (3/3 `fillRect`s) when off. GR fills `dynamic` from the top, r1.5.
- Values 10 pt (peak `accent`, GR `dynamic`); captions "OUT"/"GR" 9 pt.
- Repaints only when its picture hash changes.

**Learn / armed button** (`V:PluginEditor.cpp:465-470,684-720`): a TextButton that arms a capture.
- While armed the text becomes "Stop · <live note>", the fill pulses `frozen @ (0.35 + 0.45·phase)` with `phase = 0.5 + 0.5·sin(ms·0.006)` (≈1.05 s period) and the text is `textPrimary`; a component property `armed` is set. Disarmed: colours removed (L&F default).
- A 12 pt `textPrimary` label beside it narrates progress ("Listening - sing low to high, then Stop (x s voiced)"); errors use `dynamic`. Width 100; a small Reset button 54×22 follows.

**Voice strip** (`V:HarmonyDisplay.cpp:133-272`): one row per voice/layer.
- `panelRaised` r6 (@ 0.6 when the voice is off) with a 3 px left bar in the page colour (@ 0.35 off). Title "VOICE n" 11 pt bold in the page colour (`textMuted` off). Knob captions 12 pt `textMuted`; knob fill page colour (@ 0.4 off).
- Min height 76, header 132, knob cell 72, gap 6. Pan track 2 px `divider` with a centre tick, L/R 10 pt, 9 px dot.
- Controls carry componentID = parameter ID.

**Time-FX displays** (`V:FxDisplays.cpp:1-232`): reverb envelope (early-reflection bars, tail fill @ 0.22 + 1.4 px line @ 0.85, highs `spectrum @ 0.65`, ducking `dynamic @ 0.9`, wet band `fx @ 0.35` r4) and delay taps (3 px bars, L above / R below centre, alpha `0.35 + gain`). Background fill, inner `panel` r6 reduced 10/8, ≤ 8 time-grid lines with 10 pt labels at `textMuted @ 0.8`, 11 pt readout top-right, 10.5 pt legend. Off state: `background @ 0.55` overlay r6 with a 13 pt bold `textMuted` sentence.

**FX routing switch** (`V:FxDisplays.h:59-86`, `FxDisplays.cpp:332-449`): three labelled segments (Parallel / Delay > Reverb / Reverb > Delay), 24 px tall under a 16 px 12 pt "Routing" caption, width 210, r4. On: `fx @ 0.9`, bold 11 pt `background` text. Off: `panelRaised` (`.brighter(0.15)` hovered), `textPrimary @ 0.85` 11 pt. A small signal-flow sketch below: 44×15 `panelRaised` boxes with `fx` outlines, r3, 10 pt text, `textMuted` arrows. One undo step per change. (Interaction differs from the SegmentedToggle, see §11.4.)
*Update 2026-10-28 (`29dac9a`): the routing switch is now a `SegmentedToggle` and the time-FX off state has no sentence (dimming only); see §11.4.*

**Vibrato centre line** (`V:PitchDisplay.cpp:252-282`): when vibrato is detected (confidence > 0.25) the pitch graph draws the vibrato centre as a 1 px dashed (4/3) `textPrimary @ 0.55` line between detected (`spectrum @ 0.9`, 1.25 px) and corrected (`accent`, 2 px). The readout appends "VIB <rate> Hz ±<depth>¢".

**Latency readout** (`V:PluginEditor.cpp:488-494,575-589`): 12 pt `textPrimary` label, width 118, right after the latency selector: "<mode> · <ms> ms" (1 decimal under 10 ms). Shows what the host is compensating.

**Section preset menu** (`V:PluginEditor.cpp:443-454,650-663`): a ComboBox, width 124, empty text "Presets", on a section's control row. Loads only that section's parameters (prefix `dly_` / `rev_`) as one undo step and reports "Delay: <name>" in the status line.

---

## 3. Graphs and visualizers

General rules, from both code bases:

- **Opaque and pre-rendered.**
  - Graph components `setOpaque(true)` and `fillAll(Palette::background)`. Exception: Q's response curve uses `panel` and a 1 px divider border (`ResponseCurveComponent.cpp:655,814`).
  - Static grids and labels are cached into an `Image` at the display's physical pixel scale (`pixelScaleFor`, clamped to 1…4; K `OutputVisuals.cpp:17-31`).
- **No allocation in paint().**
  - Paths are rebuilt only when a parameter hash, the size or the pixel scale changes.
  - Gradients and dashed strokes are avoided in paint because they allocate. Use alpha strips or per-row fills instead (K `OutputMeter.cpp:140`, `EnvelopeEditor.cpp:1328`, `:1246`).
- **Display-synced.**
  - Animation and meter polling use `juce::VBlankAttachment`. Q's analyzer/curve uses a 60 Hz timer (Q `ResponseCurveComponent.cpp:54`).
  - Editor housekeeping timers run at 15 Hz (kick, `PluginEditor.cpp:696`) and 30 Hz (Q, `PluginEditor.cpp:227`).
  - When the component isn't showing, meter FIFOs are drained and discarded (`discardAll()`), and a page's view is reset when it is shown again.
- **Anti-aliasing / physical pixels** (kick waveform, `WaveformView.cpp:256-353`):
  - Peaks are decimated to one min/max pair per *physical* pixel column, up to 4096 columns, at sub-pixel column centres.
  - Minimum band thickness is half a physical pixel. The upper and lower edges are joined with straight segments (one vertex per physical column is finer than any curve fit), filled at accent 0.55 and stroked at 0.95.
  - Rendering of the hit itself runs on a low-priority worker thread, "latest job wins" (`WaveformView.h:13-29`).
- **Grid:**
  - grid colour, with the 0 dB line in divider (K EQ) or `divider.brighter(0.2)` (Q).
  - Kick time grid steps: 1, 2, 5, 10, 25, 50, 100, 250, 500 or 1000 ms, aiming for ≤12 lines per view (`EnvelopeEditor.cpp:21-25`).
  - Frequency axis: log 20 Hz…20 kHz, with labels 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k (K `OutputVisuals.cpp:398`).
  - K EQ gain axis: ±24 dB with lines at −24, −12, 0, +12, +24; the spectrum axis is 0…−96 dBFS on the right in spectrum @ 0.55 (`:279,408-423`).
  - Q display: ±24 dB EQ range, spectrum 0…−90 dBFS (Q `ResponseCurveComponent.h:55-59`).
- **No help captions on envelope graphs.** Kick has a test, `testEnvelopeInfoTextRemoved`. Titles are uppercase and coloured with the curve colour.
  - The kick EQ display still draws a hint string ("post-EQ spectrum   drag: freq / gain …") at 10.5 pt in textMuted (`OutputVisuals.cpp:601`). The waveform also shows "OUTPUT  (click to play)". See §10.
- **Crossfade when the graph's content changes:**
  - The old picture fades out over 200 ms while sliding up to 10 px (`EnvelopeEditor.h:66`, `.cpp:1296-1302`).
  - Thumb animations last 180 ms (slide toggle) and 140 ms (KEY).

**Spectrum analyzers**
- **Q:**
  - FFT order 12 (4096), Hann window, hop fftSize/4 (75% overlap), a 32768-sample FIFO, floor −120 dB, live release 30 dB/s (Q `Analysis/SpectrumAnalyzer.h:20-66`).
  - Display: 480 log points with 1/12-octave smoothing (`SpectrumDisplay.h:27-28`).
  - Live rise 0.03 s, fall 0.16 s, overlay 0.25 s (`ResponseCurveComponent.h:140-141`).
- **Kick:** FFT order 12, hop 1024, floor −120, release 30 dB/s (K `DSP/SpectrumAnalyzer.h:21-22`). The EQ curve and spectrum are one vertex per plot pixel column.
- **Dynamics/limiter history** (kick):
  - Scrolling history strips. Dynamics range −60…+6 dB; limiter −36…+12 dB (`OutputVisuals.cpp:725,1083`).
  - Dot ballistics: instant attack, 40 dB/s release. Limiter readout: 12 dB/s for gain reduction, 20 dB/s for peak (`:884-888,1194-1196`).

**Envelope editor (kick)**
- Plot inset 44/18/10/12 with an 18 px ruler.
- Pitch axis: log2, 20 Hz…16 kHz (`EnvelopeEditor.cpp:16-17`).
- Up to 8 nodes (`DSP/Envelope.h:7`); node times up to 2000 ms (6000 ms for master) (`Parameters.h:48-50`).
- Minimum view span 20 ms; default view 600 ms.
- Fit: `lastMs·1.08 + 4`, minimum 50 ms (`.cpp:351`).
- Auto-fit animation: 200 ms ease-out cubic (`.h:117`, `.cpp:413`).
- Curve colours:
  - Body pitch: dynamic.
  - Body amp: accent.
  - Master: master.
  - Other layers: their band colour.
- Effective master curve (when Time Scale or Depth isn't at 100%): a thin textPrimary @ 0.55 line on top.

---

## 4. Layout and responsiveness

**Resizing**
- Both editors are resizable between 940×600 and 2400×1400, with a default of 1080×660 (§2.5).
- Kick has `testLayoutFit` and Q its smoke tests, which check that nothing overlaps at the minimum size.

**Header**
- Fixed-width buttons; the title, preset combo and output slider share the flexible width.
- Spare width goes to the slider first (up to +24), then the title (up to +52), then the preset combo (up to +60), and whatever is left back to the slider, capped at 200 (K `PluginEditor.cpp:1344-1353`).
- The header is designed to fit from 940 px without overlap.

**Kick structure**
- Header across the top (60 px).
- Left nav column (150 px), grouped as LAYERS (Click, Body, Sub, Tail, Noise) and PROCESSING (Envelope, then the re-orderable chain Drive, EQ, Dynamics, Limiter).
- The bottom controls area sizes to the selected page's panels.
- Graphs fill the remaining area: envelope/graph on the left, a 12 px splitter, waveform on the right.
- The splitter ratio defaults to 0.6, clamps to 0.3…0.8 (`PluginProcessor.h:84`), is saved in plugin state, and resets on double-click. It never squeezes the envelope below 340 px or the waveform below 200 px.

**Q structure**
- Header (60 px), toolbar (38 px), response curve (fills), band-select row (40 px), controls (200 px).
- The controls area is split into EQ and DYNAMICS sections by a vertical divider.

**Panel flow (kick `ControlPanel`)**
- Items flow left to right, 6 px apart, and wrap to a new row when they don't fit.
- Before wrapping, knobs shrink from 70 to 56 px to keep the panel on one row.
- **Fixed slots** (`spec.slot`) reserve the same cell whether a control is shown or not. Source-specific controls (Synth vs Sample) therefore swap in place without moving anything else. A faint 0.06 backdrop marks that region.
- Hidden, unslotted items take no space, and the panel heights reflow.

**Nav tabs** share the column's height, at least 28 px each, so the column fills at any window size.

---

## 5. Interaction conventions

- **Knobs:**
  - Plain drag works horizontally or vertically. Double-click resets to the parameter default.
  - **Shift-drag is fine adjustment:** `setVelocityModeParameters(0.15, 1, 0.0, true, shiftModifier)`, `ControlPanel.cpp:43`.
  - Semitone tunes use a drag sensitivity of 720 (about 10 px per semitone, `:45-47`).
- **Faders / output slider:** double-click to default or 0 dB; Shift for fine adjustment.
- **Undo/redo:**
  - Cmd-Z, and Cmd-Shift-Z or Cmd-Y (Ctrl on other systems). The editor claims these keys so the host doesn't also undo (K `PluginEditor.cpp:1272-1293`). All other keys pass through to the host.
  - 50 steps. In Q, wheel ticks less than 0.6 s apart merge into one step (Q `UndoHistory.h:29-30`).
  - Every gesture is exactly **one** undo step, via `beginChangeGesture`/`endChangeGesture` or kick's `ParameterGesture`, including multi-parameter actions (stage power, chain drag).
  - Q deliberately gives the APVTS no UndoManager; the history is its own.
- **Tabs:** clicking selects the tab. On/off power toggles live *on* the tabs, and an off tab is dimmed but still selectable. Turning a stage back on restores its previous sub-state, kept in a state property (K `PluginEditor.cpp:889-968`).
- **Chain re-order (kick):** drag the hamburger handle. The dragged tab follows the mouse within the chain slots, and the whole drag is one gesture and one undo step.
- **Graph editing:**
  - Drag a node to move it; drag the segment diamond to change curve tension (120 px of drag = full range).
  - Double-click empty space to add a node; double-click or right-click a node to remove it.
  - Empty-space drag: after 4 px of movement the axis locks. Vertical zooms at 120 px per octave of span; horizontal pans.
  - Wheel and trackpad: Shift+wheel or horizontal swipe pans; Cmd/Ctrl+wheel or pinch zooms.
  - Double-click the time ruler to return to auto-fit. View changes are not parameters and have no undo.
- **Q EQ graph:** drag a handle for frequency/gain; wheel or Alt-drag for Q; double-click for 0 dB; bandwidth pills for width.
- **Clicking the waveform** auditions the sound. **Clicking the header meter** clears the clip lights.
- **Selectors:** slide toggle (Space/Return/Left/Right). Every custom control supplies an `AccessibilityHandler` with a role, title and description.
- **Cursors:**
  - `PointingHandCursor` on switches and icons.
  - `DraggingHandCursor` on drag handles.
  - `LeftRightResizeCursor` on splitters.
  - `UpDownLeftRightResizeCursor` on zoomable empty graph space.
- **File dialogs** are always async (`FileChooser::launchAsync`): a plugin must never block. Status messages go to a status line ("Saved …", "Could not load preset: …").
- `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` (kick CMake). Components that need keys request focus themselves.
- **Drag hygiene (V, added 2026-10-28; recommended suite-wide)** (`V` commit `66040e1`; `V:GateDisplay.cpp:186-252`, `OutputVisuals.cpp:1410-1477`):
  - The cursor always follows the current hover target.
  - `mouseExit` is ignored while a drag is in progress.
  - On `mouseUp`: if the pointer is still inside, recompute hover from the pointer; if it was released over another component, reset to `NormalCursor` and clear hover. This prevents stale drag cursors and highlights when the mouse crosses sections.
  - Horizontal threshold/ceiling lines grab within ±7 px, and the hit area extends 6 px above and below the plot, so a line parked at the floor or ceiling stays grabbable.
  - Keep the grab offset (pointer − line) for the whole drag so the line never jumps to the pointer.
  - V covers this with the "mouse across sections" test.

---

## 6. Engineering conventions

**Toolchain**
- CMake ≥ 3.22, C++20 (extensions OFF), Release by default.
- `CMAKE_OSX_DEPLOYMENT_TARGET "11.0"` is set **before** `project()`.
- arm64, VST3.
- JUCE **8.0.15** via `FetchContent` with `GIT_SHALLOW`, pinned in `DOOBLR<NAME>_JUCE_TAG`.
- Links `juce_recommended_config_flags`, `_lto_flags` and `_warning_flags`.
- Compile definitions `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`.
- `COPY_PLUGIN_AFTER_BUILD FALSE`: installation is the script's job.
- Resources go through `juce_add_binary_data` (kick samples).
- Both build caches currently point at `MacOSX26.5.sdk` with the Xcode toolchain and Unix Makefiles (read from `build/CMakeCache.txt`; see §10).

**Signing / quarantine**
- POST_BUILD strips quarantine, signs ad-hoc (`codesign --force --sign -`), then runs `codesign --verify --deep --strict`.
- Kick uses `cmake/sign-bundle.sh`: up to 8 attempts, removing `com.apple.quarantine`, `FinderInfo` and `ResourceFork`, with 0.5 s sleeps between tries.
- Kick also writes `artefact-paths/DooblrKick_VST3-$<CONFIG>.txt` so scripts can find the bundle.

**`scripts/build.sh`** (same in both repos)
- Environment variables: `BUILD_DIR`, `CONFIG=Release`, `RUN_TESTS=1`, `INSTALL_VST3=1`, `INSTALL_VST3_DIR`, `LOAD_CHECK=1`, `COPY_TO_PROJECT=0`, `JOBS`.
- Uses Ninja for a fresh build directory if it is installed, otherwise Unix Makefiles.
- Flow: configure, build `_VST3`, build LoadCheck and the tests, run `ctest --output-on-failure`, find the bundle through artefact-paths.
- Delivery:
  - Copies with `ditto --noqtn --noextattr`, removes quarantine and FinderInfo, and fails if quarantine remains.
  - Runs `diff -r -q` against the build output and `codesign --verify --deep --strict`.
  - Runs LoadCheck on the installed bundle, including a version check.
- Installs to `/Library/Audio/Plug-Ins/VST3`, or `~/Library/...` when only that copy exists. Never uses sudo. Warns when both copies exist.

**Real-time safety**
- No allocation or locks on the audio thread; kick tests this in `testRealtimeSafetyAndCpu`.
- Meters and spectra flow through lock-free FIFOs:
  - Kick `MeterTap`: spectrum capacity 2^15, frame capacity 2^11 (`DSP/MeterTap.h:47-48`).
  - Q analyzer FIFO: 2^15.
- The playhead is a single atomic (`PlayheadTap`, 47-bit sample count).
- The editor reads raw parameter atomics once and caches the pointers.

**Smoothing**
- Kick: about 10 ms, in blocks of 32 samples (`DSP/KickEngine.h:77-78`).
- Q: 30 ms, coefficients updated every 16 samples, 10 ms crossfades (README).
- Q's colour stage uses 4× polyphase IIR oversampling (README).

**State**
- APVTS parameters, plus a `stateVersion` property: kick 6 (`PluginProcessor.cpp:21`), Q 1 (`Parameters.h:15`).
- Older states are migrated in place (kick `testStateMigration`).
- **New parameters are appended. Never rename or reorder IDs.** Q comments each new ID with "added in x.y.z".
- View settings such as the splitter ratio and stage-resume bits live as state-tree properties, not parameters.

**Versioning**
- The UI version label comes from `JucePlugin_VersionString`, with component ID `"version"`.
- Tests check that it matches the CMake version.

**Tests**
- A JUCE unit-test executable built by default and run by ctest. Kick has about 55 test functions, e.g. `testNoDeadParameters`, `testLayoutFit`, `testUndoHistory`, `testGoldenRenders`.
- Kick `DOOBLRKICK_EDITOR_SNAPSHOT` writes editor snapshots.
- Tools: `Tools/LoadCheck` (both) and `Tools/FrameBench` (kick, APPLE only; per-page frame times while dragging).

**Manual validation** still needed (Q README): pluginval at strictness 10, the Steinberg VST3 validator, and a DAW pass (Ableton steps in the kick README).

**`.gitignore`**: `/build/`, `/build-*/`, `/cmake-build-*/`, `/out/`, IDE folders, `.DS_Store`, the local `/dooblr-<name>.vst3/` copy, `.sample-staging/`.

---

## 7. Product rules

- **No dead controls.** Every parameter must do something audible (kick `testNoDeadParameters`). A control that does nothing in the current mode is **disabled** (e.g. filter cutoff and resonance when the filter is off) or, for source-specific controls, **hidden** (since kick v0.3.2) without losing its value (K `PluginEditor.cpp:826-886`).
- **Power is on the tab.** A section that is off is dimmed and still editable. The kick README says editing an off section leaves it off. Note that the kick EQ hint text, "EQ off: drag a handle to switch it on", contradicts this (see §10).
- **One gesture, one undo step.** Every parameter change goes through the host-visible gesture API.
- **No help captions on graphs.** Use uppercase titles, tooltips and readouts that appear only on hover or drag.
- **Meters must read true.** Kick tests that gain reduction matches the DSP, and that the header meter turns red at 0 dBFS and its clip light latches.
- **Assets are CC0 or original.** Kick's 21 samples are documented in `Resources/Samples/LICENSES.md` and normalised to −1 dBFS peak. No third-party artwork or trademarks; brand names appear only as references.
- **Presets:** factory presets built in, user presets in `~/Music/dooblr-<name>/Presets` with a plugin-specific extension (kick `.dkick`, `Presets.h:47`). The combo shows "Factory" and "User" headings, with names sorted case-insensitively.
- **Export (kick):** 24-bit 48 kHz stereo WAV (README), always through an async save dialog.
- **Never block the message thread.** Rendering for display runs on a low-priority worker, and file dialogs are async.
- **Accessibility:** every custom control gets a title, a description and an accessibility handler.
- **Never hide a mode-ignored control; dim it and say why (V).** See "Dimmed (ignored) control" in §2.9.
- **Make a mode impossible to miss (V).** A mode that changes what other controls do gets a SegmentedToggle in the page's title bar, not a small toggle in a panel.
- **Section presets (V).** A section with many parameters may carry its own preset menu that loads only that section, as one undo step.

---

## 8. New-plugin checklist and folder template

### Folder template
```
dooblr-<name>/
├── CMakeLists.txt          # copy kick's: deployment target before project(), JUCE 8.0.15 FetchContent,
│                           # PRODUCT_NAME dooblr-<name>, COMPANY_NAME dooblr, BUNDLE_ID com.dooblr.dooblr<name>,
│                           # PLUGIN_MANUFACTURER_CODE Dblr, new PLUGIN_CODE Db??, VST3 only, COPY_PLUGIN_AFTER_BUILD FALSE,
│                           # JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0, POST_BUILD sign, artefact-paths
├── README.md               # features, build, install, Ableton steps, manual validation, disclaimer
├── .gitignore              # as §6
├── cmake/sign-bundle.sh    # retrying ad-hoc sign (copy from kick)
├── scripts/build.sh        # configure/build/test/deliver/install/LoadCheck (copy, rename)
├── Resources/              # binary data + LICENSES.md (CC0 / original only)
├── Source/
│   ├── PluginProcessor.{h,cpp}   # APVTS, stateVersion + migration, UndoHistory, taps
│   ├── PluginEditor.{h,cpp}      # header (60 px), layout, resize limits 940x600..2400x1400, default 1080x660
│   ├── Parameters.h              # IDs (append-only), ranges as inline constexpr
│   ├── UndoHistory.{h,cpp}       # 50 steps, gesture merging
│   ├── DSP/                      # engine, lock-free MeterTap / PlayheadTap
│   └── UI/
│       ├── Theme.{h,cpp}         # copy verbatim: Palette, bandColour, DooblrLookAndFeel (namespace dooblr<name>::ui)
│       ├── ControlPanel.{h,cpp}  # knob/combo/toggle flow grid with fixed slots
│       └── Switches.{h,cpp}      # SlideToggle, KeyTrackSwitch, PlayIconButton, SplitterBar
├── Tests/                  # CMakeLists.txt + <Name>Tests.cpp (JUCE UnitTest)
└── Tools/LoadCheck/        # (+ FrameBench for heavy UIs)
```

### Checklist
1. [ ] Choose a name, a 4-char plugin code (`Db??`, unique) and a VST3 category. Start at version 0.1.0.
2. [ ] Copy `Theme.h/.cpp` unchanged, renaming only the namespace. Add a new palette entry **only** for a genuinely new semantic role, as kick did with `master`. Never retint existing tokens.
3. [ ] Use `bandColour(i)` for per-band/per-layer identity, `dynamic` for processing/dynamics, `spectrum`/`frozen` for analyzers, and the meter colours for level meters.
4. [ ] Header: a 60 px `panel` strip, the 3 px accent bar at x+20, a 24 pt bold title, a 13 pt muted subtitle, Undo/Redo 50×24, an Output slider with TextBoxRight 70×20, and a version label with component ID `version`.
5. [ ] Resize limits 940×600…2400×1400, default 1080×660. Check that nothing overlaps at the minimum size (write a test).
6. [ ] All controls through `DooblrLookAndFeel`. Knobs at most 46 px in diameter in 70×84 cells, 12 pt captions, double-click reset, Shift for fine adjustment.
7. [ ] Graphs: opaque, cached background image at physical pixel scale, no allocation in paint, VBlank-driven, no help captions.
8. [ ] Animations: ease-out cubic, 140–200 ms (§2.9/§3).
9. [ ] Undo: one gesture per edit, 50 steps; Cmd-Z and Cmd-Shift-Z handled in `keyPressed`, all other keys passed to the host.
10. [ ] Real time: no allocation or locks on the audio thread; lock-free FIFOs to the UI; parameter smoothing.
11. [ ] State: `stateVersion` property, append-only IDs, migration test, state round-trip test.
12. [ ] Tests: instantiation/metadata, no dead parameters, editor layout fit, undo, real-time safety, state round-trip, version label matching CMake.
13. [ ] Build through `scripts/build.sh`: sign, verify, strip quarantine, LoadCheck, install without sudo.
14. [ ] Manual check: pluginval strictness 10, the VST3 validator, a DAW pass.
15. [ ] README: features, controls, shortcuts, build/install, licences, trademark disclaimer.

---

## 9. Appendix: token table

| Token | Value | Defined in |
|---|---|---|
| color.background | `0xff16181c` | K/Q `Source/UI/Theme.h:9` |
| color.panel | `0xff1d2026` | `Theme.h:10` |
| color.panelRaised | `0xff252931` | `Theme.h:11` |
| color.divider | `0xff2c3038` | `Theme.h:12` |
| color.grid | `0xff262a31` | `Theme.h:13` |
| color.textPrimary | `0xffe6e8eb` | `Theme.h:14` |
| color.textMuted | `0xff8a919c` | `Theme.h:15` |
| color.accent | `0xff5fb3a1` | `Theme.h:16` |
| color.dynamic | `0xffd9a35b` | `Theme.h:17` |
| color.spectrum | `0xff9fb4c8` | `Theme.h:18` |
| color.frozen | `0xffe58fc0` | `Theme.h:19` |
| color.master (kick) | `0xff8fa8e8` | K `Theme.h:20` |
| band[0..5] | `0xff6fa8dc`, `0xff5fb3a1`, `0xffa3c46c`, `0xffd9a35b`, `0xffd9776b`, `0xffb08fd0` | `Theme.cpp:8-9` |
| meter.green / yellow / red | `0xff5fbf8a` / `0xffe2c25a` / `0xffe0574f` | K `OutputMeter.cpp:11-13` |
| meter range / thresholds | −48…+3 dB; yellow −6, red 0, blend 1 dB | K `OutputMeter.h:24-25` |
| meter ballistics | hold 700 ms, release 26 dB/s, hold decay 18 dB/s | K `OutputMeter.h:26` |
| font.title | 24 bold | K `PluginEditor.cpp:434` |
| font.subtitle / caption | 13 | K `:436,438` |
| font.control caption | 12 | K `ControlPanel.cpp:54` |
| font.section heading | 11 bold | K `PluginEditor.cpp:1313` |
| font.switch label | 10.5 bold | K `Switches.cpp:136` |
| font.axis | 10 | K `EnvelopeEditor.cpp:1225` |
| font.unit / fader scale | 9 | K `Theme.cpp:113` |
| window min / default / max | 940×600 / 1080×660 / 2400×1400 | K `PluginEditor.cpp:693-694`, Q `PluginEditor.h:52-57` |
| header height | 60 | K `PluginEditor.cpp:1337`, Q `:623` |
| nav width (kick) | 150 | K `:1383` |
| Q toolbar / controls / band row | 38 / 200 / 40 | Q `PluginEditor.cpp:648,666,667` |
| panel title / knob cell / combo / toggle | 20 / 70×84 / 128 / 104 | K `ControlPanel.h:71` |
| min knob width / gap | 56 / 6 | K `ControlPanel.cpp:202` |
| maxKnobDiameter | 46 | K `ControlPanel.cpp:35` |
| level fader width | 56 | K `ControlPanel.h:112` |
| tab switch | 24×13 | K `PluginEditor.h:30`, Q `PluginEditor.h:21` |
| slide toggle | 132×22 | K `Switches.h:56` |
| KEY switch | 56×62 (track 18×36) | K `Switches.h:86`, `.cpp:231` |
| splitter width | 12 | K `Switches.h:128` |
| min envelope / waveform width | 340 / 200 | K `PluginEditor.h:147` |
| splitter default / min / max | 0.6 / 0.3 / 0.8 | K `PluginProcessor.h:84` |
| header meter width | 20 | K `PluginEditor.h:153` |
| toggle pill | 28×14, r 7; knob 10 | `Theme.cpp:169-175` |
| fader groove / cap | 5 w r 2.5 / ≤30 (22 scaled) × 12, r 3 | `Theme.cpp:117-156` |
| knob arc width | max(2, 0.12 r) | `Theme.cpp:56` |
| envelope node r / hit / line-hit | 5 / 9 / 7 | K `EnvelopeEditor.cpp:14` |
| Q handle radius | 8 | Q `ResponseCurveComponent.cpp:12` |
| anim.slideToggle | 180 ms ease-out cubic | K `Switches.h:14`, `.cpp:30` |
| anim.keySwitch | 140 ms | K `Switches.cpp:204` |
| anim.graphCrossfade | 200 ms, 10 px slide | K `EnvelopeEditor.h:66`, `.cpp:1299` |
| anim.autoFit | 200 ms ease-out cubic | K `EnvelopeEditor.h:117` |
| anim.playheadFade | 300 ms | K `EnvelopeEditor.h:126` |
| playhead alpha | floor 0.1 at −60 dB | K `EnvelopeEditor.h:140` |
| env minView / axisLock / zoom px-per-octave | 20 ms / 4 px / 120 | K `EnvelopeEditor.h:93,102`, `.cpp:15` |
| max envelope nodes | 8 | K `DSP/Envelope.h:7` |
| node time max / master max | 2000 / 6000 ms | K `Parameters.h:48,50` |
| master time scale | 0.25…4.0 | K `Parameters.h:57` |
| layer level range | −60…+12 dB | K `Parameters.h:82` |
| timers | kick editor 15 Hz; Q editor 30 Hz; Q curve 60 Hz | K `PluginEditor.cpp:696`, Q `PluginEditor.cpp:227`, `ResponseCurveComponent.cpp:54` |
| kick smoothing | 10 ms, block 32 | K `DSP/KickEngine.h:77-78` |
| FFT | order 12 (4096); Q hop 1024 (fft/4); floor −120; release 30 dB/s | Q `Analysis/SpectrumAnalyzer.h:61-66`, K `DSP/SpectrumAnalyzer.h:21-22` |
| Q spectrum display | 480 pts, 1/12 oct; rise 0.03 s, fall 0.16 s, overlay 0.25 s; 0…−90 dBFS; EQ ±24 dB | Q `SpectrumDisplay.h:27-28`, `ResponseCurveComponent.h:55-59,140-141` |
| kick EQ graph | 20 Hz…20 kHz, ±24 dB, spectrum 0…−96 dBFS | K `OutputVisuals.cpp:279` |
| waveform peaks / max columns | 4096 / 4096 | K `WaveformView.h:75-76` |
| MeterTap capacities | spectrum 2^15, frames 2^11 | K `DSP/MeterTap.h:47-48` |
| undo | 50 steps, 0.6 s merge (Q) | Q `UndoHistory.h:29-30` |
| stateVersion | kick 6, Q 1 | K `PluginProcessor.cpp:21`, Q `Parameters.h:15` |
| JUCE | 8.0.15 | `CMakeLists.txt` `*_JUCE_TAG` |
| macOS deployment target | 11.0 | `CMakeLists.txt` |
| Q ranges | freq 20…20000, gain ±24, Q 0.1…18, thresh −60…0, attack 0.1…200 ms, release 5…2000 ms, drive −12…+24, atten 0…18 | Q `Parameters.h:49-57` |
| `fx` (V) | `0xffb59be6` | V `Theme.h:21` |
| vocals window | default 1080×660, min 940×600, max 2400×1400; header 60, nav 150, meter 20 | V `PluginEditor.h:110` |
| vocals editor timer | 15 Hz + VBlank meters; undo merge 0.6 s | V `PluginEditor.cpp:520-521`, `UndoHistory.h:30` |
| SegmentedToggle | min 236×32; font jlimit(13,17,0.4h) bold; active @ 0.92 | V `ControlPanel.cpp:411-566` |
| dimmed control | 0.38 ignored (disabled), 0.6 partly ignored | V `PluginEditor.cpp:616-626` |
| limiter meter | −36…+12 dB, GR 24 dB; bars 24 / 12 px; GR release 12 dB/s | V `OutputVisuals.cpp:1098-1262,1354-1356` |
| learn pulse | 0.35 + 0.45·(0.5 + 0.5·sin(ms·0.006)), ≈1.05 s | V `PluginEditor.cpp:696-698` |
| line grab | ±7 px, plot hit area +6 px | V `GateDisplay.cpp:186-202` |
| stateVersion (V) | vocals 1 | V `Parameters.h:12` |

---

## 10. Open questions / not determinable from code

- **Xcode SDK workaround.** Neither repo's CMake or scripts set an SDK or sysroot. Both `build/CMakeCache.txt` files have `CMAKE_OSX_SYSROOT=…/MacOSX26.5.sdk`, the Xcode toolchain `c++` and Unix Makefiles. The Q README says it was tested with Apple clang 21 and the macOS 27 SDK. Whatever workaround exists lives in the configure command line or the environment, not in the repo. Ask Dan before relying on it.
- **Q default window size.** The code says 1080×660 (`PluginEditor.h:52-53`); the Q README says 980×640. Follow the code.
- **Hint text on graphs.** Kick's EQ display still draws a usage hint (`OutputVisuals.cpp:601-603`) and the waveform shows "OUTPUT  (click to play)". The no-caption rule is tested only for envelope graphs. It's unclear whether the rule covers all graphs.
- **"EQ off: drag a handle to switch it on"** (`OutputVisuals.cpp:602`) suggests a drag turns the EQ on. The README says editing an off section leaves it off. The intended behaviour couldn't be confirmed without running the plugin.
- **TextButton and ComboBox corner radii** come from JUCE `LookAndFeel_V4` defaults and are not overridden. The exact values are JUCE's, not dooblr's.
- **No typeface, shadow or glow tokens exist.** The suite uses the system default sans and draws "glow" from translucent fills (§2.8).
- **The waveform header comment** describes "quadratic segments", but the code draws straight segments, one per physical pixel column (`WaveformView.cpp:248-252`). The code is described here.
- **Kick header budget constant.** `fixedRight = meterWidth + 10 + 48 + 4 + 82 + 12` (K `PluginEditor.cpp:1344`) reserves 48 + 82 px, but the buttons actually laid out are Play 30 and Export 64 (`:1375-1377`). The budget is larger than the real buttons; the actual bounds are the ones documented here.
- **Windows/Linux and AU/AAX:** not configured in either CMake (VST3 on macOS arm64 only). Policy for other formats is unknown.
- **Bundle ID scheme for dooblr-vocals (added 2026-10-28).** V ships `com.dooblr.vocals`, not `com.dooblr.dooblrvocals`. Changing a shipped bundle ID can make hosts treat it as a new plugin, so the doc does not say whether V should be renamed.
- **Standalone format (added 2026-10-28).** V also builds Standalone. Whether other dooblr plugins should offer it is undecided.

---

## 11. dooblr-vocals (V)

Read from `~/Documents/programming/audio/dooblr-vocals` (moved 2026-10-28 to `~/Dropbox/dooblr_plugins/dooblr-vocals`) at v0.2.0 (commit `7c61e63`) on 2026-10-28. Code wins.

### 11.1 Identity and shell
- Identity: see §1.1. CMake options use the prefix `DOOBLRVOCALS_` (`BUILD_TESTS`, `PLUGIN_FORMATS`, `JUCE_TAG` 8.0.15).
- Window default 1080×660, resizable 940×600 to 2400×1400. Header 60, nav column 150, header meter 20 (`V:PluginEditor.h:110`). The header (undo/redo 50×24, preset combo, save/load 46, random 64, output caption 52) matches kick.
- Editor timer 15 Hz; meters on `VBlankAttachment`. Undo: own `UndoHistory`, 0.6 s merge.
- Presets: factory + user, combo with "Factory"/"User" headings, plus per-section menus on Delay and Reverb (§2.9). Ten factory programs (Init … Ballad Throw, Vocal Workstation '90s); the Harmony bus panel holds Level, Formant and Lo-Fi (standard knob/toggle).

### 11.2 Pages and colours
| Nav group | Pages | Page colour |
|---|---|---|
| CORRECTION | Pitch | `accent` |
| CORRECTION | Harmony | `master` (reused, §11.4) |
| PROCESSING (re-orderable, numbered badges) | Gate, EQ, Comp | `dynamic` |
| TIME FX | Delay, Reverb | `fx` (new) |
| OUTPUT (fixed, no badge or drag handle) | Limiter | `dynamic` |

Signal order: Pitch → Harmony → Gate / EQ / Comp (user order) → Delay / Reverb → output gain → Limiter. The limiter is always last, so the final output (wet included) stays under the ceiling and the limiter meter shows the true final output.

Power buttons on the tabs and the off-tab overlay follow kick exactly (`V:PluginEditor.cpp:89-210`). The only literal colours outside `Theme.h` are the band colours (`Theme.cpp:8-9`) and the header-meter zones (`OutputMeter.cpp:11-13`), and both equal the existing tokens.

### 11.3 Signature components
SegmentedToggle (KEYED | CHROMATIC), dimmed controls, learn-range button, voice strips, limiter meter + ceiling-drag history, time-FX displays, FX routing switch, vibrato centre line, latency readout and section preset menus. All are specified in §2.9 and in the JSON (`components`, `colorRecipes`, `layout.vocals`, `graphs.vocals*`).

### 11.4 Deviations from the rules in this guide (rules unchanged; listed for review)
1. **Bundle ID** `com.dooblr.vocals` breaks the `com.dooblr.dooblr<name>` scheme (§1).
2. **Formats:** VST3 + Standalone, where §1/§10 describe VST3 only.
3. **`master` reused for Harmony.** The palette says `master` is kick-only (Envelope). V uses it as the Harmony page colour.
4. **`frozen` reused** for the limiter ceiling line and the armed Learn pulse, which are not frozen-spectrum or peak-hold roles.
5. **Gradient in paint.** `LimiterMeter` fills its level bar with a `ColourGradient` built in `paint` (`V:OutputVisuals.cpp:1206-1218`). §3 says gradients are avoided in paint.
6. **Dashed stroke / path allocation in paint.** `PitchDisplay` builds its Paths and a `createDashedStroke` (vibrato centre) in every `paint`. The reverb and delay displays also build Paths in `paint` and have no cached background (§3, checklist item 7).
7. **Unconditional repaint.** Reverb/Delay displays `repaint()` on every 15 Hz timer tick, although their header comment says they repaint only on a change (`V:PluginEditor.cpp:989-992`).
8. **Help text on graphs.** The limiter history draws "time (newest right)   levels before the output gain". The time-FX off overlays say "Reverb off - switch it on with the Reverb tab's toggle". These break the §7 rule "No help captions on graphs"; it is the same open question as kick's EQ hint (§10).
9. **FX routing switch vs SegmentedToggle.** The routing switch commits on mouse-down and has no keyboard focus or accessibility handler. The SegmentedToggle commits on mouse-up and is fully accessible. §7 Accessibility is not met by the routing switch.
10. **Learn pulse animation** runs on the 15 Hz editor timer, not `VBlankAttachment` (§3 animation rule), so it steps visibly.
11. **Dead template code.** `Switches.h/.cpp` (SlideToggle, KeyTrackSwitch, PlayIconButton, SplitterBar) are compiled into V but not used.
12. **Stale comments** in `V:Theme.h`: `master` is still commented "Envelope page" and `fx` is commented as a v0.3 addition.

**Resolved 2026-10-28 in commit `29dac9a`** (items 1-4 stay open; the rules above are unchanged):
- 5. `LimiterMeter` draws its level bar as solid zone-coloured segments; no gradient is built in `paint`.
- 6. `PitchDisplay`, the reverb and the delay displays cache their Paths / background pictures and rebuild them only when their inputs or scale change.
- 7. The reverb and delay displays repaint only when a displayed value changes.
- 8. The limiter history caption and the time-FX off sentences are gone. The off state is shown by dimming only.
- 9. The FX routing switch reuses `SegmentedToggle`: commits on mouse-up as one undo step, Left/Right/Space keys, keyboard focus and an accessibility handler.
- 10. The Learn pulse is driven by `VBlankAttachment`.
- 11. `Switches.h/.cpp` were removed from V.
- 12. `V:Theme.h` comments now say `master` = Harmony page and `fx` = Delay / Reverb pages and the routing switch.

### 11.5 Key Follow strip (added 2026-10-28, commit `eddffb9`)
Pitch page only. A 28 px strip sits between the KEYED | CHROMATIC toggle row and the pitch display (4 px above, 6 px below), full display width (`V:UI/KeyFollowStrip.cpp`, `V:PluginEditor.cpp` `resized`).
- **Background:** `panel`, radius 4. No border.
- **Left to right:** Key Follow toggle pill (112) · gap 6 · key readout (98, 13 px bold) · confidence bar (56×4, radius 2) · gap 14 · Lock toggle pill (72) · gap 8 · "SENS" caption (34, 11 px bold, `textMuted`) · Sensitivity linear slider (92, no text box; track `accent`, background `divider`, thumb `textPrimary`) · gap 10 · Source combo (100: Auto / Sidechain / Vocal) · gap 10 · status text (rest, 12 px, `textMuted`, right-aligned).
- **Key readout:** "E major" / "A minor", or an em dash before any key is found. `accent` while following, `textPrimary` when a key is detected but Key Follow is off, `textMuted` with no key. When locked it shows the held key with a trailing " *".
- **Confidence bar:** `divider` track; fill `accent` (following) or `textMuted` (off) at alpha 0.9, width = confidence 0-1.
- **Status text** (one at a time): "No sidechain: enable it in the host", "Waiting for audio", "Holding (no signal)", "Off: detecting from <source>", "Listening to <source>", "Following <source>".
- **Lock** is disabled (standard disabled look) while Key Follow is off.
- **Key / Scale combos while following:** they are not changed (the parameter values are never overwritten, so automation is not fought). They use the dimmed-control half alpha (0.6, §2.8 / `dimmedControl.alphaHalf`) with the tooltip "Key Follow is using <key>; this is the fallback when Key Follow is off". The pitch display's in-key rows and the Harmony strips' "in Key Scale" label show the followed key.
- No new colour, font size, radius or alpha token: all values above already exist in §2.

### 11.6 Learn Range progress (added 2026-10-31)
Pitch page, in the toggle row: the learned-range label (12 px, right-aligned) is followed by a progress bar, then the reset button (`V:UI/LearnProgressBar.h`, `V:PluginEditor.cpp` `resized` / `updateLearnUi`).
- **Bar:** min(90, row width / 3) × 12 slot, 8 px gap before the reset button; a 6 px track, radius 3, `divider`. Mouse-transparent, componentID `learnProgress`. Hidden when no take has been started (idle).
- **Progress = voiced time, not wall-clock:** the audio thread adds a block to the voiced counter only when the detector reports a voiced pitch at the learner's confidence (≥ 0.6). Full = 8 s of voiced audio; the take then finishes on its own. The value is handed to the editor through atomics and can only grow during a take.
- **Fill colours:** listening `frozen` (same role as the armed Learn pulse, §11.4 item 4), alpha 0.5 while no voice has been heard for 0.3 s; done `accent` at full width; failed / timed out `dynamic` at the width reached.
- **Status line** (the same label): "Waiting for voice" / "Keep singing… N%" while listening; "Done - learned low <note>, high <note>" when done; "Timed out - <reason>" after 45 s without enough voice; the learner's own "Not enough voice…" text when stopped early. Label colour `textPrimary`, `dynamic` only in the failed state.
- No new colour, font size, radius or alpha token.

### 11.7 Compressor Knee (added 2026-10-31)
Comp page: a **Knee** knob after Ratio (`comp_knee`, 0–24 dB, default 6 dB — the fixed knee every earlier version used, so sessions and presets sound bit-identical). `V:DSP/Effects.cpp` `Compressor::staticReductionDb`, `V:UI/OutputVisuals.cpp` `DynamicsDisplay`.
- **Law:** quadratic soft knee centred on the threshold, width = Knee; 0 dB is a hard knee. Values above 24 dB are clamped.
- **Transfer curve:** the curve follows the Knee setting; the knee region (threshold ± Knee/2) is over-stroked 3 px in `textPrimary` at alpha 0.6 so the width is visible; the threshold handle sits on the knee-bent curve. The readout reads "<ratio>:1  knee <N> dB".
- No new colour, font size, radius or alpha token.

### 11.8 EQ high-pass (added 2026-10-31)
EQ page: a second panel, **High-pass**, after the EQ panel: HPF pill toggle (76 px), Freq knob, Slope combo (104 px). Parameters `eq_hpf_on` (default off), `eq_hpf_freq` (20–500 Hz, log, default 80 Hz), `eq_hpf_slope` (12 / 24 / 48 dB/oct, default 24). `V:DSP/Effects.cpp` `HighPass`, `ThreeBandEq`; `V:UI/OutputVisuals.cpp` `EqDisplay`.
- **This is a low-cut**, separate from the Low band, which stays a low shelf. Butterworth cascade (1 / 2 / 4 biquad sections), −3 dB at Freq. It runs first inside the EQ section, so EQ power off bypasses it too.
- **Curve:** when on, the high-pass is multiplied into the drawn EQ response, so the low-cut roll-off shows on the same curve in the EQ colour. No extra handle.
- Off is bit-identical to earlier versions (the HPF is not in the signal path at all).
- No new colour, font size, radius or alpha token.

### 11.9 Noise reducer (added 2026-11-01)
Pitch page: a third panel, **Noise reducer**, after Detector: Denoise pill toggle (96 px), Noise floor combo (Learned / Auto, 104 px), Amount knob (0–40 dB, default 12), Sensitivity knob (0–100 %, default 50), then a **Learn Noise** text button (100 px, same style as Learn Range; reads "Stop" while learning) and a status label (12 px, textPrimary). Parameters `nr_on` (default off), `nr_mode` (default Auto), `nr_amount`, `nr_sensitivity`. Learn Noise is plain state, not a parameter. `V:DSP/NoiseReducer.cpp`; `V:PluginEditor.cpp` `updateNoiseUi`.
- **Placement:** first in the engine, before pitch detection. The detector tracks a cleaner signal. The gate threshold works on the denoised floor. The compressor no longer lifts and pumps the hiss. The harmonizer grains no longer smear it.
- **DSP:** STFT with sqrt-Hann windows, 75 % overlap. Per-bin gain max(−Amount, 1 − T/P), where T = noise floor × sensitivity offset (+12 dB at 0 %, −6 dB at 100 %). The gains are smoothed over 3 bins and in time (opening in ~5 ms, closing in ~60 ms) against musical noise. **Learned:** the average PSD of a noise-only take. **Auto:** minimum statistics over ~1.5 s; uses the learned profile as its seed if there is one. The profile is saved with the session, not with presets.
- **Latency:** the FFT size. Tracking 256 (5.3 ms), Mix 1024, HQ 2048 at 48 kHz, doubled per doubling of the rate. It is added to the reported plug-in latency and to the Detector latency readout. Off: no latency, buffer untouched (bit-identical). Switching it on or off fades over 5 ms.
- **Status label:** "Off", "Off - profile stored", "No profile - click Learn Noise", "Learning noise... N s", or "Auto · −x dB" / "Learned · −x dB" (average reduction).
- **Short windows:** with three panels the Pitch page could squeeze the display at the 940 × 600 minimum. When a page's panels would leave the display less than 200 px (Pitch page: plus the 34 px key-follow strip), every panel on that page uses compact rows (`ControlPanel::compactKnobHeight` 68 px instead of 84). Otherwise the layout is unchanged.
- No new colour, font size, radius or alpha token.

### 11.10 Dynamic EQ (added 2026-11-01)
EQ page: a third panel, **Dynamic EQ**, after High-pass. It starts with a band picker combo (Low / Mid / High, 84 px; editor view state, not a parameter), then the picked band's controls in fixed slots, so switching bands never moves anything: **Dynamic** pill toggle (96 px), **Range** knob (−24…+24 dB, default 0; fill in `Palette::dynamic`), **Threshold** (−60…0 dB, default −24), **Attack** (0.1–200 ms, default 10), **Release** (5–2000 ms, default 120). While a band's Dynamic switch is off its four knobs are dimmed (alpha 0.4). Parameters `eq_{low,mid,high}_dyn_{on,range,thresh,attack,release}`; all off / neutral by default, so presets and sessions render bit-identical. `V:DSP/Effects.cpp` `ThreeBandEq::processDynamic`; `V:PluginEditor.cpp` `setEqDynamicBand`.
- **Ported from dooblr-q (copied, dooblr-q unchanged):** the gain law `computeDynamicGainDb` (2:1 above the threshold, 6 dB soft knee, clamped to the Range and signed by it, so a negative range cuts and a positive range boosts; total band gain clamped to ±30 dB), the SVF detector filters (low shelf → low-pass, bell → band-pass at the band's frequency and Q, high shelf → high-pass) and the one-pole peak envelope with separate attack / release. Marked in `Effects.h` as copied from dooblr-q.
- **Signal path:** each band's detector listens to that band's input in the series EQ, stereo-linked (max of the channels). The band's coefficients follow the gain every 16 samples. With no band dynamic, the EQ runs its original static loop.
- **Display:** while a band is dynamic, the EQ curve gains a second, live curve in `Palette::dynamic` (1.5 px), shaded against the static curve at alpha 0.18. It redraws when a band's live gain moves by 0.05 dB.
- **Deviation from dooblr-q:** the RangeRing (the ring around the gain knob showing the dynamic range) is not ported; the live curve and the dimmed controls carry that information here.
- Short windows: the page's three panels use the compact rows of §11.9 when needed. No new colour, font size, radius or alpha token.

### 11.11 De-esser (added 2026-11-01)
A new PROCESSING page, **De-Ess**, after Comp in the nav. It has a power button on the tab (`deess_on`, default off), no badge and no drag handle, because the slot is fixed. Page colour `dynamic`; title "DE-ESSER". Panel **De-esser**: **Frequency** knob (3–12 kHz, log, default 6.5 kHz), **Threshold** (−60…0 dB, default −30), **Range** (0–24 dB of maximum cut, default 8), **Mode** combo (Split / Wide, 96 px, default Split), **Listen** pill toggle (88 px). Parameters `deess_on`, `deess_freq`, `deess_thresh`, `deess_range`, `deess_mode`, `deess_listen` are appended last, so preset and session indices don't move. Off by default: presets and old sessions render bit-identical. Like Comp, the controls are not dimmed while the stage is off. `V:DSP/Effects.cpp` `DeEsser`; `V:UI/OutputVisuals.cpp` `DeEsserDisplay`, `DeEssMeter`.
- **Placement:** fixed, after the Gate / EQ / Comp chain (so after the noise reducer and pitch correction), before Delay / Reverb, output gain and Limiter. Reasons:
  - EQ presence boosts and compressor makeup raise sibilance, so the de-esser catches them.
  - Esses are removed before they reach the reverb and delay tails.
  - The re-orderable chain's 24 stored orders stay as they are.
- **Side chain:** two cascaded unity-peak SVF band-passes (Q 0.8) at Frequency, stereo-linked (max of the channels). Peak follower: 0.5 ms attack, 40 ms release. Gain law: 1:∞ above Threshold with a 4 dB soft knee, cut capped at Range.
- **Split:** a second-order Butterworth high shelf, half an octave below Frequency, whose top gain is the cut: x − (1 − g)·hp − (1 − √g)·k·bp from one TPT SVF. Its gain is never above 0 dB at any frequency. Content below about 2 kHz keeps its level; only the shelf's small phase lag remains while an ess is being cut. **Wide:** the whole signal is turned down by the cut.
- **Listen:** outputs the side-chain band (the signal the detector hears) instead of the program.
- **Latency:** none (no look-ahead; reported latency unchanged). Off: the buffer is untouched. Switching on or off fades over 10 ms.
- **Display:** a HistoryStrip (−60…0 dBFS) with the side-chain band level (fill), the band after the cut (`accent` line) and the GR (`dynamic`, from the top). The threshold line can be dragged as one undo step; dragging does not turn the stage on. Labels: "threshold X dB" ("(de-esser off)" while off), band and mode info, a LISTEN badge, and a legend. The right column (112 px) holds a **DeEssMeter**:
  - an SC bar in `spectrum`, turning `dynamic` over the threshold, with a threshold tick in `frozen` (dashed while off);
  - a GR bar in `dynamic` over 0–24 dB, with a Range tick;
  - numeric readouts on top.
  
  The meter repaints only when its picture changes. It fits the 940 × 600 minimum.
- No new colour, font size, radius or alpha token.

---

## 12. Reusable patterns (S)

Generic patterns first built in **dooblr-soothe** (`~/Dropbox/dooblr_plugins/dooblr-soothe`, short form **S**, 0.1.1–0.1.2). They are written so any dooblr plug-in can reuse them; values are S's, code wins. They use existing tokens only (no new colour or font).

### 12.1 Left-column hero knob

- A full-height column at the left of the editor: width `clamp(190, 400, 19 % of the window width)`, `panel` fill, radius 6, `panelGap + 2` to the content (`S:PluginEditor.cpp:518,560`).
- Top to bottom: product name (26 px row), version + Bypass (26 px row, Bypass 86 px wide at the right), the mode toggle (32 px), the knobs, then latency and status lines (16 px each) at the bottom.
- The plug-in's main control is the **hero** knob, centred: diameter `clamp(96, 300, min(column width, spare·2/3))`; one secondary knob below it at `clamp(52, 120, min(width/2, spare − hero))`. Captions 18 px above each, value box under each (24 px hero, 20 px secondary), 14 px between (`S:PluginEditor.cpp:576-592`).
- The hero knob gets `accent`, the secondary `accent` @ 0.8. Double-click resets to the default; Shift-drag is fine adjustment (as §5).
- Use it when one parameter is "how much" (depth, amount, drive). Everything else goes into the bottom row or an Advanced view, not the column.

### 12.2 Vertical dot-slider strip

- A narrow strip (S: between the toolbar and the graph's left edge) on a `panel` fill, radius 4 (`S:PluginEditor.cpp:526,660-670`).
- The strip is split into equal sections, one per control; each slider is `clamp(40, 160, section/2)` px tall, 6 px below the section top, caption under it in `textMuted` (`textMuted` @ 0.6 when dimmed, §2.8).
- Slider: a 3 px groove (radius 1.5), the value part in the control colour @ 0.7, a dot cap. Two- or three-state choices (L/R | M/S, on | off) use the same slider with snapping steps and `setSliderSnapsToMousePosition(true)`, so a click or a short drag moves it.
- Use it for stereo / routing / focus controls that are set once and should not take panel space.

### 12.3 Toolbar: preset, A / B, undo

- One row over the graph, 26 px controls centred in the bar (`S:PluginEditor.cpp:623-657`).
- Left: mode buttons (S: Sidechain, Delta), then `<` preset list `>` and a ☰ menu (Save / Load / Init). Right: Undo, Redo, A, B, the copy button (A→B or B→A, label shows the direction), Advanced. Groups 10–12 px apart, buttons 2 px apart.
- Toggle buttons: `buttonOnColour` = `accent` @ 0.45.
- A / B holds two full state snapshots for the editor's lifetime (not saved), B starts as a copy of A, switching clears undo. Every toolbar action is one undo step.
- Undo / Redo also on Cmd-Z / Cmd-Shift-Z. Buttons are disabled (dimmed), never hidden, when there is nothing to undo or redo.

### 12.4 Weighting curve overlaid on the spectrum

- One plot, one frequency axis (labels along the top, 18 px), one dB scale (labels at the right, 30 px). The spectrum, the live result and the editable curve share it (`S:ReductionGraph.cpp:124-127`).
- Draw order: input spectrum fill `spectrum` @ 0.10 + 1 px line @ 0.28; the reduction area hanging from 0 dB `dynamic` @ 0.26 with a 1.4 px edge @ 0.9; output spectrum 1.1 px `textPrimary` @ 0.45; then the curve fill `accent` @ 0.12, the selected band's own curve 1 px in its band colour @ 0.45, and the summed curve 2.2 px on top (`S:ReductionGraph.cpp:309-330`).
- Handles in the band colour (§2.2): **pills** (width `2(r−2)`, height `2(r+3)`, fully rounded) for shelves and cuts, **dots** (`2r`) for bells and band passes. Unselected fill @ 0.45, selected full; outline the band colour brightened 0.3 (1.2 px), 2 px `textPrimary` when the band is soloed / listened. Hover or drag grows the radius by 1.5 px (`S:ReductionGraph.cpp:335-350`).
- Mouse: drag (frequency + gain, one undo step, Shift = fine), wheel over a handle (Q), double-click empty space (add), double-click a handle (remove), right-click (shape / listen / remove). A hover chip (`panelRaised` @ 0.92, radius 4) reads frequency, curve and level.
- Gain reads like an EQ: down = more of the effect where the effect is a cut. If a sign convention ever changes, migrate saved state and presets (S stores a state version).

### 12.5 Smooth log-frequency spectrum drawing

FFT bins are linear in frequency, display points are logarithmic, so at the low end one bin covers several points; holding the bin value draws stairs. Rules (S 0.1.2, `S:ResonanceEngine.cpp`, `S:ReductionGraph.cpp`):

1. Resample per display point with **monotone cubic interpolation in dB** between bin centres (Fritsch–Butland tangents; no overshoot, no ringing).
2. Where a point spans several bins (the highs), blend toward the **peak** of the covered bins: `t = clamp((binsPerPoint − 2)/2, 0, 1)`, so narrow peaks and dips are not averaged away.
3. Apply a light Gaussian **fractional-octave smoothing** whose width shrinks with frequency: `σ = min(1/6 oct, 0.5·log2(1 + binHz/f))`; skip it where σ is under a third of a point (the highs stay untouched).
4. Draw **smooth segments** (monotone cubic Bézier through the points), never a polyline at the low end.
5. Use the same pipeline for every trace on the plot (input, output, reduction / gain), so they line up.
6. Display only: never smooth the values the DSP applies.
7. Build the paths once per new frame (`Path::clear()` keeps capacity); `paint()` only fills and strokes, so it allocates nothing.
8. Test it: flatten the drawn path, sample y per pixel column and fail on flat runs of ≥ 3 columns followed by a jump (S: 40–500 Hz at every editor size), and check that a real peak still stands out and the highs are unchanged.

### 12.6 Value-text formatting

- One formatter for every value box, readout and host string (`S:Parameters.cpp:7-16`): round to the stated decimals, never show `-0`, never show NaN / inf (show 0).
- Frequency: under 1 kHz whole hertz (`220 Hz`); 1–10 kHz two decimals (`2.50 kHz`); 10 kHz and up one decimal (`12.5 kHz`).
- dB: one decimal, explicit `+` for positive gains (`+3.0 dB`, `-6.0 dB`, `0.0 dB`).
- Percent: whole numbers (`75 %`); ms: one decimal under 10 ms, whole above (as `latencyReadout`).
- Unit after a space; the unit lives in the formatter, not in a separate label.
- Captions are UPPERCASE in panels (as §2.4), value text is not.

---

## 13. dooblr-feedback (F)

Read from **dooblr-feedback 0.1.1** (`~/Dropbox/dooblr_plugins/dooblr-feedback`, short form **F**). F is an automatic live feedback suppressor: an FFT watches the signal and drives a bank of zero-latency IIR notches. It uses the K/Q palette and §12 patterns. Where it adds a component, the component is generic and reusable, and its tokens are listed at the end of this section. Values are F's; code wins.

### 13.1 Layout

- Default 1080 × 660; resizable 940 × 600 to 2400 × 1400. Toolbar 30 px, margin 12, `panelGap` 8, output meter 16 px at the right of the graph (`F:PluginEditor.cpp`).
- Toolbar (as §12.3): mode, presets, A / B, undo. At the right: the **SAFETY** badge (§13.7).
- Graph on top. Under it, a bottom panel with three view tabs, **Notches / Notes / Advanced** (92 × 26, 4 px apart). Panel height is `max(Advanced preferred height, clamp(150, 330, 30 % of the editor height))`, so Advanced never clips.

### 13.2 Notch markers on the spectrum (event markers)

A notch is something the plug-in did by itself at one frequency. Mark it on the spectrum where it acts, and let the user keep or remove it there.

- **Colour:** fixed notches (locked, or set by ring-out) in `accent`; floating notches (they release when the feedback is gone) in `dynamic`. The colour is the notch's type, never its level.
- **Pin:** a 1 px dashed (3/3) leader line @ 0.5 from the chip down to a diamond handle (radius 4.5) on the notch's cut curve. Selected or hovered: line @ 0.9, diamond radius 5.5.
- **Chip:** 124 × 44, `panelRaised` @ 0.94, radius 4, a 2.5 px stripe in the marker colour at the left. Outline in the marker colour: 1 px @ 0.55, @ 0.9 while ringing, 1.8 px full when selected. Three lines:
  - frequency + note (bold);
  - cut + Q (marker colour);
  - hits, time since detection, FIX / FLT (`textMuted`).
- **Lock / clear:** a 22 px column at the chip's right with a padlock (lock = make fixed; unlock = floating) and × (clear, the notch fades out). The same actions are in the notch table. Each one is one undo step.
- The cut curve is drawn over the spectrum on a 30 dB cut scale, plus a separate line labelled **−∞** for full notches (§13.5).

### 13.3 Calm labels: lanes, motion, numbered dots

Labels must not bounce while levels change.

- **Lanes:** chips sit in at most 4 lanes (4 px apart), with the same lane order as frequency. A chip keeps its lane and x slot until a new notch, a cleared notch or a resize forces a relayout. A cut or level change never moves a label. A lane opens again only with a 6 px margin (hysteresis), and a chip may shift sideways at most 0.6 chip width from its pin.
- **Motion:** forced moves ease over 250 ms; labels fade in and out over 300 ms. The numbers on a chip refresh at most every 200 ms.
- **When labels show:** a chip is expanded while its notch is active (ringing, or changed in the last 4 s), hovered (in the graph or in the table, with a 700 ms linger) or selected. Otherwise it fades out.
- **Numbered dots:** a notch without an expanded chip (inactive, or no room) is a numbered dot: a 16 px circle (`pinRadius` 8) in the marker colour, in one row above the 0 dB line, 2 px apart, with the table number in 9.5 px bold. Overlapping dots collapse into a single numbered dot. Hovering or clicking a dot expands its chip.
- Tested (F tests "calm labels"): 5 s of fluctuating levels moves labels < 1 px (0.00 px measured); 24 notches at 940 / 1080 / 2400 px editor widths show no overlaps; labels go away after inactivity; the lock / clear hit-tests still work.

### 13.4 Notch table

- Notches view: one row per notch, 22 px rows, 20 px header, 12 px font (scales to 0.7 before truncating). Rows alternate `panel` / `panel.brighter(0.03)`; selected rows use `accent` @ 0.22; a 3 px stripe in the marker colour.
- Columns: # (30), Frequency (84), Note (74), Cut (64), Needed (64), Q (44), Level (66), Detected (74), Hits (44), Type (62), Lock (64), Clear (60).
- Selection is shared with the graph: selecting a row selects the marker, and hovering a row expands its chip.
- Cut and Needed show **−∞ dB** for a full notch (§13.5).

### 13.5 Zero-latency IIR notch bank, driven by an FFT

- **Detection:** a 4096-point FFT (8192 at 88.2 / 96 kHz, 16384 at 176.4 / 192 kHz), 256-sample hop (scaled the same way), on a side path. It never delays the audio. Latency reported to the host: 0.
- **Audio path:** up to 24 notches, each a TPT state-variable filter. Only live slots are processed. Measured CPU (F tests "real-time safety", one core, 48 kHz stereo, 64-sample blocks): about 0.6–0.7 % with 1 notch and 1.2–1.4 % with 24; it varies by about ±0.2 % between runs. Coefficients glide (20 ms), and cut changes ramp per control tick, with no zipper noise.
- **Shape:** a bell up to 30 dB, then the zeros move towards the unit circle. At **Max Cut ∞** the notch is a true notch (a zero on the unit circle, more than 80 dB down at the centre). The ramp into it is smooth and click-free.
- **One notch per howl:** a detection near an existing notch retunes, widens or deepens that notch instead of adding a second one. Notches that drift together are consolidated with a crossfade.
- **Value text:** the top of Max Cut reads **∞** and the cuts read **−∞ dB**, through the shared formatter. "inf", "∞", "full" and "mute" parse back. This is the one exception to §12.6 "never show inf": the value is a real infinite cut, not a failed computation. NaN is still never shown.

### 13.6 Room notes and presets

- **Notes view:** free text for the room (venue, mic positions, problem frequencies). Saved with the session and with user presets.
- **Presets:** original factory presets (Default, Speech / Lectern, Vocal in a Band, Monitor Wedge, Theatre Lav, Ring-out Start). Saving a user preset stores the settings, the current notches and the notes, in `~/Music/dooblr-feedback/Presets`. They use the §12.3 toolbar.

### 13.7 Safety Duck badge and meter threshold

- **Safety Duck** is the last resort: if the output goes over the Safety threshold while feedback is still running away (or always, in Always mode), F ducks the output gain. It has no latency and does not click.
- While it is ducking, a **SAFETY −x dB** badge (104 × 22) shows at the right of the toolbar: `meterRed` fill (§9), white 12 px bold text, with the duck count and time in its tooltip and the status line. When it is not ducking, the badge is hidden.
- Its controls are in the Advanced view (on / off, Trigger Feedback | Always, Threshold, Depth up to Mute, Release), as standard §2.9 controls.
- The output meter shows the Safety threshold (when Safety is on) as a 1 px dashed tick, two short dashes each 35 % of the bar width, in `meterRed` @ 0.9 (`F:UI/OutputMeter.cpp:170-177`).

### 13.8 Tokens (JSON)

`components.eventMarker` (with `motion` and `dots`), `components.notchTable`, `components.safetyBadge`, `components.meterThresholdTick`, and `changelog` entries. All values reuse existing colours; no new colour or font.

---

## 14. dooblr installer (I)

Read from **dooblr-installer 0.1.0** (`~/Dropbox/dooblr_plugins/dooblr-installer`, short form **I**). I is a standalone app (not a plug-in) that installs and updates the suite. It uses the K/Q dark palette, type and button styles; it adds no new tokens. Code wins.

- **Window**: native title bar, resizable 720×520 to 1400×1000. Header: "dooblr" wordmark with the accent underline, "installer vX.Y.Z" in muted text, and a step indicator on the right (Welcome · Choose · Install · Done; current step in `accent`, done steps in `textPrimary`, later steps muted).
- **Pages**: one task per page, a large heading, one primary button bottom right (accent fill, dark text), Back / Cancel as raised secondary buttons beside it.
- **Item rows**: rounded `panelRaised` rows; add-ons (the prophet panel) are indented 28 px under their parent on a `panel` fill. Left to right: checkbox, name (bold) and one-line description (muted), Installed / Available versions, status tag.
- **Status tags are labels, not buttons**: *Install* and *Update* get a tinted pill (status colour at 18 % alpha, text in the status colour); *Up to date*, *Newer installed* and *Not available* are plain muted text. Clicking anywhere on a row toggles its checkbox (pointing-hand cursor), so nothing that looks clickable is dead.
- **Selection helpers**: Select updates / All / None under the list; the primary button counts what it will do ("Install 5 items") and is disabled at zero.
- **Progress**: a progress bar and a plain-text log, one line per step ("Downloading …", "Checked … (sha256 OK)", "Installed … -> path").
- **Errors**: say what failed, why, and what was left alone ("… nothing was installed"); a manifest that can't be read shows the address and a Try again button.

## 15. Changelog

- **2026-11-01**: Added §14 dooblr installer (I, 0.1.0): window, pages, item rows with indented add-ons, status tags as labels (tinted pill only for Install / Update), whole-row click toggles, selection helpers, progress log, error wording. Changelog renumbered to §15. Additive only; no existing rule, token name or value changed; §11 (including §11.11) untouched; no JSON change.
- **2026-11-01**: Added §13 dooblr-feedback (F, 0.1.1): layout, notch markers with lock / clear, calm labels (stable lanes, eased motion, numbered dots, fade when unused), notch table, zero-latency IIR notch bank driven by an FFT (one notch per howl, Max Cut ∞ = true notch), room notes and presets, Safety Duck badge and meter threshold tick. Changelog renumbered to §14. The F 0.1.0 commit (`47f3907`) said "design §13" but did not include this section; this adds it. The F full-cut value text (−∞ dB / ∞) is documented as the one exception to §12.6 "never show inf"; §12.6 itself is unchanged. Additive only; no existing rule, token name or value changed; §11 untouched. JSON: `components.eventMarker.motion`, `.dots`, `.fullCutLine`, `components.safetyBadge`, `components.meterThresholdTick`, a `changelog` entry.
- **2026-11-01**: Added §11.11 dooblr-vocals De-esser (page, panel, display, meter, placement). Additive only; no existing rule, token name or value changed.
- **2026-11-01**: Added §11.10 dooblr-vocals Dynamic EQ panel (EQ page: band picker, per-band Dynamic / Range / Threshold / Attack / Release, live dynamic curve), ported from dooblr-q (no RangeRing). Additive only; existing tokens only.
- **2026-11-01**: Added §11.9 dooblr-vocals Noise reducer panel (Pitch page: Denoise, Noise floor Learned/Auto, Amount, Sensitivity, Learn Noise, status readout). Additive only; existing tokens only.
- **2026-10-31**: Added §11.8 dooblr-vocals EQ high-pass (low-cut 20–500 Hz, 12 / 24 / 48 dB/oct, off by default, drawn on the EQ curve). Additive only; existing tokens only.
- **2026-10-31**: Added §11.7 dooblr-vocals Compressor Knee (0–24 dB soft knee, default 6 dB, knee region drawn on the transfer curve). Additive only; existing tokens only, no rule, token name or value changed.
- **2026-10-31**: Added §11.6 dooblr-vocals Learn Range progress (bar driven by voiced time, status line, done / failed states). Additive only; existing tokens only, no rule, token name or value changed.
- **2026-10-31**: Added §12 Reusable patterns (S = dooblr-soothe 0.1.1–0.1.2): left-column hero knob, vertical dot-slider strip, toolbar with preset / A-B / undo, weighting curve overlaid on the spectrum with pill / dot handles, smooth log-frequency spectrum drawing, value-text formatting. Changelog renumbered to §13. Additive only; no existing rule, token name or value changed. JSON: `components.heroKnobColumn`, `components.dotSliderStrip`, `components.presetAbUndoToolbar`, `components.weightingCurveOverlay`, `components.valueText`, `graphs.smoothLogSpectrum`, a `changelog` entry.

- **2026-10-31**: §11.2 dooblr-vocals nav: the Limiter moves out of the re-orderable PROCESSING group into a new fixed OUTPUT group after TIME FX, matching the new signal order (the limiter now runs after the delay / reverb and the output gain). Added the signal-order line. The Reverb Decay control is now calibrated to the measured late-tail RT60 at 1 kHz (within 3 %); no visual change. No token name or value changed.
- **2026-10-30**: §11.1 notes ten factory programs (new "Vocal Workstation '90s", an original preset inspired by mid-1990s hardware vocal harmonizers) and the Harmony bus Lo-Fi knob (commit `4fcb339`). Additive only; standard components, no token changed.
- **2026-10-28**: Added §11.5 dooblr-vocals Key Follow strip (commit `eddffb9`). Additive only; no existing rule, token name or value changed. JSON: `components.keyFollowStrip`, `layout.vocals.keyFollowStrip`, a `changelog` entry.
- **2026-10-28**: §11.4 items 5-12 marked resolved by dooblr-vocals commit `29dac9a` (no rule, token name or value changed). dooblr-vocals path references updated to `~/Dropbox/dooblr_plugins/dooblr-vocals`.
- **2026-10-28**: Added dooblr-vocals v0.2.0 (V). Additive only; no existing rule, token name or value changed.
  - §1.1 identity table; §2.1 `fx` row and a note on vocals' palette use; §2.9 vocals components; §5 drag hygiene; §7 three product rules; §9 rows; §10 two open questions; §11 vocals section with its deviations; this changelog.
  - JSON: `changelog`, `color.fx`, vocals `colorRecipes`, `typography`, `layout.vocals`, components, `motion.learnPulse`, `motion.timers.vocalsEditorHz`, `graphs.vocals*`, `engineering.plugins.dooblr-vocals`.
- **2026-10-27**: Initial snapshot from dooblr-kick v0.3.5 and dooblr-q v0.8.0.
