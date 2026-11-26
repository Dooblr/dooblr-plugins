# dooblr-reverb

Algorithmic reverb, VST3 effect for macOS (Apple Silicon), part of the dooblr suite.
Version 0.1.0. Built with JUCE 8.0.15 (C++20, CMake). **Latency: 0 samples.**

One big **Space** knob picks the room, from a small ambience to an infinite space, and shows its
decay time. Six knobs shape it. A large display draws the **Decay Rate EQ** (how long each
frequency rings) and the **Post EQ** (tone of the reverb) over a live spectrum. The workflow is
modelled on a familiar "space / decay EQ" studio reverb. The DSP, presets and artwork are all
original; no third-party code, presets, names or branding are used.

## Controls

### Knob row

| Control | Range (default) | What it does |
| --- | --- | --- |
| **Space** | 0-100 % (45 %) | The room: Ambience, Small room, Room, Studio, Chamber, Club, Hall, Concert hall, Large hall, Arena, Cathedral, Infinite space. It sets the size, the reflection pattern, the diffusion and the decay time (0.2 s to 10 s, log). Under the knob: the decay time (Space × Decay Rate) and the room name. Changes glide over about 250 ms, so automating Space morphs the room without clicks. The host value reads in seconds, and you can type "1.8 s", "800 ms" or "40 %". |
| **Decay Rate** | 50-200 % (100 %) | Scales the decay time without changing the room. |
| **Brightness** | 0-100 % (50 %) | How fast the highs die away compared with the lows. It also darkens the early reflections. |
| **Character** | 0-100 % (25 %) | From a still, smooth tail to a lively, chorused one: delay-line modulation depth and rate go up, early reflections get more prominent, diffusion goes down. Up to 50 % it stays subtle; above that it is a clear effect. |
| **Distance** | 0-100 % (30 %) | Near: strong, early, bright reflections. Far: weaker, darker, later reflections and a denser, softer tail. |
| **Stereo Width** | 0-150 % (100 %) | 0 % mono, 100 % natural, 150 % extra wide. Below 100 % the two halves of the network couple fully. Above 100 % the side signal is raised. The mono sum keeps its level at any width (tested). |
| **Mix** | 0-100 % (30 %) | Dry / wet. Presets never change Mix (a mix lock), so it is safe on an insert. |

### Display

- **Decay Rate EQ** (teal, left scale 12.5-400 %): up to 6 bands (Bell, Low shelf, High shelf,
  Notch). Each band sets the decay at its frequency as a % of the Space decay time; 200 % rings
  twice as long. The thin teal line under it is the decay the engine actually aims for, with
  Brightness and the room's own colour included. The hover chip reads it in seconds.
- **Post EQ** (amber, right scale ±24 dB, 8 dB per grid line): up to 6 bands (Bell, Low shelf,
  High shelf, Low cut, High cut; cuts at 12 / 24 / 48 dB/oct). Band 1 is a 100 Hz low cut, on by
  default.
- **Spectrum:** the reverb output, and optionally the input behind it (Analyzer in the bottom bar).
- **Mouse:**
  - Drag a handle to move it (frequency and value, one undo step); Shift-drag for fine moves.
  - Mouse wheel over a handle changes its Q.
  - Double-click a handle to remove the band. Double-click empty space to add a band to the layer
    picked with the **DECAY RATE EQ** / **POST EQ** buttons (pressing a handle also picks its
    layer).
  - Right-click a handle for shape, slope and remove. Right-click empty space to add to either
    layer.
- The output meter is to the right of the display.

### Bottom bar

- **Analyzer:** Off / Reverb / Input + Reverb. This is a display setting saved with the session.
  It is not a parameter.
- **Predelay:** 0-500 ms. With **Sync** on, it is a note division (1/64 to 1/2, with dotted and
  triplet values) at the host tempo, capped at 500 ms. The control that is not in use dims but
  stays live.
- **In / Out:** −24 to +12 dB. In feeds the reverb (the dry signal follows it); Out comes after the
  mix.
- **Bypass:** crossfades (10 ms) to the untouched input. It is also the host's bypass parameter.

### Header

`<` preset `>` and ☰ (Save / Load / Init), Undo / Redo (Cmd-Z, Cmd-Shift-Z, 50 steps), and A / B
compare with copy. User presets are `.dreverb` files in `~/Music/dooblr-reverb/Presets`.
Factory presets:

- Default, Tight Ambience, Small Room, Drum Room, Vocal Room, Studio Live
- Chamber, Bright Plate, Dark Plate, Snare Plate
- Vocal Hall, Concert Hall, Large Hall, Dark Tail, Cathedral
- Chorus Space, Infinite Pad, Mono Room

