# dooblr-m

An all-purpose Model D-style monophonic synthesizer (VST3 instrument). One board, laid
out like the classic three-oscillator panel: wheels, controllers, oscillator bank, mixer,
modifiers, output. Independent architecture; original design, code and sounds. No
third-party names, logos, wood or artwork.

DSP follows the attached references: Stilson/Smith BLIT oscillators, Huovilainen's 2004
non-linear ladder, Folkman's Model D service-manual contours and mixer, and the usual
analog-synth voice (keyboard CV, glide, low-note priority) described in the MIT 6.101 report.

Version 0.1.3.

## Board

One page. No page column. Header, a voice strip, then the panel left to right.

| Section | Controls |
| --- | --- |
| **Wheels** | Pitch (springs back to centre) and Mod. MIDI pitch-bend and CC1 also drive them. |
| **Voice** | Note Priority (Low / Last rocker; Low is the original), Legato (single-trigger envelopes), Velocity (off by default — the original has none), Bend Range. |
| **Controllers** | Tune (±2.5 st), Glide, Modulation Mix (Osc 3 ↔ Noise), Osc 3 Control, Oscillator Mod, Filter Mod. |
| **Oscillator bank** | Osc 1–3: Range 32' / 16' / 8' / 4' / 2' / LO and six waveforms (triangle, shark / tri-saw, saw, square, wide pulse, narrow pulse). Osc 2 / 3 Frequency ±8 st. Oscillators are free-running. |
| **Mixer** | Osc 1 / 2 / 3, Ext / Feedback, Noise, each with a level and an on switch. Noise colour is a White / Pink rocker. Ext is the classic output-into-external overdrive trick. |
| **Modifiers** | Large filter cutoff (~1.6–2× the other knobs), resonance, amount of contour; filter contour attack / decay / sustain; keyboard control 1 (1/3) and 2 (2/3); loudness contour attack / decay / sustain; Decay switch (release = decay when on). |
| **Output** | Volume, a live scope and an interactive filter-envelope graph (static ADS from the contour settings, live playhead from the DSP, draggable Attack / Decay / Sustain / Amount handles). Header Output is extra makeup in dB. |

Controls the current routing ignores are dimmed, never hidden. Dimmed controls stay
clickable and automatable.

Header: Undo / Redo, preset menu (factory and user), Save / Load (`.dm` files in
`~/Music/dooblr-m/Presets`), Output level and meter.

Shortcuts: Cmd-Z undo, Cmd-Shift-Z or Cmd-Y redo (50 steps; only knob gestures are
recorded, not host automation). Double-click a knob to reset it; Shift-drag for fine moves.
Knobs have no value bubble or overlay while dragging (the pointer and filled arc stay; the
host still shows the value). Vertical rockers click to toggle, or drag with the lever
(up = on, down = off); dragging across neighbouring rockers flips them in one undo step.
Two-choice parameters (priority, noise colour) use that rocker; range / waveform switches
stay multi-position and step by dragging. Double-click an envelope handle to reset it.

MIDI: notes, pitch bend, mod wheel (CC1), sustain pedal. Velocity is ignored unless Velocity
is on. VST3 MIDI-CC-as-parameter emulation is compiled off, so the host sees the real
parameters only.

## Sound

- Oscillators: closed-form BLIT (Stilson/Smith SincM) with leaky integration for saw, pulse
  and triangle. Shark is a tri-saw mix. Top octave falls back to a sine. Gentle analog drift,
  independent per oscillator.
- Osc 3 Control off: oscillator 3 ignores the keyboard (C3 reference) and, in LO range, is a
  free-running LFO usable as the modulation source.
- Mixer: tanh saturation that gets hotter as more channels (and Ext/Feedback) are up.
- Filter: Huovilainen 2004 4-pole tanh ladder, 2× oversampled, half-sample delay in the
  feedback path. Self-oscillates at full resonance from thermal noise. Keyboard tracking 0,
  1/3, 2/3 or 1 (both switches).
- Contours: attack / decay / sustain. Decay switch makes release = decay; off, notes dump
  in a few milliseconds. Legato is single-trigger.
- Voice: monophonic, low-note priority by default, optional last-note, glide on the
  sample-hold.

Factory patches: Init, Classic Bass, Lead, Brass, Self-osc Filter Whistle, Pluck, Pad,
Kick / Percussion, Two-Osc Detune. All original.

## Build and install

Requirements: CMake 3.22+, a C++20 compiler. On macOS, Xcode command-line tools. JUCE 8.0.15
is fetched by CMake, or set `JUCE_SOURCE_DIR` to a local copy.

```sh
./scripts/build.sh
```

This configures `~/dooblr-build/dooblr-m`, builds the VST3, builds and runs the tests, and on
macOS ad-hoc signs and installs `dooblr-m.vst3` into `/Library/Audio/Plug-Ins/VST3` (when
writable; no sudo), then loads the installed bundle the way a host does (Tools/LoadCheck).
Options are listed at the top of the script.

Editor snapshots: `DOOBLRM_EDITOR_SNAPSHOT=/tmp/m ~/dooblr-build/dooblr-m/Tests/DooblrMTests`
writes a PNG of the board.

This repository's tests were run on Linux. They have not been claimed as a Mac or Ableton Live
verification.

## Licences and trademark

Original design, code and factory patches. The Minimoog Model D name, logo, wood and panel
artwork are not used and are not included. This is not an official emulation and is not
affiliated with any third-party manufacturer.
