# dooblr-soothe

A dynamic resonance reducer (VST3, macOS arm64). It listens for narrow peaks that stick
out of a sound's spectral envelope — harshness, ringing, sibilance, boomy room modes — and
turns them down only while, and only as much as, they stick out. Broadband material passes
nearly untouched.

Version 0.1.2. Built with JUCE 8.0.15 (C++20, CMake).

## How it works

dooblr-soothe is a short-time Fourier transform processor (Hann analysis and synthesis
windows, overlap-add). Every hop, for every bin:

1. The bin's level (its 3-bin neighbourhood) is compared with a **spectral envelope**: the
   mean power of a log-frequency window around it, with the bin's own neighbourhood left
   out, so a peak never raises its own reference.
2. Whatever sticks out by more than the **Selectivity** threshold is the excess. The cut is
   **Amount** (the `depth` parameter) × excess, weighted by the **weighting curve**, smoothed across frequency, limited
   to **Max Cut**, and given per-bin **Attack** / **Release**.
3. In **Hard** mode a level-dependent part is added: loud bins are also pulled down, so
   loud passages are treated harder than quiet ones. **Soft** mode is purely relative (the
   same treatment at any level).

The gain is real-valued per bin and the input is delayed by the FFT size on the dry path,
so Mix, Delta and Bypass are sample-aligned with the processed signal.

## Layout and controls

| Area | Controls |
| --- | --- |
| Left column | Name and version, Bypass, Soft / Hard, the big **Amount** knob (value box below; double-click resets, Shift-drag is fine), Sharpness, latency and status |
| Strip | Vertical dot-sliders: stereo mode (L/R or M/S), Link, Focus (channel 1 / mid ↔ channel 2 / side) |
| Toolbar | Sidechain (SC), Delta, preset < list > and a menu (Save / Load), Undo / Redo, A / B compare with copy (A→B or B→A), Advanced |
| Graph | One plot, frequency labelled along the top, one dB scale at the right: faint input and output spectrum, the live reduction as a filled area hanging from the 0 dB baseline (it dips where the plug-in cuts), and the weighting curve on top (bright line, light fill) with handles: pills for shelves and cuts, dots for bells and band passes. Output meter at the right |
| Bottom row, band view | Band tabs 1–8; Use, Shape (bell, low shelf, high shelf, low cut, high cut, band pass), Freq, Gain, Q; Listen |
| Bottom row, Advanced | Selectivity, Attack, Release, Max Cut, Wet Trim, Mix, Output, Quality (Standard / High / Ultra) |

Spectrum drawing (0.1.2): the analyser runs at FFT resolution (about 23 Hz per bin at
48 kHz, Standard / High), which is coarser than the 256 log-spaced display points below a
few hundred hertz. The display now interpolates between bin centres (monotone cubic, in dB,
so it never overshoots), blends to the peak of the covered bins where a point spans several
bins (the highs keep their detail and narrow dips), applies a light fractional-octave
smoothing that is widest at the low end (up to 1/6 octave, none where bins are finer than
the display), and draws smooth curve segments. The input and output spectrum and the
reduction fill use the same pipeline. This is display only: the gains applied to the audio
are unchanged. The paths are rebuilt once per new frame and paint allocates nothing.

Graph mouse: drag a node (frequency and gain, one undo step; Shift = fine), mouse wheel over
a node (Q), double-click empty space (add a band there), double-click a node (remove it),
right-click a node (shape, Listen, Remove). A hover readout shows frequency, curve and level.

- **Weighting curve** (0.1.1: reads like an EQ). Pull a region down and it is reduced more;
  push it up and it is reduced less. Low and high cuts and band pass gate where reduction
  can happen at all. Sessions and preset files from 0.1.0, where the sign was the other way
  round, are migrated on load (band Gain negated; the state carries a version number).
- **A / B** keeps two complete settings snapshots while the plug-in is open (not saved with
  the session); B starts as a copy of A. Switching clears undo.
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
NaN or denormals, state and preset round trips, the 0.1.0 → 0.1.1 Gain migration (sessions and preset files),
the inverted weighting (a band pulled down at 2 kHz cuts a 2 kHz resonance more than flat,
pushed up less), A / B compare, every parameter audible, undo, zero
allocations in `processBlock`, CPU, editor layout at 940×600, 1080×660 and 2400×1400, and real
mouse events (through a virtual window peer) on every control in both bottom-row views
(band and Advanced), the toolbar (preset < >, A / B, copy, Undo / Redo) and the graph nodes, and a display smoothness check (the drawn output spectrum and the
reduction edge have no flat-step runs between 40 and 500 Hz at all three editor sizes,
a 220 Hz peak still stands out after smoothing, and the reduction above 1.5 kHz is drawn
unchanged).

## Notes

- Original DSP, UI and presets. The name is the only nod to the class of tool it belongs to.
- Not yet tested inside a DAW; see ROADMAP.md.
- The layout follows the structure of a reference screenshot (left column with the
  main knob, toolbar over a large graph); the colours, fonts, knobs and artwork are the
  dooblr design system's own.
