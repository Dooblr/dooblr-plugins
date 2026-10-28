# dooblr-vocals

Real-time pitch correction and a vocal processing chain. Third plugin in Daniel
Feinstein's **dooblr** suite (with dooblr-q and dooblr-kick). It is a VST3 audio
effect for macOS, and it uses the same look as its siblings. The code, artwork
and presets are original. No Antares, Celemony, FabFilter, D16 or Sonic Academy
material is used.

Version 0.1.0 · JUCE 8 · C++20 · VST3 effect (stereo in, stereo out) · arm64 macOS 11+

Plugin code `Dbvx` · bundle id `com.dooblr.vocals` · manufacturer `Dblr`

## Features

- **Pitch correction (always first in the path).** The detector sees the incoming
  vocal, not a gated, EQ'd or compressed version. That order is fixed; the rest
  of the chain is user-reorderable.
  - **Keyed** mode: key + scale (major, natural minor, dorian, phrygian, lydian,
    mixolydian, locrian, harmonic minor, melodic minor, and a custom 12-note
    mask edited on the keyboard).
  - **Chromatic** mode: nearest semitone.
  - Retune speed (0 = instant), humanize / hold around the target, correction
    amount, A4 reference (415–466 Hz), formant preservation, and an input range
    (soprano…bass) that sets the detector's min/max Hz.
  - YIN pitch detection with a confidence / RMS voicing gate: unvoiced
    consonants, breaths and gaps pass through (delayed) without being snapped.
  - TD-PSOLA grains with formant on; resampled grains with formant off.
- **Live vs HQ.**
  - **Live** (default): hop 64, YIN window ~2.5 periods of the lowest F0 the
    detector must cover (70 Hz), latency = next power of two of two periods at
    70 Hz. At 48 kHz that is **2048 samples (~42.7 ms)**. Usable while tracking.
  - **HQ**: hop 32, a larger analysis window, a stricter YIN threshold, latency
    **4× Live** (8192 samples / ~171 ms at 48 kHz). Better tracking and cleaner
    shifting; the host is told via `setLatencySamples`.
  - **Offline render:** when the host calls `isNonRealtime()` (bounce / freeze),
    the HQ path is used even if the Quality control is Live, so a mixdown is
    the high-quality version without changing the session's live setting.
  - Changing Live ↔ HQ updates the reported latency. Input type does **not**
    change latency, so switching soprano/bass never makes the DAW's PDC jump.
- **Vocal chain** (copied from dooblr-kick, minus the synth / samples / drive /
  transient shaper):
  - **Gate** (new): threshold, attack, hold, release, range. Stereo-linked peak
    detector. View matches the other dynamics graphs.
  - **EQ**: low shelf, mid peak with Q, high shelf, live FFT behind the curve.
  - **Compressor**: threshold, ratio, attack, release, makeup, transfer curve.
  - **Limiter / clipper**, ceiling, release, output gain.
  - Per-section power toggles on the tabs. User-reorderable `chain_order`
    (24 permutations of Gate / EQ / Comp / Limiter) with a 15 ms linear
    crossfade onto a reset copy of the chain.
- **Pitch display**: detected vs corrected F0 over time against a note grid and
  keyboard. No help captions on the graph; hover/drag shows a readout. In Custom
  scale mode, click a key to toggle that pitch class (one undo step).
- **Presets:** Init, Tight Pop, Natural, Chromatic Track, Radio Voice, HQ Mix.
  User presets are `.dvocal` files in `~/Music/dooblr-vocals/Presets`.
- **Undo/redo** (Cmd-Z / Cmd-Shift-Z) keeps 50 steps. One drag or preset load is
  one step.
- **Resizable UI** from 940×600 to 2400×1400 (default 1080×660).
- **Real-time-safe audio.** The audio thread does not allocate. Meters and the
  pitch trace come through lock-free FIFOs and are drawn on VBlank.

## Latency / HQ trade-off

| Mode | Hop | YIN window | Latency at 48 kHz | When |
|---|---|---|---|---|
| Live | 64 | ~2.5 periods of 70 Hz | 2048 samples, 42.7 ms | Tracking, low-latency monitoring |
| HQ | 32 | 2× Live, stricter threshold | 8192 samples, 170.7 ms | Mixing, or Quality = HQ |
| Host offline | HQ path | HQ | HQ latency, reported | Bounce / freeze (`isNonRealtime`) |

The delay is also the dry path, so Live/HQ mix stays time-aligned. Amount = 0
or Pitch off still delays by the reported latency (the host compensates).

## Parameters

