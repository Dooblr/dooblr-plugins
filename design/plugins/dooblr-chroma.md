# dooblr-chroma: design notes

Read `design/INDEX.md` first. This file has two parts:

- **Hand-written notes** (this section): intent, behaviour, UX rules and decisions that the code
  cannot tell you. Update them in the same commit as any behaviour change.
- **Generated map** (between the markers below): rewritten by `design/tools/update-design.py`
  from the staged source on every commit that touches `dooblr-chroma/`. Do not edit it by hand.

## Notes (hand-written)

- Suite-wide rules: `design/dooblr-design-system.md` (sections 1–10 and 12). Existing palette
  only; no new colour, font, radius or alpha tokens. No third-party names, logos, assets or branding.
- `dooblr-chroma/README.md`
- **What it is**: a physical-modelling percussion and resonator instrument. Two modal resonators
  (A, B) are excited by a mallet and/or a filtered noise source, either mixed in parallel or
  bidirectionally coupled. Two layers can be stacked or split across the keyboard. A zero-latency
  master chain (EQ, compressor, distortion, chorus, delay, reverb) and four macros finish the
  sound. The reference pitch with Pitch at 0.00 and Key at 1.00:1 is C4 = 261.63 Hz.
- **Layout (0.1.1)**: a 60 px header (name, L1/L2 edit, Main/Advanced, undo/redo, preset,
  save/load, output, version). The Main page is built around the objects: each resonator is a
  large dooblr-style drawing of the selected body (string, bar, marimba, plate, drumhead,
  membrane, open/closed tube, manual partials) with a mallet striking at the hit position and
  no hint text. Object tiles pick the type; dragging the mallet on the picture sets Hit.
  Only four musical knobs sit under the picture (Pitch, Decay, Material or Tone, Hit).
  Coupling is drawn as a spring between A and B, with the Coupled toggle and a vertical Balance
  fader. A single compact essentials row on Main holds Mallet Stiffness, Noise amount, and
  Reverb. Noise filter/envelope, Mixer detail, Play (Layers / L1 / L2 levels, unison, vibrato,
  macros), and the rest of the FX live on the Advanced page — same controls, never dropped. Manual partials are hidden unless the
  type is Manual; Material is hidden on tubes and Radius is hidden otherwise — they are not left
  as dead controls. Split is hidden unless Layers = Split. Value strings use short units
  (e.g. `0.80`, `5.0 Hz`) and captions/value boxes do not squeeze text below a horizontal scale
  of 1. Window sizes stay 940×600 / 1080×660 / 2400×1400. Editing L1 vs L2 rebinds the same
  controls (both layers stay automatable).
- **DSP**: each resonator is a bank of two-pole modes (band-pass impulse form) with published
  ratios for string (harmonics), bar (free-free Euler–Bernoulli), marimba (undercut, 1 : 4 : 9.3),
  rectangular plate (bending, *f* ~ *k*²), circular drumhead (membrane Bessel ratios), rectangular
  membrane (*f* ~ *k*), open tube (all harmonics) and closed tube (odd harmonics). Manual uses up
  to four user ranks. Hit position scales each mode by a type-appropriate shape. Coupling in the
  On position injects a delayed, impedance-weighted force with a conservative gain bound so the
  loop cannot blow up; Off is a parallel Balance mix. Excitation enters resonator A only when
  coupled. The compressor is feed-forward with no look-ahead; plugin latency is 0.
- **Gain**: 0.1.0/0.1.1 used a fixed per-mode gain of 0.12, so loudness grew with mode count
  (Init forte LoadCheck peak 4.669, about +13 dBFS). Banks are now L2-normalized with a
  mallet-spectrum weight (`kModalScale / sqrt(sum (shape · w(f))^2)`, `w = 1/(1+(f/600)^3)`)
  so a dense inharmonic bank does not steal gain from the partials a short mallet actually
  excites. Strike gains use |shape| so bipolar plate and membrane modes do not cancel at the
  onset. Stacked layers and coupled A+B are also 1/sqrt(N)-scaled. Continuous noise is
  scaled by `kNoiseExcitation`. Output stays at 0 dB; a gentle tanh soft-clip above 0.95 is
  a last-resort guard only. A single forte note on a factory preset should peak around
  -6 to -3 dBFS, and a 3-note chord stays below 0 dBFS.
