# dooblr-meter

Loudness, translation and mix-insight meter. JUCE VST3 effect (category "Fx Analyzer"), plugin
code `Dbmt`, zero latency. Audio passes through bit-identical unless a Listen audition is on.

Version 0.1.0. Design notes: `design/plugins/dooblr-meter.md` and
`design/dooblr-design-system.md` §17 dooblr-meter (MT).

## What it measures

- **Loudness (ITU-R BS.1770-4 / EBU R128):** integrated (gated −70 LUFS / −10 LU), short-term
  (3 s), momentary (400 ms), loudness range (EBU Tech 3342: −70 / −20, P95 − P10), PLR, PSR,
  RMS (3 s). K-weighting uses the published 48 kHz coefficients and a bilinear design at other rates.
- **Peaks:** sample peak, and true peak by 4x polyphase interpolation (2x at 88.2 / 96 kHz, 1x at
  176.4 kHz and above), with overs against the target's ceiling.
- **Spectrum:** 8192-point Hann FFT (hop 2048) on mid and side, long-term 1/3-octave average
  against a reference curve (Balanced, Bright, Warm, Bass-heavy, Acoustic, Spoken; ±3 dB band).
- **Stereo:** vectorscope, correlation overall and in three bands (<120 Hz, 120 Hz–2 kHz,
  >2 kHz), side-vs-mid width, mono-sum loss, L/R balance, DC offset.
- **Delivery targets:** Streaming −14 (turns up only to the peak ceiling), Streaming −16,
  Video −14 and Streaming −15 (turn down only), Spoken −16, Broadcast EU −23, Broadcast US −24,
  Club −8 (no normalisation) and Custom. Each shows the playback gain change the mix would get.
- **Translation (8 playback profiles):** Club, BT speaker, Phone, Laptop, Earbuds, Headphones,
  Car, TV. Each gets a 0–100 score and a one-line reason (energy lost below the device's
  low-frequency limit, mono summing, limiter stress, intelligibility, clip risk and width).
  Clicking a tile auditions the mix through that profile (Listen), level matched by default.
- **Findings:** rule-based checks with Fix / Watch / Info severity: true-peak overs, clipping,
  loudness against the target, over-limiting, loudness range, rumble, boom, thin low end, mud,
  harshness, dull top, narrow resonances, sibilance, phase per band, mono compatibility, wide
  low end, L/R balance, width, DC offset and weak translation. Selecting a finding highlights
  its time span on the history and its band on the spectrum.

## Modes

- **Song** (default): measurements are indexed by the host's song position, so replaying a
  section overwrites that section instead of adding to it. Needs the host transport running.
- **Live:** measures everything since the last **Reset**.

Measurements are saved with the session (Song: the whole table; Live: the last 20 minutes).
Listen and Mono always load Off.

## Pages

- **Main:** Target, Curve, Listen, Live / Song and Reset in the header; loudness readouts,
  stereo, loudness history, spectrum, translation tiles and findings.
- **Advanced:** custom target loudness and ceiling (active when Target is Custom), history span,
  spectrum slope and averaging, findings sensitivity, Level match, Mono, Freeze; the delivery
  table, every metric, and the short-term loudness distribution.

## Build, test and install

```sh
BUILD_DIR=~/dooblr-build/meter-build dooblr-meter/scripts/build.sh
```

The script configures CMake (JUCE 8.0.15 via FetchContent; pass
`-DFETCHCONTENT_SOURCE_DIR_JUCE=...` to share a checkout), builds the VST3, `DooblrMeterTests`
and `DooblrMeterLoadCheck`, runs the tests, installs to `/Library/Audio/Plug-Ins/VST3` and runs
LoadCheck on the installed bundle. Set `DOOBLRMETER_EDITOR_SNAPSHOT=/path/prefix` when running the
tests to write editor PNGs at 940x600, 1080x660 and 2400x1400.

## Not in 0.1

Reference-track comparison and report export are deferred (no controls for them are shown).
