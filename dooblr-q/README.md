# dooblr-q

A dynamic parametric EQ plug-in (VST3) built with [JUCE](https://juce.com) and CMake.

**Version 0.4.0:** six-band dynamic EQ with Tilt bands, per-band stereo width, three
phase modes (Zero Latency / Natural Phase / Linear Phase), a native JUCE editor with a
live frequency-response display over a background spectrum analyzer with Freeze, and
automated tests. (0.3.0 added the analyzer, Freeze and Tilt.)

The version shown in the editor comes from `project(dooblr-q VERSION ...)` in
`CMakeLists.txt` (via `JucePlugin_VersionString`); a test checks they match.

## Layout

```
CMakeLists.txt                 Project, pinned JUCE dependency, plug-in target (DooblrQ)
Source/
  DSP/SvfFilter.h/.cpp         TPT state-variable filter, coefficients, analytic response
  DSP/DynamicEQBand.h/.cpp     One band: smoothing, side-chain detector, envelope, dynamic gain, width
  DSP/FirEqEngine.h/.cpp       Natural/Linear phase: kernel design + partitioned FFT convolution
  Analysis/SpectrumAnalyzer.*  Lock-free analyzer FIFO, FFT analyzer, long-term average, freeze (no GUI)
  Parameters.h/.cpp            Parameter IDs, ranges, defaults (APVTS layout)
  PluginProcessor.h/.cpp       Buses, processBlock, bypass, state
  PluginEditor.h/.cpp          Editor layout and parameter attachments
  UI/Theme.h/.cpp              Colours and look-and-feel
  UI/ResponseCurveComponent.*  Frequency-response display with draggable band handles
Tests/
  CMakeLists.txt               Test executable + CTest registration
  PluginSmokeTests.cpp         Console tests against the real processor/editor
scripts/build.sh               Configure, build, test, copy bundle, report paths
```

The CMake target is `DooblrQ` (CMake target names are kept hyphen-free); the
product, bundle and host-visible plug-in name is **dooblr-q**.

## Features

Six bands (`params::numBands` in `Source/Parameters.h`), each with:

| Parameter | Range | Default |
|---|---|---|
| Enabled | on/off | off |
| Type | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Tilt | per band (below) |
| Frequency | 20 Hz – 20 kHz, logarithmic | per band (below) |
| Gain (static) | ±24 dB | 0 dB |
| Q | 0.1 – 18, logarithmic | 0.707 (shelves/cut), 1.0 (bells) |
| Dynamic | on/off | off |
| Range | ±24 dB (negative = cut when loud, positive = boost when loud, 0 = static) | 0 dB |
| Threshold | −60 – 0 dB | −24 dB |
| Attack | 0.1 – 200 ms | 10 ms |
| Release | 5 – 2000 ms | 120 ms |
| Width | 0 – 200 % stereo width inside the band's region (100 % = unchanged) | 100 % |

Band defaults: 1 Low Cut 30 Hz, 2 Low Shelf 120 Hz, 3 Bell 400 Hz,
4 Bell 1.5 kHz, 5 Bell 4 kHz, 6 High Shelf 10 kHz. All bands start disabled,
so a fresh instance is an exact pass-through.

Global: **Output Gain** (±24 dB), **Stereo Link** (on by default: one
detector drives both channels; off: each channel gets its own dynamic gain) and
**Phase Mode** (Zero Latency by default; see below).

Width and Phase Mode were added in 0.4.0. They come after every older parameter (old
parameter order and IDs are unchanged), and a session saved before they existed loads
them at their neutral defaults (100 %, Zero Latency).

### DSP

- Each band is a topology-preserving-transform state-variable filter (Simper
  / Cytomic SVF). It is stable for any coefficients and handles modulation
  without blowing up, which matters when the gain moves with the signal.
- Coefficients are recomputed every 16 samples from smoothed parameters
  (30 ms smoothing; frequency and Q in the log domain). Enabling/disabling a
  band crossfades over 10 ms.
- Dynamics (Bell, Low Shelf, High Shelf and Tilt): the band's input feeds a
  side-chain detector filter (band-pass at the band's frequency/Q for bells,
  low-pass/high-pass at the corner for shelves, and the unfiltered (broadband) input for
  Tilt, so a dynamic tilt follows the overall level), then a peak envelope follower
  with the attack/release times. The dynamic gain is
  `sign(range) × min(|range|, 0.5 × dB over threshold)`, a 2:1 ratio-style law
  with a 6 dB soft knee. The band then uses static gain + dynamic gain (total clamped to ±30 dB).
- Low/High Cut are 12 dB/octave with Q as resonance.
- **Tilt** pivots around the band frequency. Gain is the total tilt: +6 dB means
  −3 dB in the lows, 0 dB at the pivot and +3 dB in the highs (positive = brighter).
  It is one SVF: a high shelf of the full gain scaled by −gain/2, so it is as
  modulation-safe as the other bands and dynamics work on it. Q sets the steepness of
  the transition like a shelf's Q: 0.707 is the steepest without overshoot, lower is
  gentler, higher adds a dip/bump either side of the pivot.
  Tilt was added at the end of the type list (index 6), so sessions saved before it
  load unchanged; `stateVersion` stays 1. (A host automation lane recorded on a Type
  parameter stores a normalised 0–1 value, which now maps onto 7 choices instead of 6.)
- Zero latency. Bypass is an exact pass-through.
- No allocation, locks, logging or file access on the audio thread.

### Stereo width (per band)

Each band can widen or narrow the stereo image in its own frequency region, like
Pro-Q's per-band stereo controls. Internally the band works in mid/side: the side
signal S becomes `S + (width - 1) × region(S)`, where region is a unity-peak band-pass at
the band's frequency and Q for a bell, a low-pass at the corner for a low shelf and a
high-pass for a high shelf or tilt. So 0 % removes the side signal at the band centre
(mono there), 200 % doubles it, 100 % is unchanged; the mid signal is untouched, and side
content outside the region is kept. Low/High Cut and Notch remove content rather than
shape a region, so Width does nothing on them (the knob is dimmed). On a mono instance
Width is disabled. Exactly 100 % skips the mid/side path, so it is bit-exact.
Per-band Left/Right/Mid/Side placement (Pro-Q's other stereo option) is not implemented.

### Phase modes

| Mode | What it does | Latency @ 44.1 / 48 / 96 kHz |
|---|---|---|
| Zero Latency | The original minimum-phase SVF filters (bilinear transform). | 0 |
| Natural Phase | FIR built from each band's analogue prototype, magnitude **and** phase, so the response is not "cramped" near Nyquist and the phase is the analogue filter's. 4096-tap kernel with 768 samples of pre-delay. | 1024 / 1024 / 1792 samples (23 / 21 / 19 ms) |
| Linear Phase | Symmetric FIR from the analogue magnitude, no phase shift. 8192-tap Hann-windowed kernel. | 4352 / 4352 / 8448 samples (99 / 91 / 88 ms) |

Kernel lengths double at 88.2/96 kHz and quadruple above 100 kHz, so their length in
milliseconds stays about the same. Latency = kernel delay + 256 samples of partition
buffering. It is reported to the host with `setLatencySamples` and updated when the mode
changes. How quickly a host re-applies delay compensation after a change during playback
depends on the host (not tested in a DAW yet).

How it works:

- Convolution is uniformly partitioned overlap-save (256-sample partitions, 512-point FFTs);
  stereo runs as mid/side so the side kernel can include each band's Width.
- Dynamics still work: the zero-latency band filters and detectors run on a copy of the
  input to measure each band's dynamic gain, and the kernel is rebuilt from static +
  dynamic gains when anything has moved (at most every 512 samples at 44.1/48 kHz),
  crossfading to the new kernel over 256 samples. Because the audio is delayed and the
  detector is not, the gain change effectively has the mode's latency as look-ahead.
- Kernel rebuilds happen on the audio thread, in preallocated memory (no locks or
  allocation), so offline renders are deterministic.
- Bypass is latency-compensated: in Natural/Linear Phase the bypassed output is the input
  delayed by exactly the reported latency, and the EQ keeps running underneath so
  bypass/un-bypass is click-free and aligned.
- Switching modes changes the latency, so the output jumps; it is faded in over 256 samples.
- The display draws the analogue response in Natural/Linear Phase (what those modes
  play) and the bilinear response in Zero Latency.

Measured CPU (this Mac, Release, 48 kHz stereo, 512-sample blocks, 6 bands on with one
dynamic and one width band): Zero Latency ≈ 0.4 %, Natural ≈ 3.8 %, Linear ≈ 6.7 % of one core.

Limitations: FIR modes have a finite frequency resolution (5.9 Hz bins at 48 kHz for
Linear, 11.7 Hz for Natural), so very narrow/deep low-frequency features are smoothed
(e.g. a 0 % width null at 1 kHz is about −37 dB deep in Linear Phase vs −130 dB in Zero
Latency), and low, high-Q bands that ring longer than the kernel are truncated (most
audible in Natural Phase). Linear Phase has pre-ringing on sharp boosts, as all
linear-phase EQs do. With Stereo Link off, the FIR modes use the louder channel's dynamic
gain for both channels.

### Editor

Dark, resizable editor (default 980×640). The header has the Phase Mode selector. It has a frequency-response display computed
from the same filter maths as the DSP. It draws the static curve and, when dynamics are
active, the live curve, using each band's current dynamic gain. The display redraws
immediately on any parameter change (automation from the audio thread is picked up within
one 60 Hz frame).

Each band has a handle that sits on that band's own response at its frequency, so the
curve passes through it:

- Bell: at the gain.
- Shelves: at the shelf midpoint (half the gain, where the shelf's slope is centred).
  Dragging the handle 6 dB moves the shelf gain by 12 dB.
- Low/High Cut: at the corner level (−3 dB at Q 0.707).
- Notch: on the 0 dB line, since its centre is −∞.
- Tilt: on the curve at the pivot (always 0 dB). Dragging sideways moves the pivot;
  dragging up/down changes the tilt amount (moving 3 dB on the scale = 6 dB of tilt,
  i.e. the high end follows the mouse). The amount is shown above the handle.

Handle mouse actions:

- Click a handle to select its band.
- Drag it to change frequency and, for bell, shelf and tilt types, gain.
- Dragging a disabled band's handle switches the band on.
- Double-click a handle to toggle its band on or off.
- The selected band also shows two small **bandwidth handles** on the curve at
  centre ÷ 2^(BW/2) and centre × 2^(BW/2) (BW = bandwidth in octaves from Q). Drag either
  one sideways: outward widens (lower Q), inward narrows; the other mirrors it so the
  centre frequency stays put, and a readout shows octaves and Q while dragging. The
  cursor becomes a left-right arrow over them. The same pair is shown for shelves, cuts
  and tilt, where it marks the transition width that Q controls.
- Scroll over a handle to change Q; Option/Alt-drag a handle vertically to change Q.
- A band with a width other than 100 % shows it under its handle (e.g. "W 160%").
- Disabled bands show grey, hollow handles.

Below the display are a band selector and the selected band's controls: on/off, type,
frequency, gain, Q, dynamic on/off, range, threshold, attack, release. Controls that
currently have no audible effect (e.g. gain on a cut filter) are dimmed but still work.

### Spectrum analyzer and Freeze

The response display has a spectrum analyzer behind the grid, curve and handles.

- **Spectrum** toggle: shows/hides it. While hidden (or while no editor is open) the
  audio thread does not feed it at all.
- **Post-EQ / Pre-EQ**: what it analyses (default Post-EQ, after the output gain).
  Channels are summed to mono.
- **Tilt 0 / 3 / 4.5 dB/oct**: display slope around 1 kHz (default 4.5), so typical
  music reads roughly flat. It only changes the drawing.
- Scale: the analyzer has its own dBFS scale, 0 dBFS at the top to −90 dBFS at the
  bottom (labelled −30 and −60 dBFS on the right), independent of the EQ's ±24 dB scale.
- The live spectrum (soft blue-grey) has an instant attack and falls at 36 dB/s.

Freeze is a long-term snapshot:

- From the moment you Clear (or un-freeze), the analyzer keeps a running **power
  average** of every FFT frame, plus a per-bin peak-hold. The readout shows
  `Averaging: N s`.
- **Freeze** captures that average as a static pink overlay (`Frozen: N s` = how much
  audio it covers). The live spectrum keeps moving on top, so you can compare what is
  playing now with what played before, or the same section before/after an EQ move.
- **Peak**: also draws the peak-hold as a dashed line (the frozen one while frozen).
- Pressing **Freeze** again hides the overlay and starts a new averaging window.
  **Clear** removes the overlay and restarts the average.
- The analyzer settings (visibility, source, tilt, peak) live in the plug-in instance
  and survive closing/reopening the editor, but they and the frozen capture are
  **not saved** with the session or preset.

Analyzer details: 4096-point FFT, periodic Hann window, 75% overlap (hop 1024; 11.7 Hz
bins and a frame every 21 ms at 48 kHz). Calibration: a sine of peak amplitude A reads
20·log10(A) dBFS, so a full-scale sine reads 0 dBFS when it sits on a bin and up to
1.42 dB lower between bins (Hann scalloping). The floor is −120 dBFS. On screen, each
pixel column shows the loudest bin within ±1/24 octave, interpolating at low
frequencies where the bins are sparser.

CPU / real-time: the audio thread only mixes to mono and copies into a preallocated
lock-free FIFO (32768 samples), and only while an editor shows the analyzer (an atomic
flag). If the editor stalls, the FIFO drops the newest samples instead of blocking. The
FFT, averaging and drawing run on the message thread in the 60 Hz display timer. A test
checks that `processBlock` output is bit-identical with the analyzer on and off.

## Dependencies

| Tool | Notes |
|------|-------|
| CMake ≥ 3.22 | Tested with 3.31.2 |
| C++20 compiler | macOS: Xcode / Apple clang (tested: Apple clang 21, macOS 27 SDK, arm64). Windows: Visual Studio 2022. Linux: GCC ≥ 10 or Clang ≥ 12 plus JUCE's Linux packages. |
| git + network at **configure** time | `FetchContent` downloads JUCE once into `build/_deps`. |
| Ninja | Optional; used by `scripts/build.sh` if installed, otherwise Unix Makefiles. |

JUCE is pinned to the official release tag **8.0.15** (`DOOBLRQ_JUCE_TAG`).
To use a local JUCE checkout instead: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

No runtime network dependencies (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`).

## Building

```bash
./scripts/build.sh
```

Configures `build/` in Release, builds `DooblrQ_VST3`, builds and runs the
tests with CTest, and then installs the bundle, replacing only `dooblr-q.vst3` in the VST3 folder:

- The install goes to `/Library/Audio/Plug-Ins/VST3` if dooblr-q is already installed there.
  Otherwise it goes to `~/Library/Audio/Plug-Ins/VST3`.
- It never uses sudo. If the folder isn't writable, the script stops and reports it.
- The installed copy is checked against the build output and verified with
  `codesign --verify --deep --strict`.

Rescan plug-ins in your DAW (or restart it) to load a new build.

Options:

```bash
BUILD_DIR=build-debug CONFIG=Debug INSTALL_VST3=0 ./scripts/build.sh   # Debug build, no install
RUN_TESTS=0 ./scripts/build.sh                                         # skip tests
INSTALL_VST3=0 ./scripts/build.sh                                      # build only
INSTALL_VST3_DIR=~/Library/Audio/Plug-Ins/VST3 ./scripts/build.sh      # choose the folder
COPY_TO_PROJECT=1 ./scripts/build.sh                                   # also copy to ./dooblr-q.vst3
JOBS=4 ./scripts/build.sh
```

Manual equivalent:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DooblrQ_VST3 --parallel
cmake --build build --config Release --target DooblrQTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

The build output is `build/DooblrQ_artefacts/Release/VST3/dooblr-q.vst3`
(the script reads the real path from
`build/artefact-paths/DooblrQ_VST3-<Config>.txt`, generated by CMake).

### Adding formats later

```bash
cmake -S . -B build -DDOOBLRQ_PLUGIN_FORMATS="VST3;AU"
cmake --build build --target DooblrQ_AU
```

Windows VST3 uses the same `CMakeLists.txt` with `-G "Visual Studio 17 2022"`.

## Plug-in metadata

| | |
|---|---|
| Product name | dooblr-q |
| Type | Audio effect, VST3 categories `Fx|EQ|Dynamics` |
| Manufacturer / plug-in code | `Dblr` / `Dblq` |
| Bundle ID | `com.dooblr.dooblrq` |
| Buses | Mono→mono or stereo→stereo (default stereo) |
| MIDI | None |
| Latency / tail | 0 (Zero Latency), 1024 (Natural), 4352 (Linear) samples at 48 kHz / 0.5 s |
| State | APVTS value tree with a `stateVersion` attribute (currently 1; analyzer settings are not saved) |

## Tests

`Tests/PluginSmokeTests.cpp` links the plug-in's shared-code target, so it
tests the same compiled code that goes into the VST3:

- instantiation, factory, name, MIDI flags, latency, parameter count;
- supported/rejected bus layouts;
- neutral settings (defaults, and all bands enabled at 0 dB with dynamics at 0 dB range) are identical to the input;
- +12 dB bell at 1 kHz raises a 1 kHz sine by 12 ± 0.3 dB and leaves 60 Hz and 16 kHz within 0.5 dB.
  The display maths agrees with the measured gain, and a 200 Hz low cut attenuates 50 Hz by > 20 dB;
- dynamic band (threshold −30 dB, range ∓12 dB): a −50 dBFS tone is unchanged,
  a −6 dBFS tone is cut (or boosted) by up to the range. The live display value matches;
- output stays finite under random per-block automation of every parameter
  (44.1 and 192 kHz) and at extreme settings;
- static EQ output is identical with one large block vs mixed block sizes,
  zero-length blocks are handled, dynamic processing with varying sizes is finite;
- bypass is bit-exact with active settings (mono and stereo);
- buffer channels beyond the bus are untouched;
- state round-trip restores every parameter; malformed/foreign state is ignored;
- editor: opens at default size, controls drive the selected band's parameters,
  re-selecting a band rebinds them, it resizes, paints, and closes cleanly (twice).

- curve follows handles. Dragging a disabled bell up and a disabled low shelf down:
  - enables both bands and moves the handles to the drop points;
  - puts the computed curve level at each handle frequency equal to the handle level;
  - renders curve pixels beside each handle that match the computed curve (within 1.5 px).

  The live dynamic curve equals static + dynamic gain. Double-click toggles a band.
  Off-thread automation schedules a redraw.

- spectrum analyzer core (no GUI): on-bin sine calibration (−6.02 dBFS for A = 0.5),
  1 kHz full-scale in bin 85, live release, white noise flat within 1 dB, silence at
  exactly the floor with no NaN/Inf, FIFO overflow bounded and counted, variable block
  sizes lose nothing, two tones both present in the average, freeze unaffected by later
  input, unfreeze/clear reset, sample-rate change resets;
- analyzer transparency: `processBlock` output is bit-identical with the analyzer off,
  on post-EQ and on pre-EQ; post-EQ analysis includes the output gain, pre-EQ does not;
- Tilt: display maths and measured audio give −3 / 0 / +3 dB for a +6 dB tilt at 1 kHz
  (and mirror for −6 dB); Q changes the transition; dynamic tilt tilts darker on loud
  material; dragging the handle sets the amount and the handle stays on the curve;
- a hand-written pre-Tilt (v0.2.x format) saved state restores every band type;
- phase modes: reported latency per mode at 44.1/48/96 kHz and after switching while
  running; a +12 dB bell measures +12 dB at its centre in every mode; near Nyquist the
  FIR modes follow the analogue design (+7.6 dB at 20 kHz for a +9 dB 15 kHz bell at
  44.1 kHz, vs +1.4 dB bilinear); analogue prototypes match the digital filters below
  Nyquist; the linear-phase impulse response peaks at the reported latency and is
  symmetric; neutral settings are identity after delay compensation (error < 2e-7);
  bypass is the input delayed by exactly the latency, aligned across toggles; random
  mode switching stays finite; dynamics give the same gain in all three modes; CPU per mode;
- width: 0 % silences in-band side (−130 dB zero latency, −37 dB linear) while keeping
  side content outside the band and the mid; 200 % gives +6.02 dB; 100 % is unchanged
  (bit-exact in Zero Latency); mono ignores width; old sessions load with neutral width;
- editor: dragging the upper bandwidth handle 40 px outward lowers Q, keeps the centre,
  and moves the lower handle 40 px the other way; inward narrows; scrolling changes Q;
  Width is disabled on mono;
- editor: version label = `JucePlugin_VersionString` = CMake project version; Freeze,
  Peak, Clear, Spectrum controls and the readout; closing the editor stops the feed.

Set `DOOBLRQ_EDITOR_SNAPSHOT=/path/editor.png` when running
`build/Tests/DooblrQTests` to save images of the rendered editor. It writes `editor.png`,
plus `editor-drag-before.png`, `editor-drag-after.png`, `editor-drag-dynamic.png`,
`editor-tilt.png`, `editor-spectrum-live.png`, `editor-spectrum-frozen.png` and
`editor-width-handles.png`.

## Validation that still needs doing manually

1. **pluginval** (Tracktion, not bundled): download from
   <https://github.com/Tracktion/pluginval/releases>, then
   `pluginval --strictness-level 10 --validate-in-process /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3`.
2. **Steinberg VST3 validator** (`validator` from the VST3 SDK).
3. **DAW**: rescan plug-ins (or restart the host) after each install, then load it in REAPER, Ableton
   Live, Bitwig, Cubase or Studio One. Check scanning, mono/stereo insert,
   automation, bypass, editor resize, and project save/reload. Logic Pro needs
   AU, which is not built yet.
