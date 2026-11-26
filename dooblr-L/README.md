# dooblr-L

A mastering and bus limiter. Two-stage gain reduction: a pre-process stage that
lets transients overshoot, then an Enhance stage that reshapes those residual
peaks (harmonics and density) instead of hard-clipping them. Part of Dan
Feinstein's **dooblr** suite.

VST3 effect, macOS 11+ (arm64 / x86_64) and Windows x64, C++20, CMake, JUCE 8.0.15.
Version **0.1.3**.

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

**Oversampling** Off / 2x / 4x / 8x / 16x / 32x (default 4x).

**Latency** Zero / Natural / High (default Zero). Zero reports 0 samples: no
lookahead and no oversampling delay (min-phase IIR, or Off if the IIR would
add delay). True-peak catching still honours the ceiling as well as a
zero-look path can — inter-sample overs that need future taps may slip by
one sample. Natural uses the Look knob plus linear-phase FIR oversampling.
High uses at least 8 ms of lookahead plus linear-phase oversampling. The host
is told via `setLatencySamples` whenever the mode changes.

**Learn** listens to 10–30 s of incoming audio on a background CPU thread
(lock-free FIFO from the audio thread; SIMD via `juce::FloatVectorOperations`,
Accelerate on Mac). There is no GPU path. It scores style / threshold / attack
/ release / knee / Enhance / lookahead / link for loudness vs transient
retention and applies one undoable change. Revert restores the previous
settings. The chosen loudness is shown in the status line.

**Stereo link** — independent percent controls for transients vs release.

**Meters** — input/output sample peak and true peak, scrolling I/O history
with processed output and gain-reduction overlaid on a dim input fill
(the horizontal line is Pre-Process threshold: drag it, Shift/Cmd for fine,
double-click resets; no captions or hint text on the graph),
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
`DooblrLLoadCheck <bundle> [version]` (default Zero latency is 0). Pass
`--expect-latency N` for Natural/High. Without those flags the tool requires
zero latency, same as the other suite LoadChecks.

Editor snapshots: `DOOBLRL_EDITOR_SNAPSHOT=/tmp/l ~/dooblr-build/dooblr-L/Tests/DooblrLTests`

## Licences

JUCE is used under its licence. This plug-in is original work.
