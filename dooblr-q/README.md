# dooblr-q

A dynamic parametric EQ plug-in (VST3) built with [JUCE](https://juce.com) and CMake.

**Version 0.6.1:** six-band dynamic EQ with Tilt bands, Width (bandwidth in octaves), three
phase modes (Zero Latency / Natural Phase / Linear Phase), a native JUCE editor with a
live frequency-response display over a smoothed spectrum analyzer with a continuously
updating Freeze overlay, 50-step undo/redo, on/off switches in the band tabs, and
automated tests. (0.3.0 added the analyzer, Freeze and Tilt. 0.4.1 removed 0.4.0's per-band
stereo width: "Width" now means the band's bandwidth. 0.5.0 added undo/redo, the band-tab
switches, the live Freeze overlay and display smoothing. 0.5.1 ties averaging to Freeze: it
starts fresh when Freeze is turned on and stops and clears when it is turned off. 0.6.0
makes band 6 a High Cut (low-pass) by default, alongside band 1's Low Cut (high-pass), and
gives every band type 2D handle dragging: horizontal = frequency, vertical = gain or, for
cuts and notches, Q. 0.6.1 colours dynamic activity per band, only over the range the
moving band affects, instead of recolouring the whole curve.)

The version shown in the editor comes from `project(dooblr-q VERSION ...)` in
`CMakeLists.txt` (via `JucePlugin_VersionString`); a test checks they match.

## Layout

```
CMakeLists.txt                 Project, pinned JUCE dependency, plug-in target (DooblrQ)
Source/
  DSP/SvfFilter.h/.cpp         TPT state-variable filter, coefficients, analytic response
  DSP/DynamicEQBand.h/.cpp     One band: smoothing, side-chain detector, envelope, dynamic gain
  DSP/FirEqEngine.h/.cpp       Natural/Linear phase: kernel design + partitioned FFT convolution
  Analysis/SpectrumAnalyzer.*  Lock-free analyzer FIFO, FFT analyzer, long-term average, freeze (no GUI)
  Analysis/SpectrumDisplay.*   Display smoothing: 1/12-octave + per-point rise/fall (no GUI)
  UndoHistory.h/.cpp           Undo/redo of UI gestures on juce::UndoManager (50 steps)
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
| Q, shown as **Width** | Q 0.1 – 18 (logarithmic) = 6.67 – 0.08 octaves | Q 0.707 (shelves/cut), 1.0 (bells) |
| Dynamic | on/off | off |
| Range | ±24 dB (negative = cut when loud, positive = boost when loud, 0 = static) | 0 dB |
| Threshold | −60 – 0 dB | −24 dB |
| Attack | 0.1 – 200 ms | 10 ms |
| Release | 5 – 2000 ms | 120 ms |

Band defaults: 1 Low Cut 30 Hz, 2 Low Shelf 120 Hz, 3 Bell 400 Hz,
4 Bell 1.5 kHz, 5 Bell 4 kHz, 6 High Cut 18 kHz (band 1 is a high-pass and band 6 a
low-pass, both Q 0.707 Butterworth). All bands start disabled, so a fresh instance is an
exact pass-through; dragging a handle switches its band on. Saved sessions keep the band
types they were saved with (each type is stored explicitly).

Global: **Output Gain** (±24 dB), **Stereo Link** (on by default: one
detector drives both channels; off: each channel gets its own dynamic gain) and
**Phase Mode** (Zero Latency by default; see below).

**Width** is the band's Q shown as a bandwidth in octaves, BW = 2/ln2 · asinh(1/(2Q))
(Q 1.414 = 1 octave, Q 0.667 = 2 octaves). The editor knob works in octaves (clockwise =
wider) with a readout like `1.00 oct (Q 1.41)`, but the stored parameter is still Q
(`bN_q`), so sessions are unaffected. For a bell, the Width edges centre ÷ 2^(BW/2) and
centre × 2^(BW/2) are where the band is at half its dB gain (so for a −6 dB bell, its
−3 dB points). For shelves, cuts and tilt the same Q controls the transition.

Phase Mode was added in 0.4.0 after every older parameter (old order and IDs unchanged);
sessions saved before it load at Zero Latency. 0.4.0 also had a per-band stereo
`bN_width` parameter; it was removed in 0.4.1, and 0.4.0 sessions containing it load
normally (the unknown parameter is ignored and not saved again).

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
  both channels share one kernel, each with its own convolution state.
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
dynamic band): Zero Latency ≈ 0.4 %, Natural ≈ 1.9 %, Linear ≈ 3.4 % of one core.

Limitations: FIR modes have a finite frequency resolution (5.9 Hz bins at 48 kHz for
Linear, 11.7 Hz for Natural), so very narrow/deep low-frequency features are smoothed
), and low, high-Q bands that ring longer than the kernel are truncated (most
audible in Natural Phase). Linear Phase has pre-ringing on sharp boosts, as all
linear-phase EQs do. With Stereo Link off, the FIR modes use the louder channel's dynamic
gain for both channels.

### Editor

Dark, resizable editor (default 980×640). The header has the Phase Mode selector. It has a frequency-response display computed
from the same filter maths as the DSP. It draws the combined static curve (always in the
accent colour). Dynamic activity is shown per band: while a band's dynamics are actually
moving its gain, the area between the curve without and with that band's dynamic gain is
filled in the band's colour, and the live curve is drawn in that colour over the same span,
only where that band's own change exceeds 0.1 dB. A bell shows a local dip or bump around
its centre, a shelf only its shelf side, a tilt its moving side(s); several active bands
each show their own region, and a band with dynamics on but idle (no gain change) shows
nothing. (Before 0.6.1, any band with dynamics switched on greyed the static curve and drew
the whole live curve in amber across the full spectrum.) The display redraws
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
- Drag it in two dimensions, for every band and every type: **horizontal = frequency**;
  **vertical = gain** for Bell, Low Shelf, High Shelf and Tilt, and **vertical = Q** for
  the gainless Low Cut, High Cut and Notch. Each drag is one undo step.
  - Bell: the handle is the gain. Shelves: the handle is the shelf midpoint (half the gain).
    Tilt: the handle stays on its 0 dB pivot and moving up/down by d dB changes the tilt by
    2d (shown as `Tilt +x.x dB` above it).
  - Low/High Cut: up = higher Q, i.e. a bigger resonant peak at the cutoff; down = lower Q,
    a gentler corner. The handle sits on the curve at the cutoff, whose level is exactly
    20·log10(Q): −3 dB at Q 0.707, 0 dB at Q 1, +6 dB at Q 2, +12 dB at Q 4; Q range 0.1–18.
  - Notch: up = narrower (higher Q), down = wider. Its centre is −∞ dB, so its handle height
    shows the Q on the same scale (0 dB line = Q 1) and a `Q x.xx` label is shown above it.
  - The vertical change is relative to where the drag starts, so a purely sideways drag
    never changes gain or Q.
- Dragging a disabled band's handle switches the band on.
- Double-click a handle to toggle its band on or off.
- The selected band also shows two small **bandwidth handles** on the curve at
  centre ÷ 2^(BW/2) and centre × 2^(BW/2) (BW = bandwidth in octaves from Q). Drag either
  one sideways: outward widens (lower Q), inward narrows; the other mirrors it so the
  centre frequency stays put, and a readout (`Width 2.27 oct (Q 0.57)`) shows while dragging. The
  cursor becomes a left-right arrow over them. The same pair is shown for shelves, cuts
  and tilt, where it marks the transition width that Q controls.
- Scroll over a handle to change Q; Option/Alt-drag a handle vertically to change Q (any type).
- Disabled bands show grey, hollow handles.

Below the display are six band tabs and the selected band's controls: on/off, type,
frequency, gain, Width, dynamic on/off, range, threshold, attack, release. Controls that
currently have no audible effect (e.g. gain on a cut filter) are dimmed but still work.

Each band tab has a small **on/off switch on its left** (bound to that band's on/off
parameter): clicking the switch turns the band on or off without selecting the tab;
clicking anywhere else on the tab selects the band. The switch is filled in the band's
colour when on and drawn as a dim outline when off. It follows the parameter from
anywhere (Band on toggle, double-click on a handle, automation) and is undoable.

### Undo / redo

- **Cmd-Z** (macOS) / **Ctrl-Z** (Windows) undoes; **Cmd-Shift-Z** / **Ctrl-Shift-Z** or
  **Cmd-Y** / **Ctrl-Y** redoes. Small **Undo** / **Redo** buttons in the header do the same
  and are greyed out when there is nothing to undo or redo.
- Up to **50 actions** are kept (the oldest are dropped).
- One action = one user gesture: a whole knob or slider drag, a handle drag (frequency and
  gain together), a bandwidth-handle drag, an Option-drag, a type or phase-mode change, a
  toggle, a band-tab switch click, a double-click on a handle. Quick scroll-wheel steps on
  one control (less than 0.6 s apart) merge into one action.
- Only the plug-in's own UI creates entries. Host automation (and loading a session or
  preset) changes parameters without gestures and never fills the history; loading a
  state clears it. Undo/redo report their changes to the host as normal edits.
- Implementation: `ParameterUndoHistory` (Source/UndoHistory.*) listens for parameter
  gestures and records one `juce::UndoManager` transaction per gesture. It deliberately
  does not pass an UndoManager to the `AudioProcessorValueTreeState`: APVTS records into
  it from its 10–50 Hz flush timer, which would also record host automation and split
  gestures across timer ticks.
- Keyboard focus: the editor takes keyboard focus when you click anywhere in it, and
  claims Cmd/Ctrl-Z(-Shift)/Y only (other keys go to the host). **Host caveat:** whether
  the host passes the key to the plug-in window at all is up to the host. In Ableton Live
  the plug-in window must be the focused window (click inside it first); if Live still
  handles Cmd-Z itself (Live's own undo), use the Undo / Redo buttons.

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
- The live spectrum (soft blue-grey): per FFT frame an instant attack and a 30 dB/s fall,
  then display smoothing (below).

Freeze averages the spectrum, and only while it is on:

- With **Freeze** off there is no long-term averaging at all: just the live spectrum
  (with its light display smoothing). The readout shows `Live`.
- Turning **Freeze** on starts a fresh running **power average** of every FFT frame from
  that moment, drawn as a pink overlay. `Frozen: N s` counts the audio since Freeze was
  engaged (only FFT frames played entirely after that moment are averaged, so the first
  one lands ~85 ms in and nothing from before leaks in). While frozen the average keeps accumulating the live audio and the overlay is
  redrawn every frame, gliding as it changes; the live spectrum keeps moving on top.
- Turning **Freeze** off stops the averaging and clears the overlay. Turning it on
  again starts a new average from zero.
- **Clear** resets the peak-hold and, while frozen, restarts the average from zero
  (Freeze stays on).
- **Peak**: also draws the peak-hold since the last Clear as a dashed line (the peak-hold
  runs whether or not Freeze is on).
- The readout counts in 0.1 s steps in a fixed-width font (it only repaints when the
  text changes, so it does not flicker or jitter).
- The analyzer settings (visibility, source, tilt, peak, freeze) live in the plug-in
  instance and survive closing/reopening the editor, but they are **not saved** with
  the session or preset.

Display smoothing (message thread only; the audio thread is unchanged):

- Frequency: the live spectrum and the overlay are drawn on a 480-point log-frequency
  grid (20 Hz–20 kHz). Each point is the power average of the bins within its 1/12-octave
  band; at low frequencies, where a band holds fewer than ~3 bins, it is a Catmull-Rom
  interpolation between the neighbouring bins (limited so it never overshoots), blended
  smoothly into the average. As with any fractional-octave smoothing, a pure tone reads
  lower than its per-bin level (a −6 dBFS 1 kHz sine shows about −11 dBFS); the dashed
  peak-hold line is not smoothed and still shows tone peaks.
- Time: per grid point a one-pole filter in dB, advanced by the real elapsed time (so it
  behaves the same at any frame rate): live rise 30 ms, fall 160 ms; the overlay glides
  with 250 ms.
- Drawing: quadratic-curve paths through the grid points (no polyline corners).

Analyzer details: 4096-point FFT, periodic Hann window, 75% overlap (hop 1024; 11.7 Hz
bins and a frame every 21 ms at 48 kHz). Calibration: a sine of peak amplitude A reads
20·log10(A) dBFS, so a full-scale sine reads 0 dBFS when it sits on a bin and up to
1.42 dB lower between bins (Hann scalloping). The floor is −120 dBFS. On screen the
live spectrum and overlay are smoothed as described above; the peak-hold line shows the
loudest bin within ±1/24 octave.

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

- The install goes to `/Library/Audio/Plug-Ins/VST3`, unless dooblr-q is installed only in
  `~/Library/Audio/Plug-Ins/VST3` (then that copy is updated).
- It never uses sudo. If the folder isn't writable, the script stops and reports it.
- The installed copy is checked against the build output, must carry no
  `com.apple.quarantine` flag, and is verified with `codesign --verify --deep --strict`.
- Finally the installed bundle is loaded like a host would load it, by
  `DooblrQLoadCheck` (Tools/LoadCheck): `dlopen` + `GetPluginFactory`, then JUCE's VST3
  hosting scans it, instantiates it, checks the version and processes audio. The script fails
  if this fails (`LOAD_CHECK=0` skips it).

**Why the quarantine check:** this project lives in an iCloud-synced (File Provider)
`~/Documents`, and the bundle folder in the build tree picked up `com.apple.quarantine`.
Copying it preserved the flag. A quarantined ad-hoc-signed plug-in still passes `codesign`,
but macOS refuses to load it into a host ("library load disallowed by system policy";
Finder shows "Apple could not verify dooblr-q.vst3 is free of malware"). Ableton then reports
"The bundle couldn't be loaded" / "not a plugin". The build strips the flag from the build
output after linking (POST_BUILD), and the install copies with `ditto --noqtn` and checks
again.

Rescan plug-ins in your DAW (or restart it) to load a new build.

Options:

```bash
BUILD_DIR=build-debug CONFIG=Debug INSTALL_VST3=0 ./scripts/build.sh   # Debug build, no install
RUN_TESTS=0 ./scripts/build.sh                                         # skip tests
INSTALL_VST3=0 ./scripts/build.sh                                      # build only
INSTALL_VST3_DIR=~/Library/Audio/Plug-Ins/VST3 ./scripts/build.sh      # choose the folder
LOAD_CHECK=0 ./scripts/build.sh                                        # skip the host-style load test
COPY_TO_PROJECT=1 ./scripts/build.sh                                   # also copy to ./dooblr-q.vst3
JOBS=4 ./scripts/build.sh
```

Manual equivalent:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DooblrQ_VST3 --parallel
cmake --build build --config Release --target DooblrQTests --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target DooblrQLoadCheck --parallel
"$(cat build/artefact-paths/DooblrQLoadCheck-Release.txt)" /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3
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

- per-band dynamic regions: an idle dynamic bell draws nothing; a loud 1 kHz tone gives one
  region confined to ~155 Hz–6 kHz whose depth at 1 kHz equals the dynamic gain, with no
  change at 60 Hz or 15 kHz; a dynamic low shelf driven by noise gets its own region from
  the bottom edge to ~620 Hz and nothing at 12 kHz, alongside the bell's; a static band has
  none; against the same display with dynamics off, pixels change at 40 Hz and 1 kHz and
  none at 10 or 15 kHz; dynamics off removes the regions.
- band 1/6 defaults and 2D drag: band 1 = Low Cut 30 Hz and band 6 = High Cut 18 kHz
  (Q 0.707, off), handles at −3 dB. For all seven types on band 1 and on band 6, a drag
  60 px up and from 1 to 2 kHz sets the frequency and raises gain (Bell = Δ dB, shelves and
  Tilt = 2Δ) or Q (cuts and Notch: Q = 10^(Δ/20)) without touching the other; the handle
  ends under the pointer (Tilt: on its pivot) and on the drawn curve; dragging down lowers
  it; each drag is one undo step and two undos restore the band. A sideways drag of a Q-18
  cut keeps Q 18.

- spectrum analyzer core (no GUI): on-bin sine calibration (−6.02 dBFS for A = 0.5),
  1 kHz full-scale in bin 85, live release, white noise flat within 1 dB, silence at
  exactly the floor with no NaN/Inf, FIFO overflow bounded and counted, variable block
  sizes lose nothing; the average is idle and empty while Freeze is off, starts at zero
  on engage, contains both tones played while frozen, keeps accumulating while frozen
  (a 9 kHz tone raises it there by ~80 dB), stops and clears on disengage, restarts fresh
  on re-engage; Clear restarts it while frozen and resets the peak-hold either way;
  sample-rate change resets;
- display smoothing: flat input stays flat; white noise spread across 1–20 kHz 4.9 dB per
  bin / 1.9 dB old display → 1.1 dB smoothed; live flicker at 5 kHz 0.73 → 0.10 dB per frame;
  smaller low-end corners; a tone still peaks at its frequency; rise/fall time constants
  hold at any frame rate and the fall is slower than the rise;
- undo/redo: 300 host-automation changes (message thread and another thread) create no
  entries; a knob drag, a handle drag (frequency + gain, or frequency + Q), a type change, a band-tab switch,
  a double-click enable and a bandwidth-handle drag are one step each; undo walks back
  through them exactly, redo reapplies; a new edit clears redo; Cmd/Ctrl-Z,
  Cmd/Ctrl-Shift-Z and Cmd/Ctrl-Y work and other keys are not claimed; the Undo/Redo buttons
  work and their enabled state follows the history; quick wheel ticks merge; 60 actions keep
  exactly the last 50; loading a state clears the history;
- band-tab switches: one per tab, on its left and inside it; states follow the
  parameters; clicks on the switch toggle the band without selecting the tab, clicks on
  the tab select it without toggling; undoable; on = band colour, off = dim;
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
- Width: Q 1.414 = 1.000 oct, Q 0.667 = 1.999 oct, round trip over the Q range; for a −6 dB
  bell at Widths of 1, 2 and 3 octaves the measured level at the Width edges is −3.0 dB
  (±0.04) in Zero Latency and Linear Phase, and a wider Width cuts more at 1.5 kHz;
  a v0.4.0 state containing `bN_width` loads and the obsolete IDs are not re-saved;
- editor: dragging the upper bandwidth handle 40 px outward lowers Q, keeps the centre,
  and moves the lower handle 40 px the other way; inward narrows; scrolling changes Q;
  the Width knob shows octaves and setting 1.00 / 2.00 oct gives Q 1.414 / 0.667;
- editor: version label = `JucePlugin_VersionString` = CMake project version; Freeze,
  Peak, Clear, Spectrum controls and the readout; `Live` and no averaging while Freeze is
  off; Freeze on reads `Frozen: 0.0 s` and counts up from there while the overlay keeps
  changing with new audio; Freeze off clears it; display levels stay finite; closing the
  editor stops the feed.

Set `DOOBLRQ_EDITOR_SNAPSHOT=/path/editor.png` when running
`build/Tests/DooblrQTests` to save images of the rendered editor. It writes `editor.png`,
plus `editor-drag-before.png`, `editor-drag-after.png`, `editor-drag-dynamic.png`,
`editor-tilt.png`, `editor-spectrum-live.png`, `editor-spectrum-frozen.png`,
`editor-width-handles.png`, `editor-overview.png` (spectrum + frozen overlay + width handles)
`editor-v0.5.1.png` (the same, showing the band-tab switches and the Undo/Redo buttons)
`editor-v0.6.0.png` (the default band 1 high-pass and band 6 low-pass switched on) and
`editor-v0.6.1.png` (a dynamic low shelf and a dynamic bell, each with its own region).

## Validation that still needs doing manually

1. **pluginval** (Tracktion, not bundled): download from
   <https://github.com/Tracktion/pluginval/releases>, then
   `pluginval --strictness-level 10 --validate-in-process /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3`.
2. **Steinberg VST3 validator** (`validator` from the VST3 SDK).
3. **DAW**: rescan plug-ins (or restart the host) after each install, then load it in REAPER, Ableton
   Live, Bitwig, Cubase or Studio One. Check scanning, mono/stereo insert,
   automation, bypass, editor resize, and project save/reload. Logic Pro needs
   AU, which is not built yet.
