# dooblr-feedback

An automatic feedback suppressor for live sound (VST3 effect, macOS arm64). Put it on a
vocal or lectern mic channel (or on the bus that feeds the PA). It listens to the live mic
(or to a separate reference mic on the sidechain), finds frequencies that are starting to
howl, and pulls each one down with a narrow notch. It leaves the rest of the sound alone.
Every notch is marked on the spectrum with its frequency, the cut it needed and how often
it came back, and can be locked or cleared. Rooms keep their own notes and presets. As a
last resort, a feedback-gated **Safety duck** pulls the whole output down when a howl gets
past the notches.

Version 0.1.1. Built with JUCE 8.0.15 (C++20, CMake). **Latency: 0 samples.**

## How it works

- **Signal path.** Up to 24 IIR notch filters (zero-delay-feedback state-variable form,
  stereo, coefficients updated every 16 samples with smoothed depth, frequency and Q). There
  is no lookahead and no FFT on the audio path, so the plug-in reports and adds no latency.
  Only live notch slots are processed (an idle slot costs nothing). Bypass and an empty notch
  bank pass the input bit-exact.
- **Detection.** A 4096-point Hann FFT of the detector signal every 256 samples (5.3 ms at
  48 kHz). The detector is the plug-in's own output, which is what reaches the speakers,
  or the sidechain when **SC** is on and a key is connected. A candidate is a narrow peak
  that stands well above its neighbourhood (prominence against the bins 4–12 bins away). It
  is tracked from hop to hop, and it becomes feedback when:
  - it keeps **growing** after the analysis window has filled with it (an onset filling
    the window is not growth): at least 1.5 dB and 30 dB/s over the next 50 ms (Safe), or
    1 dB and 20 dB/s over 30 ms (Aggressive); or
  - it holds still, narrow and loud for 0.7 s (Safe) or 0.35 s (Aggressive);
  - or it is a **runaway**: a seed peak that rises 9 dB at 60 dB/s or more within 80 ms (this
    catches the fast howls in a cluster of closely spaced room modes, typical above 3 kHz,
    that 0.1.0 confirmed late or not at all);
  - and it is **not musical**. It is not musical when it has no octave (2f) partial of its
    own, is not a partial of a lower series (a prominent fundamental plus one more partial,
    or two of the partials right next to it, n−2 … n+2, so chords whose fundamentals mask
    each other still count), and its pitch does not drift (vibrato and glides restart the
    tracker). Odd harmonics alone do not make it musical, because a howl driven into a
    clipping amp grows 3f and 5f.
  - **Sensitivity** (0–100 %) scales the prominence, level floor and timing thresholds.
    **Safe / Aggressive** switches the whole set.
- **Notches.** A new notch starts at 6 dB (Safe) or 8 dB (Aggressive) at Q 20 (**Notch Q**),
  deeper for a fast-growing howl (scaled by its growth rate). While the frequency still rings
  through the notch, the notch deepens at 60 or 90 dB/s, up to **Max cut**.
- **One notch per feedback frequency.** A howl that is re-confirmed inside an existing
  notch, or next to it, retunes and deepens that notch (re-centred halfway and widened a
  little, Q down to 4) instead of adding another. A notch only leans towards a peak on its
  shoulder while that peak is growing, and the lean is bounded to 0.3 bandwidths around the
  last confirmed howl, so speech and instruments cannot walk it off the howl it holds. Just
  after a catch (first 0.4 s), a peak as loud as the catch beside the notch means the
  estimate landed on a neighbouring room mode: the notch moves to it within 50 ms. Two
  notches that drift onto the same frequency are merged: the survivor takes the deeper cut
  and the other fades out at 60 dB/s, with no gap in the cut. When all 24 are in use, the
  shallowest floating notch is replaced.
- **Range low / high** limit where notches can go. Range high now defaults to 20 kHz (0.1.0:
  16 kHz, which hid every howl at 16–20 kHz). Sessions saved by 0.1.0 with Range high at
  exactly 16 kHz are moved to 20 kHz when loaded; any other value is kept.
