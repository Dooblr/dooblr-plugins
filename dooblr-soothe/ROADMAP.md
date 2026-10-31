# dooblr-soothe roadmap

## 0.1.1 (now)

- New layout: left column with the big Amount knob (the depth parameter) and Sharpness,
  a dot-slider strip (stereo mode, Link, Focus), a toolbar over the graph, and a bottom row
  that switches between the band controls and an Advanced panel.
- The weighting curve is drawn over the spectrum and the reduction in one plot (one
  frequency axis, one dB scale); pill handles for shelves / cuts, dots for bells.
- Band Gain inverted to read like an EQ (down = more reduction); 0.1.0 sessions and preset
  files are migrated; factory presets updated.
- Preset prev / next, A / B compare with copy.

## 0.1.0

- STFT resonance reducer: Depth, Sharpness, Selectivity, Attack, Release, Soft / Hard.
- Depth curve with 8 editable bands (6 shapes), live reduction graph, Band Listen.
- Delta, Bypass (host bypass parameter), Mix, Wet Trim, Max Cut, Output.
- L/R and M/S, Link, Focus; stereo sidechain input; mono and stereo layouts.
- Quality: three FFT sizes with exact, host-reported latency.
- Factory and user presets, undo / redo, resizable editor (940×600 .. 2400×1400).

## Next

- Verify in Ableton Live and another VST3 host: sidechain routing, latency compensation,
  automation of Quality (latency changes mid-session), CPU at small buffer sizes.
- AU build.
- Optional low-latency mode (shorter window with a minimum-phase-like smoothing).
- Per-band Depth / Selectivity offsets (today the bands shape how much, not how picky).
- Transient-aware detection so drum attacks are not dulled at fast Attack settings.
- Graph: freeze / peak-hold of the reduction trace; zoom of the curve range.
- Presets: more categories (dialogue, guitars, masters) after DAW listening tests.
- Oversampled detection for very high resonances near Nyquist at 44.1 kHz.
