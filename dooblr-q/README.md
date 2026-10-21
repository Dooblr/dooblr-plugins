# OctaEQ (dooblr-q)

A parametric EQ plug-in built with [JUCE](https://juce.com) and CMake.

**Current milestone (1):** a buildable, loadable VST3 audio effect that passes
audio through unchanged, with a minimal native JUCE editor. There is no EQ
processing yet.

## Layout

```
CMakeLists.txt          Project, JUCE dependency and plug-in target
Source/
  PluginProcessor.h/.cpp  Audio processing (pass-through), buses, state
  PluginEditor.h/.cpp     GUI only
Tests/
  CMakeLists.txt          Test executable + CTest registration
  PluginSmokeTests.cpp    Console smoke tests against the real processor/editor
scripts/build.sh        Configure, build, test, report artefact path
```

## Dependencies

| Tool | Notes |
|------|-------|
| CMake ≥ 3.22 | Tested with 3.31.2 |
| C++20 compiler | macOS: Xcode / Apple clang (tested with Apple clang 21, macOS 27 SDK, arm64). Windows: Visual Studio 2022. Linux: GCC ≥ 10 or Clang ≥ 12 plus JUCE's Linux packages. |
| git + network at **configure** time | CMake `FetchContent` downloads JUCE once into `build/_deps`. |
| Ninja | Optional. Used automatically by `scripts/build.sh` if installed; otherwise Unix Makefiles. |

JUCE is pinned to the official release tag **8.0.15**
(`OCTAEQ_JUCE_TAG` in `CMakeLists.txt`). To use a local JUCE checkout
instead of downloading, configure with
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

The plug-in has no runtime network dependencies (`JUCE_WEB_BROWSER=0`,
`JUCE_USE_CURL=0`) and links only `juce_audio_processors` and
`juce_gui_basics` (plus their JUCE dependencies).

## Building

```bash
./scripts/build.sh
```

This configures `build/` in Release, builds the `OctaEQ_VST3` target, builds
and runs the smoke tests through CTest, and prints the path of the built
`OctaEQ.vst3`. Options (environment variables):

```bash
BUILD_DIR=build-debug CONFIG=Debug ./scripts/build.sh   # Debug build elsewhere
RUN_TESTS=0 ./scripts/build.sh                          # plug-in only
JOBS=4 ./scripts/build.sh
```

Equivalent manual commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target OctaEQ_VST3 --parallel
cmake --build build --config Release --target OctaEQTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

On macOS the bundle ends up at
`build/OctaEQ_artefacts/Release/VST3/OctaEQ.vst3` (the script reads the actual
path from `build/artefact-paths/OctaEQ_VST3-<Config>.txt`, which CMake
generates from JUCE's target properties).

The build **does not** install the plug-in anywhere. To try it in a host, copy
it yourself, e.g. on macOS:

```bash
cp -R build/OctaEQ_artefacts/Release/VST3/OctaEQ.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

### Adding formats later

Formats are selected with a cache variable, so AU (macOS) or other formats need
no restructuring:

```bash
cmake -S . -B build -DOCTAEQ_PLUGIN_FORMATS="VST3;AU"
cmake --build build --target OctaEQ_AU
```

Windows VST3 builds use the same `CMakeLists.txt` with the Visual Studio
generator (`cmake -S . -B build -G "Visual Studio 17 2022"`).

## Plug-in metadata

| | |
|---|---|
| Product name | OctaEQ |
| Type | Audio effect, VST3 categories `Fx|EQ` |
| Manufacturer / plug-in code | `Dblr` / `Octq` |
| Bundle ID | `com.dooblr.octaeq` |
| Buses | Mono→mono or stereo→stereo (default stereo) |
| MIDI | None in or out, not a synth, not a MIDI effect |
| Latency / tail | 0 / 0 |

## Tests

`Tests/PluginSmokeTests.cpp` is a console program registered with CTest. It
links the plug-in's shared-code target, so it tests the same compiled processor
and editor that go into the VST3. It checks:

- processor instantiation, `createPluginFilter()`, name and MIDI flags;
- supported layouts (mono and stereo accepted; mismatched, disabled-input and
  surround layouts rejected);
- bit-exact pass-through (`memcmp`) for mono and stereo at 44.1/48/96 kHz with
  varying block sizes (1–1024 samples), including denormal, ±0 and
  out-of-range sample values;
- zero-length blocks;
- bypass (`processBlockBypassed`) is bit-exact;
- output-only channels: the only accepted layouts have equal input and output
  counts, so the clearing branch in `processBlock` cannot be reached by a
  valid host configuration; the test verifies that invariant and that extra
  buffer channels are left untouched;
- state save/restore round-trip, plus malformed/foreign state being ignored;
- editor creation, default size 900×520, resizability, status label text,
  off-screen paint, and clean destruction (twice).

Run them with `ctest --test-dir build -C Release --output-on-failure` or
directly via `build/Tests/OctaEQTests`.

## Validation that still needs doing manually

These are **not** automated in this repository:

1. **pluginval** (Tracktion's plug-in validator, not bundled here). Download
   it from <https://github.com/Tracktion/pluginval/releases>, then:

   ```bash
   pluginval --strictness-level 10 --validate-in-process \
       build/OctaEQ_artefacts/Release/VST3/OctaEQ.vst3
   ```

2. **Steinberg VST3 validator** (`validator` from the VST3 SDK) can also be
   run against the bundle.

3. **DAW loading.** Copy the bundle into the VST3 folder (see above) and load
   it in a VST3 host such as REAPER, Ableton Live, Bitwig, Cubase or Studio One.
   Check: it scans, appears as an EQ effect, inserts on mono and stereo tracks,
   passes audio unchanged (null test against the dry signal), bypass works,
   the editor opens/resizes/closes, and a saved project reloads. Logic Pro
   needs the AU format, which is not built yet.
