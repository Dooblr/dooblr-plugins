# dooblr-distressor

An independently designed compressor modelled on the Empirical Labs EL8 Distressor.
It is **not** an emulation of the hardware or of any licensed plug-in: the curves, detectors
and saturators are original approximations of the behaviour described in the EL8X and
Universal Audio EL8 manuals.

Part of Dan Feinstein's **dooblr** suite. VST3 effect, macOS 11+ (arm64) and Windows x64,
C++20, CMake, JUCE 8.0.15.

Version **0.1.2**.

## What it does

Eight ratio modes, each with its own threshold, knee and release shape:

| Ratio | Character (approximation) |
| --- | --- |
| 1:1 | Saturation and warmth, no compression |
| 2:1 | Mild, long parabolic knee (about 15 dB, up to ~30 dB with slower times) |
| 3:1 | Mild parabolic knee |
| 4:1 / 6:1 | General-purpose; 6:1 steepens toward limiting after the knee |
| 10:1 | Opto-style, program-dependent release up to about 20 s |
| 20:1 | Near-brickwall |
| Nuke | Brickwall, slightly lower output, logarithmic release (lets off fast, then slows) |

Input and Output are nonlinear stages. Input also drives into the threshold. Attack is
50 µs–30 ms (20 µs at minimum on 2:1, 3:1 and 4:1). Release is 50 ms–3.5 s. Knobs are
0–10.5, matching the hardware's numbering.

Detector: 100 Hz 6 dB/oct high-pass, a 6 kHz band emphasis, and stereo link, combinable.
On a mono instance, Link models the hardware "dead patch" (the unused second unit is
silence, so the detector sees half the level and distortion runs hotter).

Audio: an 18 dB/oct high-pass (about −3 dB at 60 Hz and −12 dB at 30 Hz) and mutually
exclusive Dist 2 (even harmonics) / Dist 3 (mostly 3rd). Clean / Dist 2 / Dist 3 THD is
aimed at the EL8X ranges (about 0.025–0.3 %, 0.05–3 %, 0.1–20 %).

Headroom is 4–28 dB in 4 dB steps, default 16. Higher dB values push the circuit harder.
Mix is continuous and phase-aligned (zero latency; Mix 0 is a bit copy of the input).
Bypass copies the input and **keeps the gain-reduction meter running**.

There is no British Mode (neither manual documents it) and no Power switch (Bypass is
the real in/out).

## Distortion lamps

The yellow lamp is **1 % THD**, as Universal Audio specifies. The EL8X PDF contradicts
itself: the features list says a "1% LED", while the distortion-modes page says the
yellow LED is 0.25 % THD. This plug-in follows UA. The red lamp is 3 % THD / clip.

## Controls

Every control below is an Ableton-automatable VST parameter.

- **Input / Attack / Release / Output** — 0–10.5
- **Gain Match** — off by default. When on, a slow (~1 s) RMS detector compares the dry
  input (before the Input stage) to the processed signal (after compression, before the
  Output knob) and applies a smoothed compensation of up to ±24 dB so the wet loudness
  matches the dry. Output remains a trim on top. Mix is matching the final wet/dry blend.
  Frozen below about −60 dBFS so silence and gated signal do not pump. Does nothing
  while Bypass is on. Live compensation in dB is shown under the Match button.
- **Ratio** — 1:1 … Nuke
- **Audio HP**, **Distortion** (Off / Dist 2 / Dist 3)
- **Detector HP**, **Band Emphasis**, **Link**
- **Headroom**, **Mix**, **Bypass**

The layout follows the hardware / UAD panel: ratio row and GR lamps on top, detector
and audio modes, a compression display (transfer curve, GR meter with peak-hold, scrolling
history), the four main knobs plus Gain Match, then headroom, mix and I/O meters. Drawn
entirely in the dooblr look (no third-party logos, assets or trademarks; no fake or dead
controls; no hint text on graphs). Window 1080×660, resizable 940×600–2400×1400.

The compression display is interactive. A knee point on the transfer curve drags
horizontally for Input and vertically for Output; a slope handle snaps through the
stepped Ratio values; Attack and Release handles sit on the history envelope.
Dragging a point moves the knob, turning the knob (or host automation) moves the
point. Double-click a point to reset it. Gestures go through the APVTS so undo and
Ableton automation work. The GR meter and history keep running while Bypass is on.

Factory presets are original starting points named after uses (Vocals, Bass, Room Nuke,
…), not copies of anyone's commercial presets.

User presets: `~/Music/dooblr-distressor/Presets`, extension `.ddistressor`.
Undo / Redo (Cmd-Z, Cmd-Shift-Z) cover every UI gesture.

## Build

```sh
dooblr-distressor/scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-distressor`, runs `Tests/DistressorTests.cpp`, ad-hoc
signs the bundle and installs it to `/Library/Audio/Plug-Ins/VST3/dooblr-distressor.vst3`
(no sudo). See the script header for `BUILD_DIR`, `RUN_TESTS`, `INSTALL_VST3`,
`LOAD_CHECK`, `JUCE_SOURCE_DIR`.

## Trademark

Empirical Labs, Distressor, EL8 and EL8X are trademarks of Empirical Labs, Inc.
Universal Audio and UAD are trademarks of Universal Audio, Inc. They are used here
only to name the published behaviour this plug-in approximates. dooblr-distressor
is an original work and is not affiliated with, endorsed by, or a product of
Empirical Labs or Universal Audio.
