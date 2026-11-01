# dooblr-feedback

An automatic feedback suppressor for live sound (VST3 effect, macOS arm64). Put it on a
vocal or lectern mic channel (or on the bus that feeds the PA). It listens to the live mic
(or to a separate reference mic on the sidechain), finds frequencies that are starting to
howl, and pulls each one down with a narrow notch. It leaves the rest of the sound alone.
Every notch is marked on the spectrum with its frequency, the cut it needed and how often
it came back, and can be locked or cleared. Rooms keep their own notes and presets.

Version 0.1.0. Built with JUCE 8.0.15 (C++20, CMake). **Latency: 0 samples.**

## How it works

- **Signal path.** Up to 24 IIR notch filters (zero-delay-feedback state-variable form,
  stereo, coefficients updated every 16 samples with smoothed depth, frequency and Q). There
  is no lookahead and no FFT on the audio path, so the plug-in reports and adds no latency.
  Bypass and an empty notch bank pass the input bit-exact.
- **Detection.** A 4096-point Hann FFT of the detector signal every 256 samples (5.3 ms at
  48 kHz). The detector is the plug-in's own output, which is what reaches the speakers,
  or the sidechain when **SC** is on and a key is connected. A candidate is a narrow peak
  that stands well above its neighbourhood (prominence against the bins 4–12 bins away). It
  is tracked from hop to hop, and it becomes feedback when:
  - it keeps **growing** after the analysis window has filled with it (an onset filling
    the window is not growth): at least 1.5 dB and 30 dB/s over the next 50 ms (Safe), or
    1 dB and 20 dB/s over 30 ms (Aggressive); or
  - it holds still, narrow and loud for 0.7 s (Safe) or 0.35 s (Aggressive);
  - and it is **not musical**. It is not musical when it has no octave (2f) partial of its
    own, is not a partial of a lower series (a prominent fundamental plus one more partial,
    or two of the partials right next to it, n−2 … n+2, so chords whose fundamentals mask
    each other still count), and its pitch does not drift (vibrato and glides restart the
    tracker). Odd harmonics alone do not make it musical, because a howl driven into a
    clipping amp grows 3f and 5f.
  - **Sensitivity** (0–100 %) scales the prominence, level floor and timing thresholds.
    **Safe / Aggressive** switches the whole set.
- **Notches.** A new notch starts at 6 dB (Safe) or 8 dB (Aggressive) at Q 20 (**Notch Q**).
  While the frequency still rings through the notch, the notch deepens at 60 or 90 dB/s, up
  to **Max cut**. When it rings on the notch's shoulder, the notch leans towards it and
  widens a little (Q down to 4). A howl next to an existing notch re-centres and widens
  that notch instead of adding another. When all 24 are in use, the shallowest floating
  notch is replaced. **Range low / high** limit where notches can go.
- **Float / Hold** (**Live notches**). In Float mode a floating notch that has been quiet for
  **Release** seconds fades out at 6 dB/s. In Hold mode notches stay until they are
  cleared. Fixed (locked) notches never release.
- **Ring-out.** For sound check: Aggressive thresholds, and every notch found is fixed
  (locked). Push the gain up slowly, let it catch each ring, then turn Ring-out off and the
  fixed notches stay for the show.
- **Auto** off pauses detection: the existing notches stay as they are and nothing new is added.

## Layout and controls

| Area | Controls |
| --- | --- |
| Left column | Name and version, Bypass, Safe / Aggressive, Auto, Ring-out, SC (sidechain), the big **Sensitivity** knob, Max cut, latency and status |
| Toolbar | Preset `<` list `>` and a `…` menu (Save room preset, Load room preset, Rename), Undo / Redo, A / B compare with a copy button (A→B / B→A), **Clear all** |
| Graph | Input and output spectrum (Analyzer speed sets the drawn ballistics), the summed notch curve, and a marker per notch: a chip with frequency and note name, cut and Q, hits (×n), time since it was detected, and FIX / FLT. The chip has a padlock (lock = fixed) and × (clear). A dashed line runs down to a diamond handle at the notch's depth (accent = fixed, orange = floating). When chips would overlap they collapse to numbered dots. Hover reads out the notch, or the frequency, note and output level. Output meter at the right |
| Bottom, Notches view | The notch table: #, Frequency, Note, Cut, Needed (deepest cut it has needed), Q, Level, Detected, Hits, Type, Lock / Unlock, Clear |
| Bottom, Notes view | Free-text room notes (UTF-8), saved with the session and in room presets |
| Bottom, Advanced view | Notch Q, Live notches (Float / Hold), Release, Range low, Range high, Output, Analyzer speed |

Graph mouse: click a marker to select it (and its table row). Click the padlock to toggle
fixed / floating. Click × to clear the notch. Right-click a handle to clear it. Clear all is
in the toolbar.

**Presets.** Default plus 4 original factory presets (Speech / Lectern, Vocal in a Band,
Monitor Wedge, Theatre Lav). These set only the detection settings; they leave the room's
notes and notches alone. **Room presets** (`.dfeedback`, in `~/Music/dooblr-feedback/Presets`)
save everything: parameters, notes and the current notches, fixed and floating. Save,
load and rename are in the preset menu. The session state holds the same things. Undo / Redo
(Cmd-Z, Cmd-Shift-Z) cover parameter gestures, A / B compares two complete settings
snapshots while the editor is open, and the Analyzer speed is a display preference saved
with the session.

## Measured (console tests, 48 kHz, 64-sample blocks unless noted)

