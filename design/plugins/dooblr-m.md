# dooblr-m: design notes

Read `design/INDEX.md` first. This file has two parts:

- **Hand-written notes** (this section): intent, behaviour, UX rules and decisions that the code
  cannot tell you. Update them in the same commit as any behaviour change.
- **Generated map** (between the markers below): rewritten by `design/tools/update-design.py`
  from the staged source on every commit that touches `dooblr-m/`. Do not edit it by hand.

## Notes (hand-written)

- Suite-wide rules: `design/dooblr-design-system.md` (sections 1-10 and 12). Existing palette
  only; no new colour, font, radius or alpha tokens. Rotary switches and vertical rockers are
  drawn with those tokens (ticks around a discrete rotary, a flat 18×36 rocker instead of a
  wood bat switch). Pitch / mod wheels are vertical sliders in `panelRaised`, not chrome.
- `dooblr-m/README.md`
- **What it is**: an all-purpose Model D-style monophonic synth. Independent architecture,
  not a circuit-level clone. No third-party name, logo, wood or panel artwork.
- **Layout**: classic left-to-right panel (wheels, controllers, oscillator bank, mixer,
  modifiers, output) plus a compact voice cluster (priority, legato, velocity, bend) because a
  plugin needs those and the original panel does not name them. Controllers is 2×4 so LFO
  and Retrig sit with Osc 3 Ctrl. Osc 3 in the oscillator bank is double-height (audio row
  plus Rate / Sync / Div). The voice panel is only as
  wide as those four extras and left-aligned so leftover width and height go to the oscillator
  bank. Captions use 11 pt with 1 px side
  padding (JUCE's 5 px default ellipsized "Mod Mix" / "Osc 3 Ctrl"). Two-choice parameters
  (Note Priority Low/Last, noise White/Pink) are vertical rockers, not knobs; choices with
  more than two positions stay discrete rotaries (range / waveform). Rotary ticks for those
  sit inside the switch: six-position labels grow toward the knob so "2'", "Shk" and "PW" are
  not clipped. Filter Cutoff is a large knob (~1.6–2× the others) on a taller first modifiers
  row, resonance and contour stacked beside it. Header is the suite 60 px strip. Window sizes
  stay 940×600 / 1080×660 / 2400×1400.
- **DSP sources**:
  - Oscillators: Stilson/Smith BLIT (closed-form SincM) with leaky integration for saw / pulse /
    triangle. Shark = 0.65 saw + 0.35 triangle. polyBLEP is not used; the closed form is cheap
    enough at three oscillators. Top octave (M < 3) is a sine.
  - Filter: Huovilainen DAFx-04 Euler tanh ladder, 2× oversampling, half-sample feedback delay.
    Resonance (parameter id `emphasis`, unchanged) maps to loop gain so 1.0 is just past
    self-oscillation (k ≈ 4.15).
  - Contours, mixer overdrive, Osc 3 as LFO, keyboard tracking 1/3 and 2/3, Decay switch:
    Folkman Model D service manual (boards 1–4).
  - Voice (low-note priority, sample-hold glide, gate/trigger): MIT 6.101 analog-synth report
    plus the service-manual keyboard description.
- **Defaults**: low-note priority, legato (single-trigger) on, velocity off, Decay switch on,
  Osc 3 Control on, mixer Osc 1 only, 8' saw.
- **Omissions vs hardware**: no external audio input jack (Ext is feedback from the filter
  output); no headphone amp / A-440 trim; no calibration trimmers, scale trimmers or octave
  switch −5 V reference; no 44-key F-to-C restriction; no pink/red/white as three simultaneous
  buses (white / pink choice instead); no aftertouch. Pitch wheel is a parameter (springs back)
  as well as MIDI, so it can be automated.
- **VST3 MIDI CC**: `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0` so the host sees the real
  50 parameters, not ~2100 CC proxies. A-440 was removed in 0.1.2 (UI, parameter and DSP);
  sessions / presets that still store an `a440` PARAM load, and the extra child is ignored.
  Osc 3 LFO parameters added in 0.1.5 default off; old states that omit them load the defaults.
- **Knob drag overlay (0.1.1)**: rotary knobs do not show a popup value bubble, tooltip,
  extra arc or highlight while dragging. `Knob` no longer calls `Slider::setPopupDisplayEnabled`.
  The knob's own pointer and filled arc stay; double-click reset, Shift-drag, automation and
  the host's parameter display are unchanged. Other plug-ins are not affected (the call lived
  only in dooblr-m).
