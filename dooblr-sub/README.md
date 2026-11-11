# dooblr-sub

Sub-harmonic generator, duration-preserving tone shift, and a big-system
translation check. Part of Dan Feinstein's **dooblr** suite. VST3 effect, macOS
11+ (arm64 / x86_64) and Windows x64, C++20, CMake, JUCE 8.0.15.

Version **0.1.0**.

## What it does

Three sections on one page:

1. **Sub generator** — tracks the low fundamental (band-limited autocorrelation
   with smoothing) and synthesises a phase-continuous sine / lightly shaped
   oscillator an octave down, at the fundamental, or both. A Lo band reinforces
   the bass region. Drive adds harmonics that still speak on small speakers.
   The envelope follows the input (fast attack); note changes crossfade so they
   do not click.
2. **Tone shift** — retunes percussive signals by ±1200 cents without changing
   duration. Transients are split off and left aligned. The body is a duration-locked
   modal resonator (zero-crossing period scaled by `2^(cents/1200)`) plus a cepstral
   STFT residual warped by the same ratio. This is not time-domain resampling.
   Latency is the STFT size (2048 samples) and is reported to the host.
3. **System check** — a live translation analyser for club / festival / home
   sub systems: sub-band energy, crest, low-frequency mono compatibility,
   infrasonic waste, DC, cancellation of the generated sub against the original
   fundamental, and pitch stability. The verdict is green / amber / red with
   the specific reasons. Sub safety: mono-below (default on, 100 Hz), infrasonic
   high-pass (default 25 Hz), and a soft limiter on the sub path. Target
   (Club, Festival / PA, Home / Studio) changes both the verdict thresholds and
   the limiter / infra bias.

No third-party names, logos or branding appear in the UI or the code.

## Controls

Every control is an automatable VST parameter (19 total).

**Sub:** Sub, Lo, Freq (30–120 Hz), Octave (−1 oct / Unison / Both), Track,
Drive, Mix.

**Tone Shift:** Shift (−1200…+1200 cents), Transient, Body, Shift Mix.

**System Check:** Target, Mono, Mono Hz, Infra, Infra Hz, Limit.

Header: Bypass, Output (−24…+12 dB), factory / user presets.

Factory presets: Init, 808 Weight, Kick Sub, Bass Guitar Sub, Tom Retune Down,
Snare Tone Up, Club Safe, Festival Safe.

User presets: `~/Music/dooblr-sub/Presets`, extension `.dsub`.
Undo / Redo (Cmd-Z, Cmd-Shift-Z) cover every UI gesture.

Window 1080×660, resizable 940×600–2400×1400.

## Build

```sh
dooblr-sub/scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-sub`, runs `Tests/SubTests.cpp`, ad-hoc signs
the bundle on macOS and installs it to `/Library/Audio/Plug-Ins/VST3/dooblr-sub.vst3`
when that folder is writable (never with sudo). See the top of `scripts/build.sh`
for `BUILD_DIR`, `RUN_TESTS`, `INSTALL_VST3`, `LOAD_CHECK`, `JUCE_SOURCE_DIR`.

JUCE is pinned at 8.0.15 via FetchContent. `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0`.

## Licences

JUCE is used under its licence. All original design. No third-party plug-in
artwork, branding, samples or presets are included.