- **Presets**: 40 compiled-in factory programs, grouped in the menu as Mallets, Plucked,
  Struck, Winds, Bowed, Keys, Pads and FX (Init first). User presets use
  `common/PresetPaths` (`~/Library/Audio/Presets/dooblr/dooblr-chroma/`, `.dchroma`).
- **Clicks**: a bare `(1-t)` on mallet noise started at full amplitude on the first sample
  (the Hann pulse itself correctly started at 0), which was a click at every mallet attack.
  Note-off applied `out *= max(env, 0.15) + 0.85` — always ≥ 1, and as much as 1.85× if the
  noise envelope was still high — a step discontinuity at every typical-length release.
  Mallet noise now uses the same Hann window as the pulse. Release uses a smoothed damper
  that starts at 1 and decays with time constant Release × Decay (minimum 2 ms). A ~2 ms
  residual fade covers steal and retrigger so a ringing tail is not hard-cut. Voices end
  only after the output has dropped below about -90 dBFS.
- **0.1.3**: click-free note-on / note-off (windowed mallet noise, smoothed release damper,
  steal/retrigger fade).
- **0.1.2**: gain staging (energy-normalized modal banks, output soft-clip) and 30 additional
  factory presets (40 total).
- **0.1.1**: layout pass only — object-first Main page, Advanced detail page, readable knobs, hide-not-dim for inapplicable controls.
- **0.1.0 deferrals**: per-layer exciters and FX (they are shared); graphic (10-band) noise
  shaping; randomiser; a file-chooser for Load (Load opens the first user preset; Save writes a
  new file). Do not ship dead controls in their place.

<!-- BEGIN AUTO-GENERATED by design/tools/update-design.py: do not edit between these markers -->
## Generated map

- Kind: JUCE instrument VST3 (target `DooblrChroma`, product "dooblr-chroma", code Dbch)
- Version: 0.1.3
- Last source change: 2026-11-26
- Source tree: `321559b81125` (stale check: compare with `git rev-parse --short=12 HEAD:dooblr-chroma`)
- Window: resize limits 940x600..2400x1400; default size 1080x660
- Build/test: `dooblr-chroma/scripts/build.sh` (build dir `~/dooblr-build/chroma-build`), runs the tests, then installs the VST3 to `/Library/Audio/Plug-Ins/VST3`
- Build/test: target `DooblrChromaTests` (Tests/CMakeLists.txt)
- Build/test: target `DooblrChromaLoadCheck` (Tools/LoadCheck/CMakeLists.txt)

### Parameters (144 rows, about 144 parameters after loops; from `Source/Parameters.cpp`)

`{n}` = index or name filled in by a loop; `x` = loop condition or repeat count.