- **Rocker slide-flip (0.1.1)**: `BatToggle` is a vertical rocker (on = up). A click still
  toggles. Dragging the lever in its throw (≥ 4 px, up = on, down = off) sets that state;
  a mostly-horizontal rake paints the opposite of the switch you started on. Continuing the
  drag over neighbouring rockers applies the same on/off. Every rocker touched in the rake
  shares one `beginChangeGesture`/`endChangeGesture` pair, so undo reverts them together.
  Rotary range / waveform switches already step by dragging (discrete JUCE rotary).
- **Filter envelope graph (0.1.2)**: the output-column readout is the filter contour, not a
  ladder magnitude sketch. It draws the static ADS shape from Attack, Decay, Sustain and
  Amount of Contour, and overlays a live playhead/trace of the DSP filter envelope (atomics
  written at the end of each audio block, read on the UI thread). Four handles (Attack and
  Decay on the time axis, Sustain on the plateau, Amount at the peak) drag the real
  parameters with `beginChangeGesture`/`endChangeGesture`, stay in sync with the knobs and
  host automation, and double-click resets. No hint text on the graph.
- **Resonance name (0.1.3)**: the filter control formerly labelled Emphasis is Resonance on
  the knob and in the host (Ableton parameter list). The parameter ID stays `emphasis` so
  presets, saved sets and automation still load.
- **Cutoff contour indicator (0.1.4)**: when Amount of Contour is non-zero, the large
  Cutoff knob shows a faint secondary arc in the filter (`dynamic`) colour for the envelope
  sweep from the set cutoff (up to 5 octaves at full amount, the same mapping as the DSP),
  plus a thin live marker driven by the lock-free filter-envelope atomics already used by
  the output-column graph. Hidden when Amount of Contour is 0; the live marker is hidden
  while the envelope is idle. No text or hint labels. The overlay does not intercept mouse
  and does not change knob behaviour. Timer-driven; only the knob area is repainted.
- **Osc 3 LFO (0.1.5)**: a dedicated LFO mode on Osc 3, beyond the Model D LO range.
  Rocker in Controllers (under Osc 3 Ctrl / Filt Mod) labelled LFO; Retrig beside it
  (off = free-running, on = phase resets on every note-on). Rate / Sync / Div sit on
  the Osc 3 row below Range / Freq / Wave so the audio row stays three-across at 940 px.
  In LFO mode Osc 3 ignores keyboard tracking and the pitch wheel, runs a naive bipolar
  oscillator at 0.05–30 Hz (log, one-pole smoothed ~20 ms so rate changes do not click)
  or at host tempo (Sync; default 120 BPM) with divisions 1, 1/2, 1/4, 1/8, 1/16, 1/32.
  Waveforms reuse the Osc 3 selector: triangle, falling saw, rising saw, square, plus
  sample-and-hold and random in the pulse slots (tick labels switch to Tri / Dn / Up /
  Sqr / S/H / Rnd). Mixer Osc 3 is faded out (~5 ms) so it is removed from the audio
  mix automatically; Osc 3 still feeds Modulation Mix (osc mod / filter mod). A pulse
  lamp on the Rate caption follows the LFO (no Hz text). Dim unused controls, never
  hide: Range / Freq / Osc 3 Ctrl / mixer Osc 3 dim in LFO mode; Rate / Sync / Div /
  Retrig dim when LFO is off; Rate vs Div follow Sync. Osc 3 audio is bit-identical
  with LFO off. New params default off so old presets load unchanged.

<!-- BEGIN AUTO-GENERATED by design/tools/update-design.py: do not edit between these markers -->
## Generated map

- Kind: JUCE instrument VST3 (target `DooblrM`, product "dooblr-m", code Dbmm)
- Version: 0.1.5
- Last source change: 2026-11-11
- Source tree: `0e4297d91ef2` (stale check: compare with `git rev-parse --short=12 HEAD:dooblr-m`)
- Window: resize limits 940x600..2400x1400; default size 1080x660
- Build/test: `dooblr-m/scripts/build.sh` (build dir `~/dooblr-build/dooblr-m`), runs the tests, then installs the VST3 to `/Library/Audio/Plug-Ins/VST3`
- Build/test: target `DooblrMTests` (Tests/CMakeLists.txt)
- Build/test: target `DooblrMLoadCheck` (Tools/LoadCheck/CMakeLists.txt)

