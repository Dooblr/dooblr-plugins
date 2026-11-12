# dooblr-rhythm

Intelligent virtual percussionist for Latin, African, Afro-house, Afrobeat and
world-influenced electronic music. It generates a cohesive percussion *ensemble*
(parts that interlock around a clave or bell), not independent random patterns.
Part of Dan Feinstein's **dooblr** suite. VST3 instrument, macOS 11+ (arm64 /
x86_64) and Windows x64, C++20, CMake, JUCE 8.0.15.

Version **0.1.0**.

## What it does

One click builds a full percussion arrangement from **Style**, host **BPM**,
**time signature** and **pattern length** (1–16 bars). Variation and Regen
re-roll unlocked parts and keep locked ones.

The engine is built around musical relationships:

- Roles: timekeeper, clave/key pattern, bass-drum anchor, interlocking mids,
  lead/solo call-and-response.
- Traditional structures: son/rumba 3-2 and 2-3 clave, 6/8 bell patterns,
  polyrhythmic placement, interlocking, call-and-response.
- Instrument-specific articulations (open / slap / mute / heel-toe, bell vs
  cáscara, bass/tone/slap on djembe).
- Clave/key strokes stay put. Other parts avoid masking, interlock around the
  clave, and keep clave direction consistent across bars.

**Styles:** Son/Salsa, Rumba, Cumbia, Samba/Batucada, Afro-Cuban 6/8, West
African (djembe/dunun), Afrobeat, Afro-house, Amapiano-style log-drum-free
percussion, Baile/Brazilian funk percussion, World/Fusion.

**Instruments:** congas (quinto/conga/tumba), bongos, timbales, clave, cowbell,
guiro, shakers, maracas, djembe, talking drum, dunun (kenkeni/sangban/dundunba),
shekere, udu, agogo, cabasa, gankogui, ganzá.

Groove: swing, push/pull, style-appropriate microtiming, accents, humanize.

Sections: intro, main, fill, break, outro. Fills densify the last bar and
resolve on the loop downbeat; the clave/bell stays anchored.

## Sound

No third-party samples, logos or branding are bundled. The built-in kit is
synthesised in-plugin (modal resonators + filtered noise). Round-robin and
velocity layers are real variation: modal tuning, strike position and noise
seed change per hit.

You can drop in or load a WAV per instrument (saved in the session state). Pitch
and decay still apply.

## DAW

- Follows host tempo and transport (plays in time with Live, honours song
  position). Play in the header previews when the host is stopped.
- MIDI out of the pattern. Drag MIDI exports the whole arrangement; drag a
  sequencer row to export that instrument's MIDI.
- Drag WAV exports the loop as audio.
- MIDI input: C2 starts/stops preview when the host is stopped; the instrument
  map (C1 upward, four notes per instrument) triggers individual parts.

## Controls

Header: Undo / Redo, presets, Save / Load, Output, meter.

Arrange: Style, Bars, Meter, Clave (3-2 / 2-3), Section, host BPM, **Generate**,
Variation, Regen.

Groove macros: Complexity, Syncopation, Humanize, Swing, Push/Pull.

Grid: one row per instrument (mute / solo / lock + steps). Click a step to
toggle; drag vertically for velocity. The glyph is the articulation. The
playhead is the white line.

Selected instrument: Density, Complexity, Syncopation, Velocity, Humanize,
Timing, Volume, Pan, Pitch, Decay. Load WAV replaces that instrument's open
articulation.

User presets: `~/Music/dooblr-rhythm/Presets`, extension `.drhythm`.
Undo / Redo (Cmd-Z, Cmd-Shift-Z) cover every UI gesture.

Window 1080×660, resizable 940×600–2400×1400.

## Build

```sh
dooblr-rhythm/scripts/build.sh
```

Builds in `~/dooblr-build/dooblr-rhythm`, runs `Tests/RhythmTests.cpp`, ad-hoc
signs the bundle on macOS and installs it to
`/Library/Audio/Plug-Ins/VST3/dooblr-rhythm.vst3` when that folder is writable
(never with sudo). See the top of `scripts/build.sh` for `BUILD_DIR`,
`RUN_TESTS`, `INSTALL_VST3`, `LOAD_CHECK`, `JUCE_SOURCE_DIR`.

Editor snapshots: `DOOBLRRHYTHM_EDITOR_SNAPSHOT=/tmp/rhythm ~/dooblr-build/dooblr-rhythm/Tests/DooblrRhythmTests`

JUCE is pinned at 8.0.15 via FetchContent. `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0`.

## Licences

JUCE is used under its licence. All original design. No third-party plug-in
artwork, branding, samples or presets are included.
