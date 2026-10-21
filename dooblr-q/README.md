# dooblr-q

A dynamic parametric EQ plug-in (VST3) built with [JUCE](https://juce.com) and CMake.

**Current milestone (2):** six-band dynamic EQ with a native JUCE editor, a live
frequency-response display, and automated smoke tests.

## Layout

```
CMakeLists.txt                 Project, pinned JUCE dependency, plug-in target (DooblrQ)
Source/
  DSP/SvfFilter.h/.cpp         TPT state-variable filter, coefficients, analytic response
  DSP/DynamicEQBand.h/.cpp     One band: smoothing, side-chain detector, envelope, dynamic gain
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
| Type | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch | per band (below) |
| Frequency | 20 Hz – 20 kHz, logarithmic | per band (below) |
| Gain (static) | ±24 dB | 0 dB |
| Q | 0.1 – 18, logarithmic | 0.707 (shelves/cut), 1.0 (bells) |
| Dynamic | on/off | off |
| Range | ±24 dB (negative = cut when loud, positive = boost when loud, 0 = static) | 0 dB |
| Threshold | −60 – 0 dB | −24 dB |
| Attack | 0.1 – 200 ms | 10 ms |
| Release | 5 – 2000 ms | 120 ms |

Band defaults: 1 Low Cut 30 Hz, 2 Low Shelf 120 Hz, 3 Bell 400 Hz,
4 Bell 1.5 kHz, 5 Bell 4 kHz, 6 High Shelf 10 kHz. All bands start disabled,
so a fresh instance is an exact pass-through.

Global: **Output Gain** (±24 dB) and **Stereo Link** (on by default: one
detector drives both channels; off: each channel gets its own dynamic gain).

### DSP

- Each band is a topology-preserving-transform state-variable filter (Simper
  / Cytomic SVF). It is stable for any coefficients and handles modulation
  without blowing up, which matters when the gain moves with the signal.
- Coefficients are recomputed every 16 samples from smoothed parameters
  (30 ms smoothing; frequency and Q in the log domain). Enabling/disabling a
  band crossfades over 10 ms.
- Dynamics (Bell, Low Shelf, High Shelf only): the band's input feeds a
  side-chain detector filter (band-pass at the band's frequency/Q for bells,
  low-pass/high-pass at the corner for shelves), then a peak envelope follower
  with the attack/release times. The dynamic gain is
  `sign(range) × min(|range|, 0.5 × dB over threshold)`, a 2:1 ratio-style law
  with a 6 dB soft knee. The band then uses static gain + dynamic gain (total clamped to ±30 dB).
- Low/High Cut are 12 dB/octave with Q as resonance.
- Zero latency. Bypass is an exact pass-through.
- No allocation, locks, logging or file access on the audio thread.

### Editor

Dark, resizable editor (default 960×600). It has a frequency-response display computed
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

Handle mouse actions:

- Click a handle to select its band.
- Drag it to change frequency and, for bell and shelf types, gain.
- Dragging a disabled band's handle switches the band on.
- Double-click a handle to toggle its band on or off.
- Disabled bands show grey, hollow handles.

Below the display are a band selector and the selected band's controls: on/off, type,
frequency, gain, Q, dynamic on/off, range, threshold, attack, release. Controls that
currently have no audible effect (e.g. gain on a cut filter) are dimmed but still work.

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
| Latency / tail | 0 samples / 0.5 s |
| State | APVTS value tree with a `stateVersion` attribute (currently 1) |

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

Set `DOOBLRQ_EDITOR_SNAPSHOT=/path/editor.png` when running
`build/Tests/DooblrQTests` to save images of the rendered editor. It writes `editor.png`,
plus `editor-drag-before.png`, `editor-drag-after.png` and `editor-drag-dynamic.png`
from the handle-drag test.

## Validation that still needs doing manually

1. **pluginval** (Tracktion, not bundled): download from
   <https://github.com/Tracktion/pluginval/releases>, then
   `pluginval --strictness-level 10 --validate-in-process /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3`.
2. **Steinberg VST3 validator** (`validator` from the VST3 SDK).
3. **DAW**: rescan plug-ins (or restart the host) after each install, then load it in REAPER, Ableton
   Live, Bitwig, Cubase or Studio One. Check scanning, mono/stereo insert,
   automation, bypass, editor resize, and project save/reload. Logic Pro needs
   AU, which is not built yet.
