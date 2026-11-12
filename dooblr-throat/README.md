# dooblr-throat

A monophonic / legato VST3 vocal instrument. MIDI in, audio out. It is a
physical model of the human airway: lungs and a parametric glottis drive a
branched transmission-line tract (pharynx, oral, nasal) stepped with a
staggered-grid FDTD, then decimated back to the host rate. There are no
samples and no neural vocoder.

The real-time C++ core is a port of the human-voice engine from
[CHIMERA](https://github.com/jamesondh/chimera-vocal-synth) (MIT, Copyright
2026 Jameson Hodge). Credit and the MIT license text are in `LICENSES.md`.
The editor and factory presets are original dooblr work and do not use that
project's name.

## Host

This is an instrument: put it on a MIDI track in Ableton Live 11 (or any
VST3 instrument host). It does not belong on an audio track.

## Controls

- **Vowel** — named English vowels, or **Pad** to morph in height (close–open)
  and back (front–back) space. The pad shows the calibrated vowel targets.
- **Voice** — Female and Child blend the body (tract scale / formants). Nasal
  raises the velum port when turned up; zero leaves the vowel's velum.
- **Source** — Breath, Tension, Bright, Fry, Rough, Pressure (cm H2O), Voiced.
- **Pitch** — Vibrato depth and rate, Glide, pitch-bend range.
- **Envelope** — ADSR on the glottal gate. Output level is in the header.
- **Phrase** — type English text and play MIDI. **Sing** advances one syllable
  per note (pitch from the key, vowel held, coda on release or the next note;
  wraps; **Reset** restarts). **Speak** says the whole line on one note-on, at
  **Rate**, around the note's pitch, and stops on release or at the end. The
  readout shows the current phoneme / syllable. Empty text keeps the vowel-pad
  behaviour of 0.1.x.

One FDTD voice; legato re-pitches the same airway with glide.


## Factory presets

Init, Close Front, Open Back, Soft Breath, Spoken, Choir Lead, Low Voice,
Whisper, Spoken Line, Sung Phrase.

## Limits of v0.2

- English text only. Intelligibility is that of a tract model, not a commercial
  vocoder; some words will be clearer than others.
- No beatbox and no creature models.
- Body (Female / Child) rebuilds the airway off the audio thread; a brief
  delay is expected after those knobs move.
- The C++ port follows the original equations but is not claimed bit-identical
  to the Rust engine.

## Build

JUCE 8.0.15, C++20, arm64. From this directory:

```
./scripts/build.sh
```

Default build directory: `~/dooblr-build/dooblr-throat`. JUCE is reused from
`~/dooblr-build/_deps/juce-src` when present. After tests and LoadCheck pass,
the bundle is installed to `/Library/Audio/Plug-Ins/VST3/dooblr-throat.vst3`
(no sudo). Version 0.1.1.