| ID | Name | Default |
|---|---|---|
| `pitch_on` | Pitch On | on |
| `corr_mode` | Mode (Keyed / Chromatic) | Keyed |
| `corr_key` | Key | C |
| `corr_scale` | Scale | Major |
| `corr_mask` | Scale Mask (custom) | C major bits |
| `retune_speed` | Retune Speed | 20 ms |
| `humanize` | Humanize | 15 % |
| `corr_amount` | Amount | 100 % |
| `a4_hz` | A4 | 440 Hz |
| `formant` | Formant | on |
| `input_type` | Range | Tenor |
| `quality` | Quality | Live |
| `gate_on` | Gate On | off |
| `gate_threshold` / `gate_attack` / `gate_hold` / `gate_release` / `gate_range` | Gate | −40 dB, 1 ms, 50 ms, 80 ms, 60 dB |
| `eq_on` plus low/mid/high freq, gain, mid Q | EQ | off, 80 / 1 k / 8 kHz |
| `comp_on` plus threshold, ratio, attack, release, makeup | Comp | off, −18 dB, 3:1 |
| `lim_mode` / `lim_ceiling` / `lim_release` | Limiter | Limiter, −0.3 dB, 60 ms |
| `output_gain` | Output | 0 dB |
| `chain_order` | Chain Order | Gate > EQ > Comp > Limiter |

## Build and install

Requirements on the Mac this plug-in ships for: Xcode (with the macOS SDK) and
CMake 3.22 or later. JUCE 8.0.15 is fetched by CMake. To reuse an existing JUCE
checkout, pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`. `scripts/build.sh`
uses `~/dooblr-build/_deps/juce-src` when it exists (override with `JUCE_SOURCE_DIR`).

The repo lives in `~/Dropbox/dooblr_plugins/dooblr-vocals`. The script builds
outside it, in `~/dooblr-build/dooblr-vocals` (override with `BUILD_DIR`),
because Dropbox's extended attributes on build output break `codesign`.

The script never uses sudo and never installs system packages.

```sh
scripts/build.sh
```

This script does the following:

1. Configures with Unix Makefiles and builds the VST3, the tests and the
   LoadCheck tool, then runs the tests (`ctest`). Editor tests use `xvfb-run`
   when there is no `DISPLAY`.
2. On macOS, installs `dooblr-vocals.vst3` into `/Library/Audio/Plug-Ins/VST3`
   with `ditto --noqtn --noextattr`. It never uses sudo. If that folder is not
   writable, set `INSTALL_VST3_DIR`.
3. Re-signs the bundle after JUCE writes `moduleinfo.json` (`cmake/sign-bundle.sh`
   strips Finder info / quarantine and retries, because a File Provider synced
   folder such as Dropbox can re-add them).
4. Checks the code signature (`codesign --verify --deep --strict`).
5. Loads the installed bundle the way a host would (`Tools/LoadCheck`).

See the top of the script for options. `INSTALL_VST3=0` builds without
installing. On Linux the install step is off by default (no `/Library`); the
built bundle is still LoadChecked.

Linux is a development target only: this repository's tests and a Linux VST3 /
standalone build are meant to run here. The shipping bundle is macOS arm64.

## Using it in Ableton Live

1. Rescan plug-ins: Preferences → Plug-Ins → Rescan, with VST3 system folders
   turned on.
2. Drag **dooblr-vocals** (Plug-Ins → VST3 → dooblr) onto an **audio track**.
   It is an effect, not an instrument.
3. Set Key / Scale (or Chromatic), Range to the singer, and Retune. Turn on
   Gate / EQ / Comp / Limiter as needed. Drag the tab handles to reorder the
   chain. Switch Quality to HQ when mixing if you can spare the latency.

## Tests

`~/dooblr-build/dooblr-vocals/Tests/DooblrVocalsTests` checks:

- Instantiation, name, version `0.1.0`, stereo I/O, plugin code path.
- YIN accuracy on synthetic tones and vibrato.
- Keyed vs chromatic snap (including a custom mask).
- Unvoiced / quiet passthrough.
- Reported latency (Live, HQ, and `isNonRealtime()`).
- Gate threshold / range / hold.
- Chain reorder (24 permutations, crossfade stays finite).
- Parameter and state round-trip, factory presets, user `.dvocal` files.
- Every parameter changes the sound or the reported latency (no dead controls).
- Undo history (one gesture, one step, 50-step cap).
- No `operator new` during `processBlock` after prepare (warmup).
- Editor layout at 940×600, 1080×660 and 2400×1400, version label, tab power.

Set `DOOBLRVOCALS_EDITOR_SNAPSHOT=/path/prefix` to write editor PNG snapshots.

Manual validation still needed on the Mac: pluginval at strictness 10, the
Steinberg VST3 validator, and a DAW pass.

## Notes

Pitch correction is designed in the spirit of tracking-friendly tuners. It is
not Auto-Tune, Melodyne or anything else, and it makes no claim to match them.
