# dooblr-sub: design notes

Read `design/INDEX.md` first. This file has two parts:

- **Hand-written notes** (this section): intent, behaviour, UX rules and decisions that the code
  cannot tell you. Update them in the same commit as any behaviour change.
- **Generated map** (between the markers below): rewritten by `design/tools/update-design.py`
  from the staged source on every commit that touches `dooblr-sub/`. Do not edit it by hand.

## Notes (hand-written)

- Suite-wide rules: `design/dooblr-design-system.md` (sections 1–10). One page, three panels
  (Sub, Tone Shift, System Check) plus a spectrum / sub-band meter and a prominent verdict
  panel. Existing palette only. No new colour, font, radius or alpha tokens.
- `dooblr-sub/README.md`
- Independently designed. The sub generator is a tracking oscillator plus a Lo band, not a
  recreation of any commercial sub-harmonic enhancer. The tone shifter is a transient/body
  split with cepstral envelope warping, not a recreation of any commercial drum tuner. No
  third-party names, logos or branding appear in the UI, presets, comments or identifiers.

### Sub generator

- Tracking: one-pole low-pass around Frequency, high-pass at ~18 Hz, then normalised
  autocorrelation of a 2048-sample window every 256 samples, parabolic peak interpolation,
  smoothed by Tracking (about 8–180 ms). Range is clamped to 30–120 Hz.
- Oscillator: two voices, phase-continuous hand-off with an 8 ms crossfade when the tracked
  pitch jumps more than 6 %. Octave mode is −1 oct, unison, or both. Amplitude follows a
  peak envelope (≈3 ms attack; release shortens as Tracking increases). Drive is a cubic
  shaper plus tanh so the sub still speaks on small speakers.
- Lo: TPT-ish band-pass at Frequency, same envelope, milder drive.

### Tone shift (algorithm)

Duration must not change. Plain FFT resampling (resample then stretch back) is not used.

1. **Transient / body split** — fast vs slow envelope followers on |x|. The transient path
   is delayed by the STFT latency and is not shifted, so attacks stay in time.
2. **Body resynthesis** — two parallel duration-locked paths:
   - **Modal resonator** (the audible body): zero-crossing period of the body, smoothed,
     scaled by `2^(cents/1200)`, and resynthesised as an envelope-followed sine, delayed
     to the STFT latency. Duration is the body's envelope; only the modal frequency moves.
   - **Cepstral STFT residual** (broadband fill, analysed every hop): Hann-windowed STFT,
     order 11 (2048), hop 512 (75 % overlap). A real cepstrum of the log-magnitude
     (low-quefrency lifter, ~48 bins) estimates the spectral envelope. Residual
     `mag / envelope` and envelope are warped along frequency by the same ratio and
     overlap-add resynthesised with the source-bin phase, so hop (and therefore duration)
     is unchanged. The residual is tanh-limited and mixed under the resonator so a
     smeared OLA cannot dominate a single-mode body's centroid.
3. **Mix** — Shift Mix blends delayed dry with (delayed transient + delayed modal + STFT
   residual). Body sets how much of the resynthesised body is kept. Transient sets how
   much of the unshifted attack is kept.

Latency is `ToneShift::latencySamples` (2048) and is always reported, including on bypass
(bypass is the delayed dry signal so host delay compensation stays correct).

References (algorithm, not code):

- Makhoul, J. "Linear prediction: A tutorial review." Proc. IEEE 63(4), 1975.
- Oppenheim, A. V. & Schafer, R. W. *Discrete-Time Signal Processing* (real cepstrum).
- McAulay, R. & Quatieri, T. "Speech analysis/synthesis based on a sinusoidal
  representation." IEEE TASSP 34(4), 1986.
- Serra, X. "A system for sound analysis/transformation/synthesis based on a deterministic
  plus stochastic decomposition." PhD, Stanford CCRMA, 1989.
- Laroche, J. & Dolson, M. "New phase-vocoder techniques for pitch-shifting and
  time-scaling." J. AES 47(11), 1999. Used here only as duration-locked frequency-axis
  warping of the body.

