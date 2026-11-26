# dooblr-L: design notes

Read `design/INDEX.md` first. This file has two parts:

- **Hand-written notes** (this section): intent, behaviour, UX rules and decisions that the code
  cannot tell you. Update them in the same commit as any behaviour change.
- **Generated map** (between the markers below): rewritten by `design/tools/update-design.py`
  from the staged source on every commit that touches `dooblr-L/`. Do not edit it by hand.

## Notes (hand-written)

- Suite-wide rules: `design/dooblr-design-system.md` (sections 1–10). One page: header, style
  strip, meter row (I/O peaks, GR history, LUFS), then Pre-Process / Enhance / Link / Time
  and a full-width Output strip. Existing palette only. No new colour, font, radius or alpha
  tokens. No hint text on graphs.
- **0.1.3**: scrolling I/O history on the same −40…0 dB plot as the threshold line. Input is
  a dim `spectrum` fill, processed output is `accent` (fill + 1.6 px line), gain reduction
  is `dynamic` from the top; all three share the plot. Frames arrive on a lock-free
  `HistoryTap` FIFO (~5 ms, drop-when-full, no audio-thread alloc). Paint uses a cached
  scrolling image plus a cached grid/dB scale (32 px on the right, 0 at the top).
  **Learn** (UI-only, not a host parameter) listens 10–30 s, copies audio into a lock-free
  FIFO, and searches on a background CPU thread (`juce::FloatVectorOperations` / Accelerate;
  no GPU — in-plugin GPU is not practical for this search). Features: crest, onset density
  and strength, low-end energy, stereo correlation, short-term loudness. Candidates cover
  style, threshold, attack, release, knee, Enhance, lookahead and link; they are scored for
  LUFS gain vs onset/crest retention minus distortion, and must honour the ceiling. Apply
  is one undoable host gesture; Revert restores the previous values; the status line shows
  the chosen LUFS. **Latency** (`latencyMode`, appended) Zero / Natural / High, default
  Zero. Zero reports 0 samples (no lookahead; IIR oversampling only if it reports 0 latency,
  else Off). Auto Comp in Zero ducks the current sample with no 11-sample reconstruction
  delay — true-peak overs that need future taps may slip by a sample. Natural = Look knob +
  linear-phase FIR. High = max(Look, 8 ms) + linear-phase FIR. `setLatencySamples` updates
  on change. LoadCheck default is 0 again. Look is dimmed in Zero but stays live.
- **0.1.2**: the GR history draws a draggable **threshold** line (Pre-Process `threshold`,
  −40 dB at the bottom of the plot, 0 dB at the top). Vertical drag writes that parameter
  with `beginChangeGesture` / `endChangeGesture` so the host records automation; Shift or
  Command is 0.1× fine adjust; double-click resets to −3 dB. Hover uses the up/down resize
  cursor and a thicker highlight. The Thresh knob and the line share the same parameter
  (both directions). No caption or hint text on the graph. Paint stays the cached-grid
  path plus one extra `fillRect`.
- **0.1.1**: the Output strip height is `ControlPanel::getPreferredHeight` plus padding, so
  Input / Ceiling value boxes sit fully inside the panel at 940×600, 1080×660 and 2400×1400
  (0.1.0 clipped them at 1080×660 on Mac). Layout tests require every knob value box inside
  its parent panel, not only inside the window. `Tools/LoadCheck` keeps a default of zero
  latency and takes `--expect-latency N` or `--allow-latency`; 0.1.1/0.1.2 `build.sh`
  passed `--expect-latency 168`. From 0.1.3 the default latency mode is Zero, so LoadCheck
  is invoked with no extra flag.
- `dooblr-L/README.md`
- Independently designed two-stage mastering / bus limiter. No third-party names, logos or
  branding appear in the UI, presets, comments or identifiers.

### Two-stage path

- **Pre-process** is a lookahead soft-knee limiter. Style `overshootDb` lengthens attack
  (transients leak into Enhance); sustained GR is the full soft-knee ratio. It is not a
  brickwall at the ceiling.
- **Enhance** (amount 0–100, default 0) reshapes those residual peaks: odd (tanh) and even
  (signed square) harmonics mixed per style. Amount 0 skips the stage entirely, so output
  nulls against pre-process + Auto Comp + ceiling. The ceiling is applied after Enhance.
- **Auto Comp** (host rate) uses the same 4x windowed-sinc interpolator as the meters. In
  Natural/High it uses an 11-sample reconstruction delay that is added to reported latency
  while the option is on. In Zero it ducks the current sample with no delay (true-peak overs
  that need future taps may slip by a sample). Attack is instant for the duration of an
  inter-sample over; release is ~8 ms so the rest of the program is not ducked. A 0.05 dB
  working margin sits under the ceiling. Bypass is the delay-compensated dry path (no Auto
  Comp, trim, or ceiling).