- **Max cut** runs 3–30 dB and then **∞** (the top detent): a true notch, cut to the bottom of
  the filter (more than 120 dB in the tests). The depth glides there per control tick, so it
  is click-free. Default 18 dB. The value reads and types as "∞ dB" (also "inf"). The
  normalised automation mapping changed in 0.1.1 (the top 4 % of the knob is ∞); sessions
  store dB, so they load with the same value.
- **Safety duck** (Advanced view: Safety duck, Duck when, Threshold, Depth, Recover). The last
  resort. When the output peak is over **Threshold** (−24 to 0 dBFS, default −6) *and* the
  detector reports a runaway (within the last 0.5 s a notch was engaged or re-triggered, a
  ringing notch sat at Max cut, or a confirmed howl found the bank full), the main level falls
  by **Depth** (3–40 dB or **Mute**, default 12 dB) with a 5 ms time constant, is held while
  the condition lasts plus 80 ms, then recovers over **Recover** (0.2–5 s, default 1.5 s),
  linear in dB. "Duck when: Always" drops the feedback condition (a plain over-level duck).
  Loud music without feedback never ducks in Feedback mode. A red **SAFETY −12 dB** badge
  shows in the toolbar while it ducks, the status line counts the ducks, and the output
  meter shows the threshold as a red dashed tick. On by default, Feedback mode. No latency.
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
| Graph | Input and output spectrum (Analyzer speed sets the drawn ballistics), the summed notch curve, and a marker per notch: a chip with frequency and note name, cut and Q, hits (×n), time since it was detected, and FIX / FLT. The chip has a padlock (lock = fixed) and × (clear). A dashed line runs down to a diamond handle at the notch's depth (accent = fixed, orange = floating). **Calm labels:** chips sit in stable lanes and are only re-laid out when a notch is added or cleared or the editor is resized, never because a level or cut changed; moves ease over 250 ms, values refresh at most 5 times a second, and a chip fades (300 ms) to a small numbered dot once its notch has been quiet for 4 s. Hovering a dot, its table row or the graph near it, or selecting it, expands it again. Chips never overlap: those that do not fit collapse to numbered dots. Hover reads out the notch, or the frequency, note and output level. Output meter at the right, with the Safety threshold tick |
| Bottom, Notches view | The notch table: #, Frequency, Note, Cut, Needed (deepest cut it has needed), Q, Level, Detected, Hits, Type, Lock / Unlock, Clear |
| Bottom, Notes view | Free-text room notes (UTF-8), saved with the session and in room presets |
| Bottom, Advanced view | Notch Q, Live notches (Float / Hold), Release, Range low, Range high, Output, Analyzer speed; Safety duck, Duck when, Threshold, Depth, Recover |

Graph mouse: click a marker to select it (and its table row). Click the padlock to toggle
fixed / floating. Click × to clear the notch. Right-click a handle to clear it. Clear all is
in the toolbar.

**Presets.** Default plus 5 original factory presets (Speech / Lectern, Vocal in a Band,
Monitor Wedge, Theatre Lav, Ring-out Start). These set only the detection settings; they leave the room's
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
| Loop gain 1.2 (+1.6 dB) | howl +6 dB over the noise at 140 ms, +20 dB at 250 ms, settles at −5.3 dB RMS (clipping) | first notch at 187 ms, peak +12.1 dB over the noise (10 ms RMS), then 0.1 dB over the noise |
| Loop gain 1.05 (+0.4 dB) | +6 dB at 690 ms, +20 dB at 1090 ms, settles at −10.4 dB RMS | first notch at 699 ms, peak +6.2 dB, then 0.1 dB over the noise |
| Three resonances (700 / 2500 / 5300 Hz, gain 1.15) | runaway at −6.0 dB RMS | all three notched, first at 251 ms; −39.9 dB (the noise floor) |
| Max cut 6 dB, gain 1.6 | — | every notch capped at 6.0 dB |

- **Detection time.** About 150–270 ms from the first sign of a runaway (165–221 ms at
  48–192 kHz, 267 ms at 44.1 kHz). That is slower than the 50–150 ms
  aimed for: the 4096-point window has to fill (about 60 ms) before growth can be told from
  an onset, and then the growth has to hold for 30–50 ms. A tone growing at 40 dB/s through
  −40 dB pink noise gets 3 dB of cut 391–844 ms after it starts, while it is still −14.4 to +3.8 dB re the
  noise RMS (250 Hz – 8 kHz, both modes).
