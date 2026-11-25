# dooblr-L

A mastering and bus limiter. Two-stage gain reduction: a pre-process stage that
lets transients overshoot, then an Enhance stage that reshapes those residual
peaks (harmonics and density) instead of hard-clipping them. Part of Dan
Feinstein's **dooblr** suite.

VST3 effect, macOS 11+ (arm64 / x86_64) and Windows x64, C++20, CMake, JUCE 8.0.15.
Version **0.1.2**.

Independently designed. No third-party names, logos, branding or assets appear
in the plug-in, the UI or this document.

## What it does

**Pre-process** — threshold, attack, release and a soft knee. The envelope is
deliberately not a brickwall, so some transient overshoot is left for the second
stage.

**Enhance** — amount 0–100 %. At 0 the stage is skipped (bit-identical to
pre-process plus ceiling). Above 0 it reshapes overshoots: controlled harmonics
during transients, denser/louder hits, kick and snare impact kept. The output
ceiling is still honoured.

**Auto Comp** — true-peak reconstruction metering is always on. Auto Comp (default
on) ducks inter-sample overs only for the duration of the problem, not by
lowering the whole program.

**Styles** (8, each a different detector / envelope / saturation law, not a
label): Transparent, Punchy, Dynamic (transient reshape before limiting),
Allround, Aggressive (near-clip), Modern (clean, loud), Bus (deliberate pumping),
Safe (avoid distortion).

**Oversampling** Off / 2x / 4x / 8x / 16x / 32x (default 4x). Latency is reported
to the host (lookahead + oversampler FIR). Lookahead 0–20 ms.

**Stereo link** — independent percent controls for transients vs release.

**Meters** — input/output sample peak and true peak, gain-reduction history
(the horizontal line is Pre-Process threshold: drag it, Shift/Cmd for fine,
double-click resets; no captions on the graph),
LUFS momentary / short-term / integrated, loudness range (LRA), Reset.
Optional LUFS target (−23 / −16 / −14 / −12) slowly rides short-term loudness
toward that value, still under the ceiling.

**Output** — input drive, ceiling (dBTP), unity-gain audition, listen-to-difference
(delta), dither 16/24-bit with Off / Low / Medium / High noise shaping (dither
off by default).

Bypass is delay-compensated (the dry path is delayed by the reported latency).

## Presets

User presets: `~/Library/Audio/Presets/dooblr/dooblr-L/` on Mac,
`%USERPROFILE%\Documents\VST3 Presets\dooblr\dooblr-L\` on Windows, extension `.dl`.
Files previously saved under `~/Music/dooblr-L/Presets` are copied on first run,
not deleted.

## Building

```sh
dooblr-L/scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-L`, runs `Tests/LimiterTests.cpp`, ad-hoc
signs the bundle on macOS and installs it to `/Library/Audio/Plug-Ins/VST3/dooblr-L.vst3`
when that folder is writable (never with sudo). LoadCheck is
`DooblrLLoadCheck --expect-latency 168 <bundle> [version]` (48 kHz, default 4x
OS + 2 ms lookahead + Auto Comp). Without `--expect-latency` / `--allow-latency`
the tool still requires zero latency, same as the other suite LoadChecks.

Editor snapshots: `DOOBLRL_EDITOR_SNAPSHOT=/tmp/l ~/dooblr-build/dooblr-L/Tests/DooblrLTests`

## Licences

JUCE is used under its licence. This plug-in is original work.