### System check

Verdicts are computed from measured values in `dsp::evaluate` (same function the UI and
the tests call):

| Reason | Trigger (Club; Festival is stricter, Home looser) |
|---|---|
| Too much energy below 30 Hz | infra / sub RMS above the target ratio, or 20–30 Hz dominating |
| Stereo low end below 120 Hz | L/R correlation of the low band too low, or side RMS high |
| Sub peaks clipping headroom | peak at or above the target ceiling |
| Sub cancels fundamental | correlation of generated sub vs original low band strongly negative |
| DC offset | \|mean\| above 0.02 |
| Unstable sub pitch | relative std of the tracker |
| Excessive sustained low end | sub RMS in dB above the target floor |

Target also moves the sub-path limiter ceiling and the infra HPF bias, so it is not a
dead control. Synthetic tests: mono 50 Hz sine passes on Club; 18 Hz fails; out-of-phase
stereo sub fails; clipped sub fails.

Sub safety (defaults on): mono-below ~100 Hz, infrasonic HPF ~25 Hz, soft limiter.

### UI

- Header 60 px, 3 px accent bar, 24 pt title, 13 pt version (`componentID` `version`),
  Undo/Redo 50×24, Output slider with TextBoxRight 70×20, Bypass.
- Three ControlPanels in columns; spectrum + five sub-band meters on the left of the
  display row; verdict badge (green / amber / red) on the right. No hint text on graphs.
- Resize 940×600, 1080×660, 2400×1400 with a layout overflow test.

<!-- BEGIN AUTO-GENERATED by design/tools/update-design.py: do not edit between these markers -->
## Generated map

- Kind: JUCE effect VST3 (target `DooblrSub`, product "dooblr-sub", code Dbsb)
- Version: 0.1.0
- Last source change: 2026-11-11
- Source tree: `795db3fb6d71` (stale check: compare with `git rev-parse --short=12 HEAD:dooblr-sub`)
- Window: resize limits minWidthxminHeight..2400x1400; default size 1080x660
- Build/test: `dooblr-sub/scripts/build.sh` (build dir `~/dooblr-build/dooblr-sub`), runs the tests, then installs the VST3 to `/Library/Audio/Plug-Ins/VST3`
- Build/test: target `DooblrSubTests` (Tests/CMakeLists.txt)
- Build/test: target `DooblrSubLoadCheck` (Tools/LoadCheck/CMakeLists.txt)

### Parameters (19 rows, 19 parameters after loops; from `Source/Parameters.cpp`)

`{n}` = index or name filled in by a loop; `x` = loop condition or repeat count.

| id | type | name | range | default | x |
|---|---|---|---|---|---|
| subAmount | float | Sub | 0..100 step 0.1 | 50 |  |
| loAmount | float | Lo | 0..100 step 0.1 | 25 |  |
| frequency | float | Frequency | 30..120 skew centre sqrt(30 * 120) | 60 |  |
| octaveMode | choice | Octave | [-1 oct, Unison, Both] | 0 (-1 oct) |  |
| trackingSpeed | float | Tracking | 0..100 step 0.1 | 55 |  |
| drive | float | Drive | 0..100 step 0.1 | 20 |  |
| mix | float | Mix | 0..100 step 0.1 | 100 |  |
| output | float | Output | -24..12 step 0.1 | 0 |  |
| shift | float | Shift | -1200..1200 step 1 | 0 |  |
| transient | float | Transient | 0..100 step 0.1 | 80 |  |
| bodyMix | float | Body | 0..100 step 0.1 | 100 |  |
| shiftMix | float | Shift Mix | 0..100 step 0.1 | 0 |  |
| monoBelow | bool | Mono below | on/off | true |  |
| monoFreq | float | Mono freq | 60..150 skew centre sqrt(60 * 150) | 100 |  |
| infraHpf | bool | Infra HPF | on/off | true |  |
| infraFreq | float | Infra freq | 15..40 skew centre sqrt(15 * 40) | 25 |  |
| subLimit | bool | Sub limit | on/off | true |  |
| target | choice | Target | [Club, Festival / PA, Home / Studio] | 0 (Club) |  |
| bypass | bool | Bypass | on/off | false |  |

