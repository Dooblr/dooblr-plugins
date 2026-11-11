# dooblr-0coast

A monophonic MIDI-driven synth voice (VST3 instrument). Independently designed
approximation of the Make Noise 0-Coast's "no-coast" architecture, from the
published 0-Coast manual: triangle-core oscillator, Overtone / Multiply timbre,
cycling Slope, Contour envelope, LPG-style Dynamics, MIDI B, clocked random,
Voltage MATH, a host-tempo arpeggiator, and a virtual patch bay.

It is **not** a circuit-level emulation and does not use third-party code, artwork
or trademarks on the UI.

Part of Dan Feinstein's **dooblr** suite. VST3 instrument, C++20, CMake, JUCE 8.0.15.

Version **0.1.0**. Plugin code `Dbnc`.

## Board

One page, hardware-style left-to-right: CTRL, OSCILLATOR, TIMBRE, SLOPE, CONTOUR,
DYNAMICS. Header with undo / redo, presets, output and a peak meter. A small live
oscilloscope sits in the Dynamics column (driven by the output, no hint text).

| Section | Controls |
| --- | --- |
| **CTRL** | Arp on/off, Traditional / Latch type, Clock division (host tempo), Legato, MIDI B CV / Gate sources, MIDI B LFO rate, Voltage MATH attenuverter, Bend Range. Jacks: MIDI A/B CV and Gate, Clock, Random, MATH 1/2, Tempo, MATH CH1/CH2. |
| **Oscillator** | Pitch (±5 oct), Fine (±100 ct), Portamento, Lin FM index. Triangle and square outputs, 1V/Oct and Lin FM inputs. MIDI note, bend and glide drive the core. |
| **Timbre** | Overtone (odd → even → Slope blend) and Multiply (ADAA folding), with CV attenuator / attenuverter. Slope is normalled to Multiply CV. |
| **Slope** | Cycle, Rise, Fall, Vari-Response. EOC and CV outputs; Time and Trig inputs. |
| **Contour** | Onset, Sustain, Decay, Vari-Response. EON and CV outputs; Decay CV and Gate inputs. MIDI A Gate is normalled to Contour Gate. |
| **Dynamics** | Balance (FUND / OVRTN), Balance CV, Dynamics (LPG-style amplitude + brightness with a lagged response), Level (clean to 80%, then overload). Triangle is normalled to Balance Ext; Contour is normalled to Dynamics CV. |

Every knob and switch is an automatable APVTS parameter, visible in Ableton.
Patching: drag a cable from an output jack to an input jack to override a normal;
double-click a cable to remove it. Cables are saved in plugin state.

Window 1080×660, resizable 940×600–2400×1400.

Factory patches: Init, Default Voice, Drone, Simple Bass, Flutter Arp, Percussion,
Wobble, Pitched Noise, Arcade. All original.

User presets: `~/Music/dooblr-0coast/Presets`, extension `.dcoast`.
Undo / Redo (Cmd-Z, Cmd-Shift-Z) cover every UI gesture.

## Sound

- Oscillator: triangle-core (integrated polyBLEP square) plus polyBLEP square, 2× oversampled.
- Linear FM is AC-coupled; above ~80% the bus overdrives and tracking suffers, as the manual describes.
- Overtone: Chebyshev-style odd / even harmonic, with Slope mixed into the audio path at the clockwise end.
- Multiply: first-order ADAA sine-fold.
- Slope / Contour: unipolar function generators with Vari-Response (log / lin / expo). Contour is ADSR-like; Sustain at zero is a one-shot AD (gate length ignored).
- Dynamics: one-pole low-pass + VCA sharing a lagged control (transistor LPG, vactrol-like ballistics).
- Clock follows the host tempo (120 BPM when the playhead is missing). Stepped random updates on each clock.
- Arpeggiator: held-order traditional, or Latch & Shift (up to 20 notes), at the clock rate.

## What was left out

- **Hardware calibration trimmer and MIDI-to-CV calibration routine** — a plugin tracks exactly; there is nothing to calibrate.
- **PGM A / PGM B hidden program pages as a button choreography** — those settings are exposed as normal parameters (Arp type, Legato, MIDI B sources, clock).
- **MIDI channel learn, SysEx, and CC programming of PGM pages** — the host owns MIDI routing; every panel control is already an automatable parameter.
- **External Eurorack I/O at modular level** — this is a VST3 instrument. The Dynamics jack is an internal patch point, not a second hardware output.
- **Clock-synced portamento via CC 5 / 65** — Portamento is a time knob; the host can automate it. Tempo-sync of glide is omitted.
- **Stackable physical cables / output-to-output patches** — one cable per input, outputs may fan out. That matches switching-jack normals without the analogue hazards of shorting outputs.

## Build and install

```sh
dooblr-0coast/scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-0coast`, runs `Tests/CoastTests.cpp`, ad-hoc
signs the bundle and installs it to `/Library/Audio/Plug-Ins/VST3/dooblr-0coast.vst3`
(no sudo). See the script header for `BUILD_DIR`, `RUN_TESTS`, `INSTALL_VST3`,
`LOAD_CHECK`, `JUCE_SOURCE_DIR`.

Editor snapshots: `DOOBLR0COAST_EDITOR_SNAPSHOT=/tmp/0coast ~/dooblr-build/dooblr-0coast/Tests/Dooblr0CoastTests`
writes a PNG of the board.

## Trademark

Make Noise and 0-Coast are trademarks of Make Noise Co. They are used here only to
name the published behaviour this plug-in approximates. dooblr-0coast is an original
work and is not affiliated with, endorsed by, or a product of Make Noise.