| id | type | name | range | default | x |
|---|---|---|---|---|---|
| malletStiffness | float | Stiffness | 0..1 step 0.01 | 0.55 |  |
| malletNoise | float | Mallet Noise | 0..1 step 0.01 | 0.12 |  |
| malletColor | float | Color | rangeWithCentre(80, 8000, 1200) | 1800 |  |
| mixMallet | float | Mallet | 0..1 step 0.01 | 0.85 |  |
| mixNoise | float | Noise Mix | 0..1 step 0.01 | 0 |  |
| mixDirect | float | Direct | 0..1 step 0.01 | 0 |  |
| noiseFilter | choice | Noise Filter | [Low-pass, High-pass, Band-pass, LP+HP] | 0 (Low-pass) |  |
| noiseFreq | float | Noise Freq | rangeWithCentre(40, 12000, 1500) | 1800 |  |
| noiseQ | float | Noise Q | rangeWithCentre(0.3, 12, 1) | 0.8 |  |
| envMode | choice | Env Mode | [ADSR, AHD] | 0 (ADSR) |  |
| envAttack | float | Attack | rangeWithCentre(0.001, 4, 0.05) | 0.005 |  |
| envHold | float | Hold | rangeWithCentre(0, 4, 0.1) | 0.08 |  |
| envDecay | float | Env Decay | rangeWithCentre(0.01, 8, 0.3) | 0.25 |  |
| envSustain | float | Sustain | 0..1 step 0.01 | 0 |  |
| envRelease | float | Env Release | rangeWithCentre(0.01, 8, 0.2) | 0.15 |  |
| lfoRate | float | LFO Rate | rangeWithCentre(0.05, 30, 3) | 4 |  |
| lfoDepth | float | LFO Depth | 0..1 step 0.01 | 0 |  |
| unisonCount | choice | Unison | [1, 2, 3, 4] | 0 (1) |  |
| unisonDetune | float | Detune | 0..50 step 0.1 | 8 |  |
| vibratoRate | float | Vibrato Rate | rangeWithCentre(0.1, 12, 5) | 5 |  |
| vibratoDepth | float | Vibrato Depth | 0..100 step 0.1 | 0 |  |
| glide | float | Glide | rangeWithCentre(0, 1.5, 0.08) | 0 |  |
| macro1 | float | Macro 1 | 0..1 step 0.01 | 0 |  |
| macro2 | float | Macro 2 | 0..1 step 0.01 | 0 |  |
| macro3 | float | Macro 3 | 0..1 step 0.01 | 0 |  |
| macro4 | float | Macro 4 | 0..1 step 0.01 | 0 |  |
| layerMode | choice | Layers | [Layered, Split] | 0 (Layered) |  |
| splitNote | float | Split Note | 0..127 step 1 | 60 |  |
| layer1Level | float | Layer 1 Level | 0..1 step 0.01 | 1 |  |
| layer2Level | float | Layer 2 Level | 0..1 step 0.01 | 0 |  |
| layer2Transpose | float | Layer 2 Transpose | -24..24 step 1 | 0 |  |
| output | float | Output | -24..12 step 0.1 | 0 |  |
| eqLow | float | EQ Low | -12..12 step 0.1 | 0 |  |
| eqMid | float | EQ Mid | -12..12 step 0.1 | 0 |  |
| eqHigh | float | EQ High | -12..12 step 0.1 | 0 |  |
| eqMidFreq | float | EQ Mid Freq | rangeWithCentre(200, 5000, 1000) | 1000 |  |
| compThreshold | float | Comp Thresh | -40..0 step 0.1 | 0 |  |
| compRatio | float | Comp Ratio | rangeWithCentre(1, 20, 4) | 4 |  |
| compAttack | float | Comp Attack | rangeWithCentre(0.001, 0.1, 0.01) | 0.01 |  |
| compRelease | float | Comp Release | rangeWithCentre(0.02, 1, 0.15) | 0.12 |  |
| compMix | float | Comp Mix | 0..1 step 0.01 | 0 |  |
| distDrive | float | Drive | 0..1 step 0.01 | 0 |  |
| distMix | float | Dist Mix | 0..1 step 0.01 | 0.5 |  |
| chorusRate | float | Chorus Rate | rangeWithCentre(0.05, 8, 0.8) | 0.8 |  |
| chorusDepth | float | Chorus Depth | 0..1 step 0.01 | 0.35 |  |
| chorusMix | float | Chorus Mix | 0..1 step 0.01 | 0 |  |
| delayTime | float | Delay Time | rangeWithCentre(0.01, 1.5, 0.3) | 0.35 |  |
| delayFeedback | float | Delay Feedback | 0..0.95 step 0.01 | 0.35 |  |
| delayMix | float | Delay Mix | 0..1 step 0.01 | 0 |  |
| reverbSize | float | Reverb Size | 0..1 step 0.01 | 0.45 |  |
| reverbDamp | float | Reverb Damp | 0..1 step 0.01 | 0.4 |  |
| reverbMix | float | Reverb Mix | 0..1 step 0.01 | 0 |  |
| l1_couple | bool | L1 Coupling | on/off | false |  |
| l1_balance | float | L1 Balance | 0..1 step 0.01 | 0.5 |  |
| l2_couple | bool | L2 Coupling | on/off | false |  |
| l2_balance | float | L2 Balance | 0..1 step 0.01 | 0.5 |  |
| {n}_on | bool | L1 A On | on/off | true |  |
| {n}_type | choice | L1 A Type | [String, Bar, Marimba, Plate, Drumhead, Membrane, Open Tube, Closed Tube, Manual] | 0 (String) |  |
| {n}_density | choice | L1 A Density | [4, 16, 30, 70] | 1 (16) |  |
| {n}_pitch | float | L1 A Pitch | -24..24 step 1 | 0 |  |
| {n}_cents | float | L1 A Cents | -50..50 step 1 | 0 |  |
| {n}_key | float | L1 A Key | 0..2 step 0.01 | 1 |  |
| {n}_pLevel | float | L1 A P.Level | -12..12 step 0.01 | 0 |  |
| {n}_pRate | float | L1 A P.Rate | rangeWithCentre(0.001, 2, 0.08) | 0.05 |  |
| {n}_pVel | float | L1 A P.Vel | -1..1 step 0.01 | 0 |  |
| {n}_decay | float | L1 A Decay | rangeWithCentre(0.05, 12, 1.5) | 1.4 |  |
| {n}_decayKey | float | L1 A Decay Key | -1..1 step 0.01 | 0 |  |
| {n}_decayVel | float | L1 A Decay Vel | -1..1 step 0.01 | 0 |  |
| {n}_release | float | L1 A Release | 0..1 step 0.01 | 0.35 |  |
| {n}_material | float | L1 A Material | -1..1 step 0.01 | -0.35 |  |
| {n}_tone | float | L1 A Tone | -12..12 step 0.1 | -3 |  |
| {n}_lowCut | float | L1 A Low Cut | 0..16 step 1 | 0 |  |
| {n}_radius | float | L1 A Radius | 0..1 step 0.01 | 0.55 |  |
| {n}_hit | float | L1 A Hit | 0..1 step 0.01 | 0.22 |  |
| {n}_hitVel | float | L1 A Hit Vel | -1..1 step 0.01 | 0 |  |
| {n}_hitKey | float | L1 A Hit Key | -1..1 step 0.01 | 0 |  |
| {n}_hitRnd | float | L1 A Hit Rnd | 0..1 step 0.01 | 0 |  |
| `prefix + suffix` | float | L1 A Partial {n} | 1..32 step 0.01 | i | i<=4 |
| {n}_on | bool | L1 B On | on/off | false |  |
| {n}_type | choice | L1 B Type | [String, Bar, Marimba, Plate, Drumhead, Membrane, Open Tube, Closed Tube, Manual] | 3 (Plate) |  |
| {n}_density | choice | L1 B Density | [4, 16, 30, 70] | 1 (16) |  |
| {n}_pitch | float | L1 B Pitch | -24..24 step 1 | 0 |  |
| {n}_cents | float | L1 B Cents | -50..50 step 1 | 0 |  |
| {n}_key | float | L1 B Key | 0..2 step 0.01 | 1 |  |
| {n}_pLevel | float | L1 B P.Level | -12..12 step 0.01 | 0 |  |
| {n}_pRate | float | L1 B P.Rate | rangeWithCentre(0.001, 2, 0.08) | 0.05 |  |
| {n}_pVel | float | L1 B P.Vel | -1..1 step 0.01 | 0 |  |
| {n}_decay | float | L1 B Decay | rangeWithCentre(0.05, 12, 1.5) | 1.4 |  |
| {n}_decayKey | float | L1 B Decay Key | -1..1 step 0.01 | 0 |  |
| {n}_decayVel | float | L1 B Decay Vel | -1..1 step 0.01 | 0 |  |
| {n}_release | float | L1 B Release | 0..1 step 0.01 | 0.35 |  |
| {n}_material | float | L1 B Material | -1..1 step 0.01 | -0.35 |  |
| {n}_tone | float | L1 B Tone | -12..12 step 0.1 | -3 |  |
| {n}_lowCut | float | L1 B Low Cut | 0..16 step 1 | 0 |  |
| {n}_radius | float | L1 B Radius | 0..1 step 0.01 | 0.55 |  |
| {n}_hit | float | L1 B Hit | 0..1 step 0.01 | 0.22 |  |
| {n}_hitVel | float | L1 B Hit Vel | -1..1 step 0.01 | 0 |  |
| {n}_hitKey | float | L1 B Hit Key | -1..1 step 0.01 | 0 |  |
| {n}_hitRnd | float | L1 B Hit Rnd | 0..1 step 0.01 | 0 |  |
| `prefix + suffix` | float | L1 B Partial {n} | 1..32 step 0.01 | i | i<=4 |
| {n}_on | bool | L2 A On | on/off | true |  |
| {n}_type | choice | L2 A Type | [String, Bar, Marimba, Plate, Drumhead, Membrane, Open Tube, Closed Tube, Manual] | 1 (Bar) |  |
| {n}_density | choice | L2 A Density | [4, 16, 30, 70] | 1 (16) |  |
| {n}_pitch | float | L2 A Pitch | -24..24 step 1 | 0 |  |
| {n}_cents | float | L2 A Cents | -50..50 step 1 | 0 |  |
| {n}_key | float | L2 A Key | 0..2 step 0.01 | 1 |  |
| {n}_pLevel | float | L2 A P.Level | -12..12 step 0.01 | 0 |  |
| {n}_pRate | float | L2 A P.Rate | rangeWithCentre(0.001, 2, 0.08) | 0.05 |  |
| {n}_pVel | float | L2 A P.Vel | -1..1 step 0.01 | 0 |  |
| {n}_decay | float | L2 A Decay | rangeWithCentre(0.05, 12, 1.5) | 1.4 |  |
| {n}_decayKey | float | L2 A Decay Key | -1..1 step 0.01 | 0 |  |
| {n}_decayVel | float | L2 A Decay Vel | -1..1 step 0.01 | 0 |  |
| {n}_release | float | L2 A Release | 0..1 step 0.01 | 0.35 |  |
| {n}_material | float | L2 A Material | -1..1 step 0.01 | -0.35 |  |
| {n}_tone | float | L2 A Tone | -12..12 step 0.1 | -3 |  |
| {n}_lowCut | float | L2 A Low Cut | 0..16 step 1 | 0 |  |
| {n}_radius | float | L2 A Radius | 0..1 step 0.01 | 0.55 |  |
| {n}_hit | float | L2 A Hit | 0..1 step 0.01 | 0.22 |  |
| {n}_hitVel | float | L2 A Hit Vel | -1..1 step 0.01 | 0 |  |
| {n}_hitKey | float | L2 A Hit Key | -1..1 step 0.01 | 0 |  |
| {n}_hitRnd | float | L2 A Hit Rnd | 0..1 step 0.01 | 0 |  |
| `prefix + suffix` | float | L2 A Partial {n} | 1..32 step 0.01 | i | i<=4 |
| {n}_on | bool | L2 B On | on/off | false |  |
| {n}_type | choice | L2 B Type | [String, Bar, Marimba, Plate, Drumhead, Membrane, Open Tube, Closed Tube, Manual] | 4 (Drumhead) |  |
| {n}_density | choice | L2 B Density | [4, 16, 30, 70] | 1 (16) |  |
| {n}_pitch | float | L2 B Pitch | -24..24 step 1 | 0 |  |
| {n}_cents | float | L2 B Cents | -50..50 step 1 | 0 |  |
| {n}_key | float | L2 B Key | 0..2 step 0.01 | 1 |  |
| {n}_pLevel | float | L2 B P.Level | -12..12 step 0.01 | 0 |  |
| {n}_pRate | float | L2 B P.Rate | rangeWithCentre(0.001, 2, 0.08) | 0.05 |  |
| {n}_pVel | float | L2 B P.Vel | -1..1 step 0.01 | 0 |  |
| {n}_decay | float | L2 B Decay | rangeWithCentre(0.05, 12, 1.5) | 1.4 |  |
| {n}_decayKey | float | L2 B Decay Key | -1..1 step 0.01 | 0 |  |
| {n}_decayVel | float | L2 B Decay Vel | -1..1 step 0.01 | 0 |  |
| {n}_release | float | L2 B Release | 0..1 step 0.01 | 0.35 |  |
| {n}_material | float | L2 B Material | -1..1 step 0.01 | -0.35 |  |
| {n}_tone | float | L2 B Tone | -12..12 step 0.1 | -3 |  |
| {n}_lowCut | float | L2 B Low Cut | 0..16 step 1 | 0 |  |
| {n}_radius | float | L2 B Radius | 0..1 step 0.01 | 0.55 |  |
| {n}_hit | float | L2 B Hit | 0..1 step 0.01 | 0.22 |  |
| {n}_hitVel | float | L2 B Hit Vel | -1..1 step 0.01 | 0 |  |
| {n}_hitKey | float | L2 B Hit Key | -1..1 step 0.01 | 0 |  |
| {n}_hitRnd | float | L2 B Hit Rnd | 0..1 step 0.01 | 0 |  |
| `prefix + suffix` | float | L2 B Partial {n} | 1..32 step 0.01 | i | i<=4 |