- **Block size.** 1–33 samples: the same 187 ms; 512: 469 ms; 2048-sample blocks: 1152 ms (the round
  trip in the simulation is then at least 2048 samples, as it would be in a host). It
  still settles at the noise floor.
- **False positives** (6 s each, sensitivity 50 %): a sustained C-E-G chord with 10 harmonics
  per note, a 220 Hz vowel with 5.5 Hz vibrato, a 55 Hz bass with 16 harmonics and a sine
  melody of 300 ms notes give **0 notches** in Safe and in Aggressive. Pink and white
  noise: 0. Synthetic speech: 0 in Safe and in Aggressive (0.1.0: 3 in Aggressive). A steady pure sine is notched after about 0.7 s in Safe mode. That
  is by design: a stable pure tone looks exactly like feedback, so keep test tones away
  from it.
- **Audio quality.** Zero latency. Bit-exact null with no notches and in Bypass. A 3 kHz
  notch engaging under a 200 Hz sine changes it by at most 0.004 (second-difference
  deviation 0.00027, no clicks). No NaN or denormals with silence, 1e-30 input or extreme
  settings, at 44.1–192 kHz.
- **CPU** (one core, Apple silicon, 48 kHz stereo, 64-sample blocks, three runs on
  2026-11-01): 0.9–1.1 % of real time with detection and no notches, 0.6–0.7 % with 1 notch,
  1.2–1.4 % with all 24 notches (the numbers move by ±0.2 % between runs; 0.1.0 measured
  1.2 % / 1.8 % for none / 24 in the same session). Only active slots are filtered. Zero
  allocations in `processBlock`.
- **Max cut ∞:** a 1 kHz tone is cut by 127.3 dB with a full notch; ramping to it, the largest
  sample step is 0.0650 (the sine's own step is 0.0654), so no click.
- **Safety duck** (2 kHz, loop gain 1.6, Max cut 3 dB so the notches cannot hold): −0.9 dBFS
  without the duck, −35.0 dBFS peak with it after the first 30 ms over the threshold. Depth
  Mute: −149.5 dBFS 60–80 ms after the output reached the threshold. Recovery: worst gain step
  0.21 dB per 5 ms. Loud music (−1.3 dBFS peaks, no feedback): 0 ducks in Feedback mode.
- **Calm labels:** over 5 s of fluctuating howls, labels moved 0.00 px outside add / clear /
  open / close events (2 re-layouts in 300 frames). With 24 notches: 8 / 10 / 19 full labels
  (rest as dots) at 940×600 / 1080×660 / 2400×1400, none overlapping.

### High frequencies (0.1.1 fix)

0.1.0 missed or barely held feedback above about 8 kHz. The test builds a realistic room:
a cluster of room modes around the howl (comb response with the loop delay), air absorption,
a speaker roll-off, room noise or speech plus room noise, tanh clipping. Loop gains 1.02,
1.10 and 1.30, 3–18 kHz, 44.1 / 48 / 96 kHz, 64–2048-sample buffers: 59 cases. "Caught"
means notched and held within 3 dB of the loop-free level. 0.1.0 is the 47f3907 engine run
through the same test.

- 0.1.0: **39 of 59** caught. 0.1.1: **59 of 59**.
- Root causes, with evidence from the per-case logs:
  1. **Range high defaulted to 16 kHz.** Every 16–18 kHz case at 48 kHz / 64 (both programmes,
     all gains), 16 kHz at 48 kHz / 256 and 18 kHz at 44.1 and 96 kHz got no notch at all (+79 to +85 dB, the loop ran
     to clipping). Now 20 kHz, with session migration.
  2. **Late confirmation in a cluster of modes.** Neighbouring modes beat, so the growth test
     kept restarting; with 512–2048-sample buffers the round trip made it worse (48 kHz / 2048:
     5 kHz notched at 4.4 s and held 10.8 dB over, 10 and 16 kHz never). Fixed with the runaway
     seed path above; now 3.5–3.9 s at 2048 and held within 0.7 dB.
  3. **Notch on the wrong mode.** For a howl at 11903 Hz, 0.1.0's notch sat at 12033 Hz (a
     neighbouring mode): 8.6 dB deep there, but only 6.0 dB at the howl, which kept running
     2.1–2.4 dB over the loop-free level. Fixed by the phase-vocoder frequency estimate, the
     early re-centre and the bounded lean; now −8.0 to −12.0 dB at the howl at gain 1.30.
  4. **Cut too shallow.** 0.1.0 often stopped at the 6 dB first cut (800 Hz under speech:
     right frequency, 6.3 dB, held 6.8 dB over; 8 kHz with speech at gain 1.30: 3.1 dB over).
     The first cut now scales with the growth rate and a re-confirmed howl deepens its notch.