Closed-loop simulation: mic = tanh(pink noise at −40 dB + G × resonance(speaker delayed
10 ms, Q 30 at 2 kHz)), speaker = the plug-in's output. tanh stands in for the PA
clipping. These are simulations, not a room.

| Case | Without the plug-in | With the plug-in |
| --- | --- | --- |
| Loop gain 1.2 (+1.6 dB) | howl +6 dB over the noise at 140 ms, +20 dB at 250 ms, settles at −5.3 dB RMS (clipping) | first notch at 192 ms, peak +12.4 dB over the noise (10 ms RMS), then 0.1 dB over the noise |
| Loop gain 1.05 (+0.4 dB) | +6 dB at 690 ms, +20 dB at 1090 ms, settles at −10.4 dB RMS | first notch at 603 ms, peak +6.2 dB, then 0.1 dB over the noise |
| Three resonances (700 / 2500 / 5300 Hz, gain 1.15) | runaway at −6.0 dB RMS | all three notched, first at 235 ms; −39.9 dB (the noise floor) |
| Max cut 6 dB, gain 1.6 | — | every notch capped at 6.0 dB |

- **Detection time.** About 150–250 ms from the first sign of a runaway at 48 kHz and
  above (149–203 ms at 48–192 kHz, 337 ms at 44.1 kHz). That is slower than the 50–150 ms
  aimed for: the 4096-point window has to fill (about 60 ms) before growth can be told from
  an onset, and then the growth has to hold for 30–50 ms. A tone growing at 40 dB/s through
  −40 dB pink noise gets 3 dB of cut 487–812 ms after it starts, while it is still −10.5 to +2.5 dB re the
  noise RMS (250 Hz – 8 kHz, both modes).
- **Block size.** 1–512 samples: the same 192–245 ms. 2048-sample blocks: 1067 ms (the round
  trip in the simulation is then at least 2048 samples, as it would be in a host). It
  still settles at the noise floor.
- **False positives** (6 s each, sensitivity 50 %): a sustained C-E-G chord with 10 harmonics
  per note, a 220 Hz vowel with 5.5 Hz vibrato, a 55 Hz bass with 16 harmonics and a sine
  melody of 300 ms notes give **0 notches** in Safe and in Aggressive. Pink and white
  noise: 0. Synthetic speech: 0 in Safe, **3 in Aggressive** (157–281 Hz, on its steady
  formant-like partials). A steady pure sine is notched after about 0.7 s in Safe mode. That
  is by design: a stable pure tone looks exactly like feedback, so keep test tones away
  from it.
- **Audio quality.** Zero latency. Bit-exact null with no notches and in Bypass. A 3 kHz
  notch engaging under a 200 Hz sine changes it by at most 0.004 (second-difference
  deviation 0.00027, no clicks). No NaN or denormals with silence, 1e-30 input or extreme
  settings, at 44.1–192 kHz.
- **CPU** (one core, Apple silicon, 48 kHz stereo, 64-sample blocks): 0.8–0.9 % of real time
  with detection and no notches, 1.2–1.3 % with all 24 notches. Zero allocations in
  `processBlock`.

## Build

```sh
./scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-feedback`, runs the console tests (`Tests/FeedbackTests.cpp`)
and a host-style load check (`Tools/LoadCheck`, which also checks that the latency is 0),
ad-hoc signs the bundle and installs it to `/Library/Audio/Plug-Ins/VST3/dooblr-feedback.vst3`
(no sudo). Set `DOOBLRFEEDBACK_EDITOR_SNAPSHOT=/path/prefix` when running the test binary to
write editor PNG snapshots.

## Tests

The 18 console tests drive the real processor and editor:
- **Metadata and latency:** metadata; 0 samples of latency (reported and measured) with a null when idle.
- **Feedback simulation:**
  - the closed-loop simulation above at two loop gains;
  - three simultaneous feedback frequencies;
  - detection speed in Safe and Aggressive at 250 Hz – 8 kHz.
- **False positives:** music (chord, vibrato, bass, melody) and speech / noise.
- **Sidechain:** detection follows the reference mic, and SC on with no key connected falls back to the plug-in's own signal.
- **Notch handling:**
  - clear one, clear all, lock;
  - Float release vs Hold;
  - Ring-out fixing its notches;
  - the 24-notch bank;
  - Max cut and no zipper noise.
- **State and presets:**
  - session state, UTF-8 notes and notches round trip;
  - room presets save, load and rename;
  - factory presets leave notes and notches alone;
  - undo and A / B;
  - value text.
- **Extremes:** 44.1–192 kHz, blocks of 1–2048, NaN / denormal input.
- **Real-time and CPU:** no allocation in `processBlock`, CPU.
- **Editor:**
  - layout and every control at 940×600, 1080×660 and 2400×1400;
  - markers for fixed and floating notches;
  - notes typing reaching the processor;
  - snapshots.
- **Real mouse** events through a virtual window peer on:
  - the view tabs and every control in all three views;
  - marker select, lock and clear, and right-click on a handle;
  - the table's Lock and Clear buttons;
  - Clear all, preset `<` / `>`, A / B, Undo / Redo.
- **Analyzer speed** setting.

## Notes and limits

- **Not yet tested in a room or a DAW.** All the numbers above come from simulations and
  synthetic signals.
- A notch can only cut feedback that is already audible in the detector. It buys headroom;
  it does not replace mic placement and system EQ.
- Feedback at a frequency that is exactly a partial of a note being played at the same moment
  can read as music and be held back until the note stops.
- Original DSP, UI and presets; no third-party code, artwork or presets.