### Source map

| file | role |
|---|---|
| `CMakeLists.txt` | CMake build: DooblrChroma |
| `README.md` | Readme: dooblr-chroma 0.1.3 |
| `Source/DSP/Biquad.h` | A reusable, non-nested transposes-form-II biquad. [Biquad] |
| `Source/DSP/Modes.{h,cpp}` | Concert-pitch C4 used as the resonator reference when Pitch is 0.00. |
| `Source/DSP/SynthEngine.{h,cpp}` | Continuous noise has much higher RMS than a short mallet pulse through (x-x1). [ResonatorSettings, LayerSettings, Settings, SynthEngine, ...] |
| `Source/Parameters.{h,cpp}` | Per-layer, per-resonator (l1/l2, a/b) l1 resonator a |
| `Source/PluginEditor.{h,cpp}` | Global exciters / mixer / play / fx — always bound [DooblrChromaEditor] |
| `Source/PluginProcessor.{h,cpp}` | [DooblrChromaProcessor] |
| `Source/Presets.{h,cpp}` | Struck / drums --- [Factory] |
| `Source/UI/ControlPanel.{h,cpp}` | Object body + mallet at the hit position. [Knob, Choice, Toggle, Panel, ...] |
| `Source/UI/Theme.{h,cpp}` | Flat, understated look for knobs, buttons and combo boxes. [DooblrLookAndFeel] |
| `Source/UndoHistory.{h,cpp}` | Undo/redo of the plug-in's own UI edits, built on juce::UndoManager. [ParameterUndoHistory, Change, Pending] |
| `Tests/CMakeLists.txt` | CMake build: DooblrChromaTests |
| `Tests/ChromaTests.cpp` | dooblr-chroma console tests: the real processor, DSP and editor. |
| `Tools/LoadCheck/CMakeLists.txt` | CMake build: DooblrChromaLoadCheck |
| `Tools/LoadCheck/LoadCheck.cpp` | DooblrChromaLoadCheck: loads a built or installed dooblr-chroma.vst3 the way a host does. |
| `cmake/sign-bundle.sh` | Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes. |
| `scripts/build.sh` | Configure and build the dooblr-chroma VST3, run the tests, and install the built bundle into the VST3... |

### UI components

- `DooblrChromaEditor` : AudioProcessorEditor, Timer, ChangeListener (PluginEditor.h)
- `Knob` : Component (ControlPanel.h)
- `Choice` : Component (ControlPanel.h)
- `Toggle` : Component (ControlPanel.h)
- `Panel` : Component (ControlPanel.h)
- `ResonatorView` : Component (ControlPanel.h)
- `ObjectPicker` : Component (ControlPanel.h)
- `CouplingArt` : Component (ControlPanel.h)
- `DooblrLookAndFeel` : LookAndFeel_V4 (Theme.h)

### Tests

- `Tests/ChromaTests.cpp` (13): Metadata, Silence, C4, ModalRatios, CouplingStability, NoNanParallelAndFx, PresetRoundTrip, UserPresetFile, MacrosLive, RealtimeAndCpu, GainStaging, Editor, ClickFree
<!-- END AUTO-GENERATED -->