- Final sample-peak clip at the ceiling (except in Delta audition). Dither is re-clipped.

### Styles

Each of the eight styles is a different detector mix, envelope law, overshoot, and
saturation path (`dsp::LimiterEngine::algoFor`), not a renamed preset:

| Style | Distinguishing behaviour |
|---|---|
| Transparent | Fast attack, high ratio, small overshoot, little harmonic mix |
| Punchy | Slow attack, fast release, large overshoot (lets hits through) |
| Dynamic | Transient extracted and boosted *before* GR, then limited |
| Allround | Peak/RMS hybrid (~35 % RMS), moderate everything |
| Aggressive | Very fast times, soft-clip shaper toward the ceiling, high odd harmonics |
| Modern | DC blocker, tighter overshoot, low colour |
| Bus | RMS-weighted detector, slow program-dependent release (pumps) |
| Safe | Extra ceiling margin, extra knee, 5 ms minimum lookahead, no added harmonics |

### Latency, oversampling, link

- Lookahead 0–20 ms (default 2) plus oversampling (Off/2x/4x/8x/16x/32x, default 4x).
  Latency mode Zero reports 0 (lookahead forced off; min-phase IIR OS only when that
  oversampler reports 0, otherwise OS is skipped). Natural uses the Look knob and
  linear-phase FIR. High uses at least 8 ms look and linear-phase FIR.
  `setLatencySamples` follows `engine.latencySamples()`. Bypass and Delta use a host-rate
  dry delay of that length so delay compensation stays correct.
- Transient link and release link are independent 0–100 % blends of L/R vs max(L,R) on the
  attack detector vs the RMS/release detector.

### Metering

- Input/output sample peak and true peak, GR history (3 s / 60 Hz, instant attack, 12 dB/s
  display release, 700 ms hold then 18 dB/s decay — same ballistics as the suite GR meters).
- LUFS M / S / I and LRA from a BS.1770-4 K-weighted meter (published 48 kHz SOS). LRA
  (EBU Tech 3342 percentiles) only records short-term windows after the 3 s buffer is full,
  so a constant tone does not inherit the fill-from-silence ramp. Reset clears integrated,
  LRA and the GR history. LUFS target is optional; Off is a no-op, other values ride
  short-term LUFS toward the target (400 ms) once that window is full, still under the
  ceiling. Below-gate M/S/I read `--`.
- GR history caches its grid `Image` and a scrolling waveform `Image`, and has no caption
  or hint text. The plot is −40…0 dB (0 dB at the top): dim input fill, processed output,
  GR overlay, threshold line. 1× paint budget is the suite's 4 ms.

### User presets (0.1.0)

