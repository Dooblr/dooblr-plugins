# dooblr-soothe

A dynamic resonance reducer (VST3, macOS arm64). It listens for narrow peaks that stick
out of a sound's spectral envelope — harshness, ringing, sibilance, boomy room modes — and
turns them down only while, and only as much as, they stick out. Broadband material passes
nearly untouched.

Version 0.1.0. Built with JUCE 8.0.15 (C++20, CMake).

## How it works

dooblr-soothe is a short-time Fourier transform processor (Hann analysis and synthesis
windows, overlap-add). Every hop, for every bin:

1. The bin's level (its 3-bin neighbourhood) is compared with a **spectral envelope**: the
   mean power of a log-frequency window around it, with the bin's own neighbourhood left
   out, so a peak never raises its own reference.
2. Whatever sticks out by more than the **Selectivity** threshold is the excess. The cut is
   **Depth** × excess, weighted by the **depth curve**, smoothed across frequency, limited
   to **Max Cut**, and given per-bin **Attack** / **Release**.
3. In **Hard** mode a level-dependent part is added: loud bins are also pulled down, so
   loud passages are treated harder than quiet ones. **Soft** mode is purely relative (the
   same treatment at any level).

The gain is real-valued per bin and the input is delayed by the FFT size on the dry path,
so Mix, Delta and Bypass are sample-aligned with the processed signal.

## Controls

| Area | Controls |
| --- | --- |
| Toolbar | Soft / Hard, Quality (Standard / High / Ultra), Sidechain, Delta, Bypass, latency readout |
| Graph (top) | Input spectrum (fill), output spectrum (line), reduction hanging from the top (30 dB scale) |
| Graph (bottom) | Depth curve (±18 dB), up to 8 numbered nodes |
| Detection | Depth, Sharpness, Selectivity, Attack, Release |
| Stereo | L/R or M/S, Link, Focus (channel 1 / mid ↔ channel 2 / side) |
| Output | Max Cut, Wet Trim, Mix; Output gain and meter in the header |
| Bands 1–8 | Use, Shape (bell, low shelf, high shelf, low cut, high cut, band pass), Freq, Gain, Q, Listen |

Graph mouse: drag a node (frequency and gain, one undo step; Shift = fine), mouse wheel over
a node (Q), double-click empty space (add a band there), double-click a node (remove it),
right-click a node (shape, Listen, Remove). A hover readout shows frequency and level.

- **Depth curve.** Positive gain = more reduction in that region, negative = less. Low
  and high cuts and band pass gate where reduction can happen at all.
- **Delta** plays exactly what is removed (input − output).
- **Band Listen** plays only what the selected band's region removes.
- **Sidechain** detects on the sidechain input (enable the plug-in's sidechain bus in the
  host); without a connected key it detects on the input itself.
- **Quality** sets the FFT size and overlap: at 44.1/48 kHz Standard 1024 samples (4×
  overlap), High 2048 (4×), Ultra 2048 (8×, finer in time); doubled at 88.2/96 kHz and
  quadrupled at 176.4/192 kHz. The latency equals the FFT size. The latency is reported to the host and
  shown in the toolbar.

Presets: Default plus 7 original factory presets; user presets (`.dsoothe`) in
`~/Music/dooblr-soothe/Presets`. Undo / Redo (Cmd-Z, Cmd-Shift-Z) cover every UI gesture.

## Build

```sh
./scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-soothe`, runs the console tests (`Tests/SootheTests.cpp`)
and a host-style load check (`Tools/LoadCheck`), ad-hoc signs the bundle and installs it to
`/Library/Audio/Plug-Ins/VST3/dooblr-soothe.vst3` (no sudo). See the script header for
options. Set `DOOBLRSOOTHE_EDITOR_SNAPSHOT=/path/prefix` when running the test binary to write
editor PNG snapshots.

## Tests

The console tests drive the real processor and editor: latency (reported and measured with an
impulse at every Quality and 44.1–192 kHz), depth-0 / Bypass / Mix-0 null, a resonant peak is
cut while broadband white and pink noise stay within 2 dB per octave, Delta = input − output,
sidechain keying, Band Listen, stereo Link / Focus / M/S and mono, extreme settings without
NaN or denormals, state and preset round trips, every parameter audible, undo, zero
allocations in `processBlock`, CPU, editor layout at 940×600, 1080×660 and 2400×1400, and real
mouse events (through a virtual window peer) on every control and on the graph nodes.

## Notes

- Original DSP, UI and presets. The name is the only nod to the class of tool it belongs to.
- Not yet tested inside a DAW; see ROADMAP.md.
