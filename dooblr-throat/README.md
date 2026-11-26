# dooblr-throat

A monophonic / legato VST3 vocal instrument. MIDI in, audio out. From 0.4.0
the default **Natural** engine is a neural speech voice (Piper) resynthesized
with the WORLD vocoder at the MIDI note, so formants and duration stay put on
low and high notes (C2 to A4 tested). The 0.2.x physical model — lungs and a parametric
glottis driving a branched FDTD tract — remains the **Tract** fallback and is
still used when the phrase box is empty. Licenses for the bundled engines are
in `LICENSES.md`; this build is for personal use.

The real-time C++ core is a port of the human-voice engine from
[CHIMERA](https://github.com/jamesondh/chimera-vocal-synth) (MIT, Copyright
2026 Jameson Hodge). Credit and the MIT license text are in `LICENSES.md`.
The editor and factory presets are original dooblr work and do not use that
project's name.

On the Tract engine, expressivity on the source (spectral tilt with effort and
pitch, residual jitter plus a slightly wandering vibrato, and optional formant
tuning at high pitch) follows ideas from the CHANT singing-synthesis project
(Rodet, Potard and Barrière 1984), used only as a guide. The paper is not a
source of code or of copied text.

Consonant shapes are context-sensitive after Birkholz (2013): each consonant
is a weighted blend of its /a/, /i/ and /u/ context targets, with weights from
how close the neighbouring vowel is to those corner vowels. Tract parameters
approach those targets through a third-order dynamical system with a time
constant per articulator (lips and tongue tip faster than the tongue body;
velum on its own rate), after Birkholz (2007). Both papers were used as a
guide only. This plugin does not copy or link VocalTractLab (GPL-3.0).

Rodet, X., Potard, Y., and Barrière, J.-B. (1984). The CHANT project: From
the synthesis of the singing voice to synthesis in general. *Computer Music
Journal*, 8(3), 15–31.

Birkholz, P. (2007). Control of an articulatory speech synthesizer based on
dynamic approximation of spatial articulatory targets. *Interspeech 2007*,
2869–2872. https://www.isca-archive.org/interspeech_2007/birkholz07_interspeech.html

Birkholz, P. (2013). Modeling consonant-vowel coarticulation for articulatory
speech synthesis. *PLoS ONE*, 8(4), e60603.
https://doi.org/10.1371/journal.pone.0060603

## Host

This is an instrument: put it on a MIDI track in Ableton Live 11 (or any
VST3 instrument host). It does not belong on an audio track.

## Controls

The editor has two pages.

**Main** (everything needed to play):

- **Phrase** — type English text, then play MIDI. **Reset** restarts at the
  first syllable. The readout shows the current syllable.
- **Mode** — **Sing** plays the next syllable on each note-on (wraps) at the
  note's pitch and holds its vowel while the key is down; the closing
  consonant plays on release. **Speak** says the whole line on one note-on, at
  **Rate**, and stops on release or at the end.
- **Voice** — **Auto** darkens the formants a little on low notes (about 16%
  lower at C2, none from C4 up) so a bass note sounds like a lower voice, not a
  slowed-down one. **Bright** keeps half of that shift; **Deep** adds 10% more.
- **Rate** — Speak-mode speed. It does not change pitch.
- **Formant** — moves the whole spectral envelope darker (left) or brighter
  (right); 50% is neutral.
- **Level** — the same control as Output in the header.

**Advanced** (the Tract engine and the engine switch):

- **Engine** — **Natural** (default) or **Tract**, the 0.2.x physical model.
- **Vowel** pad and panel — named English vowels or **Pad** (height / back).
- **Voice** panel — Female, Child, Nasal, Effort, Tuning.
- **Source** — Breath, Tension, Bright, Fry, Rough, Pressure (cm H2O), Voiced.
- **Pitch** — Vibrato depth and rate, Glide, pitch-bend range.
- **Envelope** — ADSR on the glottal gate.

The Advanced controls shape the Tract engine; Natural ignores them (except
pitch bend). Empty text keeps the vowel-pad behaviour of 0.1.x (always Tract).

### How Natural works (0.4.0)

When the phrase changes, a worker thread runs Piper for the whole line and for
each syllable and stores WORLD features (F0, spectral envelope, aperiodicity)
at the voice's native 16 kHz rate (F0 floor 40 Hz). On each note-on the worker
resynthesizes the clip at the note's F0 with the envelope kept (formants stay
put), applies the Voice / Formant shift, upsamples to the host rate with a
windowed-sinc filter, and finds a vowel loop of whole pitch periods. The audio
thread only plays the finished clip (no locks, no allocation). Speak keeps 15%
of Piper's own intonation; Sing holds the note within ±15 cents. Up to 0.3.0
the clip was made once at C4 and resampled to the note, which slowed and
darkened low notes (4× longer at C2) and made them unintelligible.

On Tract, one FDTD voice; legato re-pitches the same airway with glide.

## Factory presets

Init, Close Front, Open Back, Soft Breath, Spoken, Choir Lead, Low Voice,
Whisper, Spoken Line, Sung Phrase.

## Limits of v0.4.0

- English text only. Natural uses a low-quality en_US Piper voice; the first
  render after a phrase edit takes a fraction of a second to about a second.
- Natural needs the note's clip before it can sound: about 20–35 ms after
  note-on in Sing and 55–70 ms in Speak on an M-series Mac (worker time plus
  the voice's own onset). Repeating the same syllable on the same note is
  instant. Play slightly ahead of the beat for tight timing.
- Speak does not hold its last vowel; a held Speak note ends with the line.
- Voice / Source / Effort / Tuning colour the Tract model; they do not
  reshape the Natural recording.
- The Lessac-trained voice is for personal, non-commercial use (see
  `LICENSES.md`).
- No beatbox and no creature models.
- Body (Female / Child) rebuilds the Tract airway off the audio thread; a brief
  delay is expected after those knobs move.
- The C++ tract port follows the original equations but is not claimed
  bit-identical to the Rust engine.

## Build

JUCE 8.0.15, C++20, arm64. From this directory:

```
./scripts/build.sh
```

Default build directory: `~/dooblr-build/dooblr-throat`. JUCE is reused from
`~/dooblr-build/_deps/juce-src` when present. After tests and LoadCheck pass,
the bundle is installed to `/Library/Audio/Plug-Ins/VST3/dooblr-throat.vst3`
(no sudo). Version 0.4.0.