- Per-octave whitening and a shorter-hop HF detector were considered but not needed: with the
  fixes above all 59 cases are caught with the existing 4096-point detector.
- **Cut at the exact frequency.** The "cut at the howl" column below is the summed notch
  bank's response at the measured howl frequency (not at the notch centre).

At 48 kHz / 64, both programmes (room noise; speech + room), all three loop gains:

| Howl | 0.1.0 caught | 0.1.0 worst steady | 0.1.1 caught | 0.1.1 worst steady | 0.1.1 cut at the howl | 0.1.1 notch in |
| --- | --- | --- | --- | --- | --- | --- |
| 3 kHz | 6 / 6 | +1.6 dB | 6 / 6 | +1.3 dB | -9.7 to -13.5 dB | 187–384 ms |
| 5 kHz | 6 / 6 | +0.8 dB | 6 / 6 | +0.4 dB | -6.4 to -13.7 dB | 181–1093 ms |
| 8 kHz | 5 / 6 | +3.1 dB | 6 / 6 | +1.1 dB | -5.6 to -11.3 dB | 219–971 ms |
| 10 kHz | 6 / 6 | +2.1 dB | 6 / 6 | +1.9 dB | -6.0 to -12.0 dB | 293–1072 ms |
| 12 kHz | 6 / 6 | +2.7 dB | 6 / 6 | +2.7 dB | -5.2 to -12.0 dB | 224–1003 ms |
| 16 kHz | 0 / 6 | +83.7 dB | 6 / 6 | +1.7 dB | -6.0 to -12.0 dB | 224–693 ms |
| 18 kHz | 0 / 6 | +85.5 dB | 6 / 6 | +2.0 dB | -6.0 to -12.0 dB | 235–1648 ms |

| Rate / buffer, gain 1.10, room noise | Howl | 0.1.0 | 0.1.1 |
| --- | --- | --- | --- |
| 44.1 kHz / 64 | 5 kHz | caught, steady +0.7 dB | caught, steady +0.9 dB, cut -9.2 dB |
| 44.1 kHz / 64 | 10 kHz | caught, steady +1.8 dB | caught, steady +1.0 dB, cut -8.9 dB |
| 44.1 kHz / 64 | 16 kHz | caught, steady +3.0 dB | caught, steady +1.2 dB, cut -8.8 dB |
| 44.1 kHz / 64 | 18 kHz | **missed**, steady +85.4 dB | caught, steady +0.9 dB, cut -9.4 dB |
| 48 kHz / 256 | 5 kHz | caught, steady +0.2 dB | caught, steady +0.2 dB, cut -9.5 dB |
| 48 kHz / 256 | 10 kHz | caught, steady +2.1 dB | caught, steady +1.3 dB, cut -9.2 dB |
| 48 kHz / 256 | 16 kHz | **missed**, steady +82.8 dB | caught, steady +0.8 dB, cut -8.9 dB |
| 48 kHz / 512 | 5 kHz | caught, steady +2.3 dB | caught, steady +0.4 dB, cut -10.9 dB |
| 48 kHz / 512 | 10 kHz | **missed**, steady +3.1 dB | caught, steady +2.0 dB, cut -8.3 dB |
| 48 kHz / 512 | 16 kHz | caught, steady +2.7 dB | caught, steady +1.8 dB, cut -7.9 dB |
| 48 kHz / 2048 | 5 kHz | **missed**, steady +10.8 dB | caught, steady +0.0 dB, cut -16.3 dB |
| 48 kHz / 2048 | 10 kHz | **missed**, steady +75.5 dB | caught, steady +0.7 dB, cut -18.0 dB |
| 48 kHz / 2048 | 16 kHz | **missed**, steady +82.5 dB | caught, steady +0.1 dB, cut -18.0 dB |
| 96.0 kHz / 64 | 5 kHz | caught, steady +1.3 dB | caught, steady +0.7 dB, cut -8.0 dB |
| 96.0 kHz / 64 | 10 kHz | caught, steady +2.2 dB | caught, steady +1.3 dB, cut -8.8 dB |
| 96.0 kHz / 64 | 16 kHz | caught, steady +2.0 dB | caught, steady +1.4 dB, cut -8.5 dB |
| 96.0 kHz / 64 | 18 kHz | **missed**, steady +79.0 dB | caught, steady +1.3 dB, cut -9.7 dB |