### Parameters (50 rows, 50 parameters after loops; from `Source/Parameters.cpp`)

`{n}` = index or name filled in by a loop; `x` = loop condition or repeat count.

| id | type | name | range | default | x |
|---|---|---|---|---|---|
| tune | float | Tune | -2.5..2.5 | 0 |  |
| glide | float | Glide | skewed(0, 2.5, 0.12) | 0 |  |
| modMix | float | Modulation Mix | 0..1 | 0 |  |
| osc3Control | bool | Osc 3 Control | on/off | true |  |
| oscMod | bool | Oscillator Mod | on/off | false |  |
| filterMod | bool | Filter Mod | on/off | false |  |
| osc1Range | choice | Osc 1 Range | rangeNames() | 2 |  |
| osc1Wave | choice | Osc 1 Waveform | waveNames() | 2 |  |
| osc2Range | choice | Osc 2 Range | rangeNames() | 2 |  |
| osc2Wave | choice | Osc 2 Waveform | waveNames() | 2 |  |
| osc2Freq | float | Osc 2 Frequency | -8..8 | 0 |  |
| osc3Range | choice | Osc 3 Range | rangeNames() | 2 |  |
| osc3Wave | choice | Osc 3 Waveform | waveNames() | 0 |  |
| osc3Freq | float | Osc 3 Frequency | -8..8 | 0 |  |
| osc3Lfo | bool | Osc 3 LFO | on/off | false |  |
| lfoRate | float | LFO Rate | skewed(0.05, 30, 1) | 1 |  |
| lfoSync | bool | LFO Sync | on/off | false |  |
| lfoDivision | choice | LFO Division | lfoDivisionNames() | 2 |  |
| lfoRetrigger | bool | LFO Retrigger | on/off | false |  |
| mixOsc1 | float | Osc 1 | 0..1 | 0.8 |  |
| mixOsc1On | bool | Osc 1 On | on/off | true |  |
| mixOsc2 | float | Osc 2 | 0..1 | 0 |  |
| mixOsc2On | bool | Osc 2 On | on/off | false |  |
| mixOsc3 | float | Osc 3 | 0..1 | 0 |  |
| mixOsc3On | bool | Osc 3 On | on/off | false |  |
| mixExt | float | Ext / Feedback | 0..1 | 0 |  |
| mixExtOn | bool | Ext On | on/off | false |  |
| mixNoise | float | Noise | 0..1 | 0 |  |
| mixNoiseOn | bool | Noise On | on/off | false |  |
| noiseType | choice | Noise Type | noiseNames() | 0 |  |
| cutoff | float | Cutoff | skewed(20, 20000, 800) | 1200 |  |
| emphasis | float | Resonance | 0..1 | 0.15 |  |
| contourAmt | float | Amount of Contour | 0..1 | 0 |  |
| fAttack | float | Filter Attack | skewed(0.001, 10, 0.4) | 0.002 |  |
| fDecay | float | Filter Decay | skewed(0.001, 10, 0.4) | 0.4 |  |
| fSustain | float | Filter Sustain | 0..1 | 0 |  |
| kybd1 | bool | Keyboard Control 1 | on/off | false |  |
| kybd2 | bool | Keyboard Control 2 | on/off | false |  |
| aAttack | float | Loudness Attack | skewed(0.001, 10, 0.4) | 0.002 |  |
| aDecay | float | Loudness Decay | skewed(0.001, 10, 0.4) | 0.5 |  |
| aSustain | float | Loudness Sustain | 0..1 | 1 |  |
| decaySwitch | bool | Decay | on/off | true |  |
| volume | float | Volume | 0..1 | 0.7 |  |
| notePriority | choice | Note Priority | priorityNames() | 0 |  |
| legato | bool | Legato | on/off | true |  |
| bendRange | int | Bend Range | 0..12 | 2 |  |
| velocityOn | bool | Velocity | on/off | false |  |
| output | float | Output | -24..12 | 0 |  |
| modWheel | float | Mod Wheel | 0..1 | 0 |  |
| pitchBend | float | Pitch Bend | -1..1 | 0 |  |