The window is resizable from 940×600 to 2400×1400 (default 1080×660).

## DSP

- **Signal path:** input side narrowing (Width < 100 %) → predelay → early reflections → late
  reverb. The early reflections are 24 taps per channel from a fixed, room-scaled pattern, with
  level, spacing and tone set by Space and Distance. The late reverb runs through 4 allpass
  diffusers per channel into a 16-line feedback delay network. Then side gain (Width), the Post
  EQ and the wet output. The processor mixes dry and wet.
- **Network:** two 8-line halves (left / right), each mixed by an 8×8 Hadamard matrix, joined by a
  rotation whose angle is the stereo coupling. Every step in the loop is orthogonal, so only the
  absorption filters set the decay. The delay lines are read with 3rd-order Lagrange
  interpolation and modulated by slow, decorrelated LFOs (Character). Delay lengths glide when
  Space changes.
- **Frequency-dependent decay:**
  - The target RT60 T(f) = Space time × Decay Rate × 2^(Decay Rate EQ + Brightness + room tone),
    clamped to 0.05-40 s.
  - Each line has a broadband gain plus a 10-band octave graphic EQ (TPT bells). The dB gains are
    proportional to that line's length and are fitted by regularised least squares to T(f) at 19
    half-octave points (31 Hz-16 kHz).
  - A guard check on a 64-point grid lowers the broadband gain if any frequency would ring longer
    than 40 s, so the network cannot run away at any setting.
  - Measured RT60 at 1 kHz is within 5 % of the target (tests: 0.39 / 1.04 / 3.54 s measured vs
    0.41 / 1.09 / 3.56 s target).
- **Smoothing and safety:**
  - Control values update every 32 samples with per-parameter smoothing (Space about 250 ms).
  - Denormals are flushed (ScopedNoDenormals), and tests confirm no subnormal output in long tails.
  - The audio thread does not allocate.
  - CPU is about 4-5 % of one Apple Silicon core at 48 kHz stereo.
- **Layouts:** stereo, mono, and mono in / stereo out.
- **Tail:** the plugin reports a tail of the longest current decay plus 500 ms predelay to the
  host.

## Build and install

```sh
cd ~/Dropbox/dooblr_plugins
dooblr-reverb/scripts/build.sh      # builds in ~/dooblr-build/dooblr-reverb, tests, installs, LoadCheck
```

- The build uses JUCE 8.0.15 from `~/dooblr-build/_deps/juce-src` when present; otherwise it is
  fetched. See the top of `scripts/build.sh` for the environment variables.
- The build is installed to `/Library/Audio/Plug-Ins/VST3/dooblr-reverb.vst3` (suite convention,
  no sudo) and ad-hoc signed.
- Tests: `~/dooblr-build/dooblr-reverb/Tests/DooblrReverbTests` (18 tests, 380 checks).
  `DOOBLRREVERB_EDITOR_SNAPSHOT=/tmp/reverb` writes editor PNGs.

### Validation (0.1.0)

- Console tests: 18 / 18 pass, covering:
  - null at Mix 0 and Bypass, zero latency
  - RT60 against the readout; Decay Rate, decay band and Brightness effects
  - predelay and sync; width, mono compatibility and mono layouts
  - stability at every extreme from 44.1 to 192 kHz with block sizes 1-2048
  - state round trip, presets, no dead parameters, undo, A / B
  - no allocation in processBlock
  - editor layout at 3 sizes, and real-mouse display drag / wheel / add / remove
- LoadCheck: dlopen, scan, instantiate, 80 parameters, latency 0, finite output.
- pluginval 1.0.4 at **strictness 10**: SUCCESS (two random seeds).

### Ableton Live 11 Suite

1. Rescan plug-ins (Preferences → Plug-Ins → Rescan), or restart Live.
2. Browser → Plug-Ins → VST3 → dooblr → **dooblr-reverb**, onto an audio or return track.
3. On a return track, set Mix to 100 %. On an insert, start around 20-30 %.
4. Try the presets and the Space knob. Then drag a Decay Rate EQ handle and listen to the tail
   change length at that frequency.

## Limits and differences from the reference workflow

- No MIDI learn, multi-select or value-entry pop-up on display handles. No full-screen mode.
- Decay Rate EQ and Post EQ have 6 bands each.
- No auto-gain or ducking. Mix lock is always on for presets (no separate switch).
- The analyzer is a display setting (Off / Reverb / Input + Reverb); there is no speed or range
  control.
- Not yet tested in Live by ear. The DSP has been checked by measurement only.