False positives on bright programme (6 s each, synthetic and original: cymbal crashes,
closed / open hi-hats, sibilant speech, a polyBLEP sawtooth chord, a strummed bright
guitar): **Safe: 0 notches on every one** (0.1.0: 1 on sibilant speech). Aggressive mode still
notches bright programme: cymbals 8, hi-hats 5, saw chord 1, guitar 8, sibilance 0 (0.1.0:
2 / 5 / 1 / 5 / 2). Aggressive is meant for ring-out and speech; use Safe for music.

**One notch per howl** (10 cases: steady, between bins, drifting 1.5–6 %, warbling, fast
12 kHz, 800 Hz under speech, Q 60 with drift and warble, two howls 1/3 octave apart): one
notch per howl in every case, never more at once. 0.1.0 stacked 3 notches on the Q 60
drifting case and held 800 Hz under speech 6.8 dB over the loop-free level (0.1.1: 3.5 dB).



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

The 24 console tests (553 checks; 555 when the editor snapshots are rendered) drive the real processor and editor:
- **Metadata and latency:** metadata; 0 samples of latency (reported and measured) with a null when idle.
- **Feedback simulation:**
  - the closed-loop simulation above at two loop gains;
  - three simultaneous feedback frequencies;
  - detection speed in Safe and Aggressive at 250 Hz – 8 kHz;
  - **high-frequency feedback**: 59 room cases at 3–18 kHz (above);
  - **one howl, one notch**: merge, retune, consolidate (10 cases plus the consolidation crossfade).
- **False positives:** music (chord, vibrato, bass, melody), speech / noise, and **bright
  programme** (cymbals, hi-hats, sibilance, saw chord, guitar).
- **Sidechain:** detection follows the reference mic, and SC on with no key connected falls back to the plug-in's own signal.
- **Notch handling:**
  - clear one, clear all, lock;
  - Float release vs Hold;
  - Ring-out fixing its notches;
  - the 24-notch bank;
  - Max cut and no zipper noise;
  - **Max cut ∞**: full notch depth, click-free ramp, "∞ dB" text both ways, old sessions.
- **Safety duck:** runaway the notches cannot hold, Depth Mute, recovery, an extreme runaway,
  loud music not ducked (Feedback mode) and ducked (Always), the badge, status and meter tick.
- **State and presets:**
  - session state, UTF-8 notes and notches round trip;
  - 0.1.0 sessions: Max cut in dB loads the same, Range high at 16 kHz moves to 20 kHz;
  - room presets save, load and rename;
  - factory presets leave notes and notches alone;
  - undo and A / B;
  - value text.
- **Extremes:** 44.1–192 kHz, blocks of 1–2048, NaN / denormal input.
- **Real-time and CPU:** no allocation in `processBlock`, CPU with none / 1 / 24 notches.
- **Editor:**
  - layout and every control at 940×600, 1080×660 and 2400×1400;
  - markers for fixed and floating notches;
  - **calm labels**: stable lanes, no overlap, idle labels close, 24 notches at three sizes;
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
- Aggressive mode notches bright music (cymbals, hi-hats, guitar); see above.
- The Safety duck is a level duck, not a fix: it acts only after the notches have failed.
- Original DSP, UI and presets; no third-party code, artwork or presets.