### Source map

| file | role |
|---|---|
| `CMakeLists.txt` | CMake build: DooblrSub |
| `README.md` | Readme: dooblr-sub |
| `Source/DSP/OutputPeakTap.h` | The peaks (linear gain) since the last take; resets them. [OutputPeakTap] |
| `Source/DSP/PitchTracker.{h,cpp}` | Autocorrelation pitch tracker on a low-passed tracking band. [PitchTracker] |
| `Source/DSP/SubEngine.{h,cpp}` | Full dooblr-sub path: tone shift (latency) -> sub generator -> safety -> mix / output. [EngineSettings, DisplaySnapshot, SubEngine] |
| `Source/DSP/SubGenerator.{h,cpp}` | Detects the low fundamental and synthesises a phase-continuous sub-harmonic (octave-down and/or unison... [SubSettings, SubGenerator] |
| `Source/DSP/SystemCheck.{h,cpp}` | Pure verdict from measured values. [Analysis, ReasonList, Thresholds, SystemCheck, ...] |
| `Source/DSP/ToneShift.{h,cpp}` | Duration-preserving tone shift of percussive signals. [ToneShiftSettings, ToneShift] |
| `Source/Parameters.{h,cpp}` | State-tree version written with every saved state (bump when a migration is needed). |
| `Source/PluginEditor.{h,cpp}` | [DooblrSubEditor] |
| `Source/PluginProcessor.{h,cpp}` | [DooblrSubProcessor] |
| `Source/Presets.{h,cpp}` | [Factory] |
| `Source/UI/ControlPanel.{h,cpp}` | A titled group of parameter controls laid out left to right (wrapping): rotary knobs, pill toggles and... [ControlPanel, Spec, Item, SegmentedToggle] |
| `Source/UI/OutputMeter.{h,cpp}` | Feeds one frame's peaks (linear) at a time in ms: VBlank does this from the tap. [OutputMeter, Channel] |
| `Source/UI/SubDisplay.{h,cpp}` | Spectrum (20-200 Hz) and five sub-band meters. [SubDisplay] |
| `Source/UI/Theme.{h,cpp}` | Flat, understated look for knobs, buttons and combo boxes. [DooblrLookAndFeel] |
| `Source/UI/VerdictPanel.{h,cpp}` | [VerdictPanel] |
| `Source/UndoHistory.{h,cpp}` | Undo/redo of the plug-in's own UI edits, built on juce::UndoManager. [ParameterUndoHistory, Change, Pending] |
| `Tests/CMakeLists.txt` | CMake build: DooblrSubTests |
| `Tests/SubTests.cpp` | dooblr-sub console tests: the real processor, DSP and editor. |
| `Tools/LoadCheck/CMakeLists.txt` | CMake build: DooblrSubLoadCheck |
| `Tools/LoadCheck/LoadCheck.cpp` | DooblrSubLoadCheck: loads a built or installed dooblr-sub.vst3 the way a host does. |
| `cmake/sign-bundle.sh` | Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes. |
| `scripts/build.sh` | Configure and build the dooblr-sub VST3, run the tests, and install the built bundle into the VST3 plug-in... |

### UI components

- `DooblrSubEditor` : AudioProcessorEditor, Timer, ChangeListener (PluginEditor.h)
- `ControlPanel` : Component (ControlPanel.h)
- `SegmentedToggle` : Component (ControlPanel.h)
- `OutputMeter` : Component (OutputMeter.h)
- `SubDisplay` : Component (SubDisplay.h)
- `DooblrLookAndFeel` : LookAndFeel_V4 (Theme.h)
- `VerdictPanel` : Component (VerdictPanel.h)

### Tests

- `Tests/SubTests.cpp` (8): Metadata, SubTrackingAndOctave, ToneShift, SystemCheck, NoDeadParameters, StatePresetsUndo, CpuAndNan, Editor
<!-- END AUTO-GENERATED -->