`common/PresetPaths` — macOS `~/Library/Audio/Presets/dooblr/dooblr-L/`, Windows
`%USERPROFILE%\Documents\VST3 Presets\dooblr\dooblr-L\`. Legacy `~/Music/dooblr-L/Presets`
is copied on first run, not deleted. Extension `.dl`.

<!-- BEGIN AUTO-GENERATED by design/tools/update-design.py: do not edit between these markers -->
## Generated map

- Kind: JUCE effect VST3 (target `DooblrL`, product "dooblr-L", code Dbll)
- Version: 0.1.3
- Last source change: 2026-11-26
- Source tree: `73f1c35b999d` (stale check: compare with `git rev-parse --short=12 HEAD:dooblr-L`)
- Window: resize limits minWidthxminHeight..2400x1400; default size 1080x660
- Build/test: `dooblr-L/scripts/build.sh` (build dir `~/dooblr-build/dooblr-L`), runs the tests, then installs the VST3 to `/Library/Audio/Plug-Ins/VST3`
- Build/test: target `DooblrLTests` (Tests/CMakeLists.txt)
- Build/test: target `DooblrLLoadCheck` (Tools/LoadCheck/CMakeLists.txt)

### Parameters (20 rows, 20 parameters after loops; from `Source/Parameters.cpp`)

`{n}` = index or name filled in by a loop; `x` = loop condition or repeat count.

| id | type | name | range | default | x |
|---|---|---|---|---|---|
| threshold | float | Threshold | -40..0 step 0.1 | -3 |  |
| attack | float | Attack | 0.01..40 step 0.01 skew 0.4 | 0.5 |  |
| release | float | Release | 1..2000 step 0.1 skew 0.35 | 50 |  |
| knee | float | Knee | 0..12 step 0.1 | 3 |  |
| enhance | float | Enhance | 0..100 step 0.1 | 0 |  |
| style | choice | Style | [Transparent, Punchy, Dynamic, Allround, Aggressive, Modern, Bus, Safe] | 3 (Allround) |  |
| oversampling | choice | Oversampling | [Off, 2x, 4x, 8x, 16x, 32x] | 2 (4x) |  |
| lookahead | float | Lookahead | 0..20 step 0.01 skew 0.45 | 2 |  |
| transientLink | float | Transient Link | 0..100 step 0.1 | 100 |  |
| releaseLink | float | Release Link | 0..100 step 0.1 | 70 |  |
| inputGain | float | Input | -12..24 step 0.1 | 0 |  |
| ceiling | float | Ceiling | -12..0 step 0.01 | -0.3 |  |
| unityGain | bool | Unity Gain | on/off | false |  |
| delta | bool | Delta | on/off | false |  |
| autoComp | bool | Auto Comp | on/off | true |  |
| dither | choice | Dither | [Off, 16-bit, 24-bit] | 0 (Off) |  |
| shaping | choice | Shaping | [Off, Low, Medium, High] | 1 (Low) |  |
| lufsTarget | choice | LUFS Target | [Off, -23 LUFS, -16 LUFS, -14 LUFS, -12 LUFS] | 0 (Off) |  |
| bypass | bool | Bypass | on/off | false |  |
| latencyMode | choice | Latency | [Zero, Natural, High] | 0 (Zero) |  |

### Source map

| file | role |
|---|---|
| `CMakeLists.txt` | CMake build: DooblrL |
| `README.md` | Readme: dooblr-L |
| `Source/DSP/HistoryTap.{h,cpp}` | One history column: peaks over about 5 ms, plus the deepest GR in that window. [HistoryFrame, HistoryTap] |
| `Source/DSP/LearnSetup.{h,cpp}` | Features extracted from a captured buffer (deterministic, CPU / SIMD). [LearnFeatures, LearnResult, LearnSetup] |
| `Source/DSP/LimiterEngine.{h,cpp}` | Two-stage mastering / bus limiter. [Settings, Meters, LimiterEngine, StyleAlgo] |
| `Source/DSP/Loudness.{h,cpp}` | ITU-R BS.1770-4 / EBU R128 K-weighted loudness and EBU Tech 3342 LRA. [LoudnessMeter, Biquad, Channel] |
| `Source/DSP/TruePeak.{h,cpp}` | 4x true-peak reconstruction (windowed-sinc polyphase interpolator). [TruePeak] |
| `Source/Parameters.{h,cpp}` | State-tree version written with every saved state (bump when a migration is needed). |
| `Source/PluginEditor.{h,cpp}` | [DooblrLEditor, LearnBar] |
| `Source/PluginProcessor.{h,cpp}` | Tests: analyse a buffer and apply as one undoable change. [DooblrLProcessor] |
| `Source/Presets.{h,cpp}` | [Factory] |
| `Source/UI/ControlPanel.{h,cpp}` | A titled group of parameter controls laid out left to right (wrapping): rotary knobs, pill toggles and... [ControlPanel, Spec, Item, SegmentedToggle] |
| `Source/UI/Meters.{h,cpp}` | Display-only GR ballistics and a 60 Hz / 3 s history. [GrMeterModel, LevelMeter, Ch, GrHistory, ...] |
| `Source/UI/Theme.{h,cpp}` | Flat, understated look for knobs, buttons and combo boxes. [DooblrLookAndFeel] |
| `Source/UndoHistory.{h,cpp}` | Undo/redo of the plug-in's own UI edits, built on juce::UndoManager. [ParameterUndoHistory, Change, Pending] |
| `Tests/CMakeLists.txt` | CMake build: DooblrLTests |
| `Tests/LimiterTests.cpp` | dooblr-L console tests: the real processor, limiter engine, loudness meter and editor. |
| `Tools/LoadCheck/CMakeLists.txt` | CMake build: DooblrLLoadCheck |
| `Tools/LoadCheck/LoadCheck.cpp` | DooblrLLoadCheck: loads a built or installed dooblr-L.vst3 the way a host does. |
| `cmake/sign-bundle.sh` | Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes. |
| `scripts/build.sh` | Configure and build the dooblr-L VST3, run the tests, and install the built bundle into the VST3 plug-in... |

### UI components

- `DooblrLEditor` : AudioProcessorEditor, Timer, ChangeListener (PluginEditor.h)
- `LearnBar` : Component (PluginEditor.h)
- `ControlPanel` : Component (ControlPanel.h)
- `SegmentedToggle` : Component (ControlPanel.h)
- `LevelMeter` : Component (Meters.h)
- `GrHistory` : Component (Meters.h)
- `LoudnessPanel` : Component (Meters.h)
- `DooblrLookAndFeel` : LookAndFeel_V4 (Theme.h)
- Size constants: scaleWidth=32 (Meters.h)

### Tests

- `Tests/LimiterTests.cpp` (17): Metadata, Ceiling, Latency, EnhanceTransparency, Styles, Lufs, StereoLink, Oversampling, StatePresetsUndo, NoDeadParameters, Editor, ThresholdLineDrag, HistoryFifo, BurstOverlayAndPaint, NoAudioThreadAllocation, Learn, LearnThread
<!-- END AUTO-GENERATED -->
