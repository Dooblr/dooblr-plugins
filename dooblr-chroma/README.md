# dooblr-chroma 0.1.3

A physical-modelling instrument (VST3, MIDI in). Two modal resonators are struck by a mallet and/or a filtered noise source, mixed in parallel or joined through a stable bidirectional coupling. Two layers can play together or be split across the keyboard. A compact master chain (EQ, compressor, distortion, chorus, delay, reverb) and four macros sit at the end.

Independent architecture and dooblr aesthetics. No third-party names, logos, assets or branding.

## Architecture

- **Exciters**: Mallet (Stiffness, Noise, Color) and a Noise source (LP / HP / BP / LP+HP, ADSR or AHD, LFO). Mixer: Mallet, Noise, Direct.
- **Resonators A and B**, types: string, bar, marimba, plate, drumhead, membrane, open tube, closed tube, manual (up to 4 partials). Mode density 4 / 16 / 30 / 70 (tubes use every mode up to Nyquist, capped at 70).
- Per resonator: Pitch, Key tracking, Level / Rate / Vel pitch envelope, Decay, Release, Material, Tone, Low Cut, Radius (tubes), Hit Position (with Vel / Key / Rnd).
- **Coupling**: parallel mix (Balance) or bidirectional coupled (Balance = impedance ratio), energy-bounded so it cannot blow up.
- **Two layers**: layered or split, with L2 transpose and independent levels. Exciters, macros and the master FX chain are shared in 0.1.x.
- Unison (1–4), vibrato, glide, four macros (mallet level, decay, tone, noise amount).
- **Effects**: EQ, compressor (zero look-ahead), distortion, chorus, delay, reverb, output gain.
- Plugin latency is zero.

Factory programs (compiled in): Init, Marimba, Kalimba, Steel Drum, Bells, Plucked String, Noise Pad, Open Pipe, Coupled Plates, Bass Drum. User presets go through `common/PresetPaths` (macOS `~/Library/Audio/Presets/dooblr/dooblr-chroma/`, extension `.dchroma`).

The editor follows the suite design tokens. A resonator view shows the object and the hit (or coupling) position; it has no hint text. Window sizes: 940×600, 1080×660 (default), 2400×1400.

## Build

```sh
dooblr-chroma/scripts/build.sh   # builds in ~/dooblr-build/chroma-build
```

Uses JUCE 8.0.15 via `-DFETCHCONTENT_SOURCE_DIR_JUCE=~/dooblr-build/_deps/juce-src` when that checkout exists. Installs to `/Library/Audio/Plug-Ins/VST3/dooblr-chroma.vst3` after the tests pass (never uses sudo).

To write editor snapshots while running the tests:

```sh
DOOBLRCHROMA_EDITOR_SNAPSHOT=/tmp/dooblr-chroma ~/dooblr-build/chroma-build/Tests/DooblrChromaTests
```