### Source map

| file | role |
|---|---|
| `CMakeLists.txt` | CMake build: DooblrM |
| `README.md` | Readme: dooblr-m |
| `Source/DSP/OutputPeakTap.h` | The final output's peak for the header meter. [OutputPeakTap] |
| `Source/DSP/Primitives.h` | Padé tanh; good to a few percent on \|x\| < 3, hard-clips beyond. [Rng, Noise, BlitOsc, LfoOsc, ...] |
| `Source/DSP/SynthEngine.{h,cpp}` | Amount of Contour = 1 opens the ladder by this many octaves. [EnvSettings, Settings, HostTime, SynthEngine, ...] |
| `Source/Parameters.{h,cpp}` | State-tree version written with every saved state (bump when a migration is needed). |
| `Source/PluginEditor.{h,cpp}` | One-page Model D-style board in the dooblr aesthetic (design/dooblr-design-system.md): 60 px header, a... [DooblrMEditor] |
| `Source/PluginProcessor.{h,cpp}` | [DooblrMProcessor] |
| `Source/Presets.{h,cpp}` | [Factory] |
| `Source/UI/BoardControls.{h,cpp}` | Titled panel background used by every Model D column. [SectionPanel, Knob, CutoffContourIndicator, Picture, ...] |
| `Source/UI/OutputMeter.{h,cpp}` | Feeds one frame's peaks (linear) at a time in ms: VBlank does this from the tap. [OutputMeter, Channel] |
| `Source/UI/Theme.{h,cpp}` | Flat, understated look for knobs, buttons and combo boxes. [DooblrLookAndFeel] |
| `Source/UndoHistory.{h,cpp}` | Undo/redo of the plug-in's own UI edits, built on juce::UndoManager. [ParameterUndoHistory, Change, Pending] |
| `Tests/CMakeLists.txt` | CMake build: DooblrMTests |
| `Tests/ModelTests.cpp` | dooblr-m console tests: the real processor, DSP and editor. |
| `Tools/LoadCheck/CMakeLists.txt` | CMake build: DooblrMLoadCheck |
| `Tools/LoadCheck/LoadCheck.cpp` | DooblrMLoadCheck: loads a built or installed dooblr-m.vst3 the way a host does. |
| `cmake/sign-bundle.sh` | Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes. |
| `scripts/build.sh` | Configure and build the dooblr-m VST3, run the tests, and install the built bundle into the VST3 plug-in... |

### UI components

- `DooblrMEditor` : AudioProcessorEditor, Timer, ChangeListener (PluginEditor.h)
- `SectionPanel` : Component (BoardControls.h)
- `Knob` : Component (BoardControls.h)
- `CutoffContourIndicator` : Component, Timer, AudioProcessorValueTreeState::Listener (BoardControls.h)
- `RotarySwitch` : Component (BoardControls.h)
- `BatToggle` : Component (BoardControls.h)
- `SilentToggle` : ToggleButton (BoardControls.h)
- `Wheel` : Component (BoardControls.h)
- `ScopeView` : Component (BoardControls.h)
- `FilterReadout` : Component, Timer, AudioProcessorValueTreeState::Listener (BoardControls.h)
- `LfoPulse` : Component, Timer (BoardControls.h)
- `OutputMeter` : Component (OutputMeter.h)
- `DooblrLookAndFeel` : LookAndFeel_V4 (Theme.h)
- Size constants: titleHeight=20 (BoardControls.h), captionHeight=16 (BoardControls.h)

### Tests

- `Tests/ModelTests.cpp` (26): Metadata, Silence, PitchA4, BlitOscillator, NoteBoundedAndReleases, SelfOscillation, CutoffTracking, EnvelopeTiming, NotePriorityAndGlide, Osc3LfoMode, Osc3DedicatedLfo, A440Removed, Aliasing, SampleRates, ExtremeFinite, StateRoundTrip, Presets, NoDeadParameters, UndoHistory, RealtimeAndCpu, Editor, KnobOverlay, SwitchDrag, PriorityAndNoiseSwitches, EnvelopeHandles, CutoffContourIndicator
<!-- END AUTO-GENERATED -->
