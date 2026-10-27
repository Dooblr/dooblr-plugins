# dooblr-kick

A kick drum designer for heavy bass and dance music. It is a VST3 instrument for macOS, and it
uses the same look as dooblr-q. The design ideas come from layered kick synths such as D16
PunchBox and Sonic Academy Kick 3. The code, artwork, presets and sounds are all original. No
D16 or Sonic Academy material is used.

Version 0.3.0 · JUCE 8 · C++20 · VST3 instrument (MIDI in, stereo out) · arm64 macOS 11+

## Features

- **Five layers.** Each layer has its own on/solo switch, level, pan, start offset, drive send,
  and filter (off, LP, HP or BP, with cutoff and resonance).
  - **Click:** synthesised transient (chirp, noise burst, snap, tick or knock), with pitch, tone
    and length. It can also play a sample.
  - **Body:** the main tone. It has a wave shape, harmonics, tune, start phase and drive. It is
    shaped by multi-point **pitch** and **amp** envelopes (up to 8 points each, with draggable
    curve tension). It can also play a sample, optionally re-pitched by the pitch envelope
    ("Smp pitch env").
  - **Sub:** a sine at the **Key** note plus tune and fine. It has phase, AHD envelope and pitch
    envelope amount.
  - **Tail:** a rumble/boom layer with a wave, tune, drive, stereo spread and AHD. It can also
    play a sample.
  - **Noise:** coloured noise with stereo width and AHD. It can also play a sample (with its own
    sample tune).
- **The MIDI note does not change the synth.** Every note plays the same kick; only velocity
  (and note length in gate mode) matters. The kick's pitch is set by **Key** (Play panel,
  default F0) and each layer's tune. Sample layers have an opt-in **Smp key track** switch
  (default off) that re-pitches the sample by the played note relative to Key.
- **Synth / Sample switch** on the click, body, tail and noise layers. Pick one of the 21
  factory samples below, or load your own WAV/AIFF/FLAC file. User samples are saved with the
  project.
- **Tune range -48 .. +24 semitones** on the click, body, tail and noise tune knobs, shown as
  semitones and cents ("-12 st -25 ct"). Drag is fine-grained (Shift for finer, double-click
  for the default; values can be typed). A sample tuned -48 st plays 16x slower and its whole
  stretched length plays (live, in the preview and in the WAV export, up to 30 s), within the
  layer's envelope.
- **Play:** Key (default F0), velocity sensitivity, and a gate mode that holds and then
  releases the kick. (Glide was removed in 0.1.1 because the note no longer sets the pitch.)
- **Smooth parameter changes.** Level, pan, tune, drive, filter, EQ, dynamics and output
  changes are smoothed (about 10 ms), so moving a control during a held note does not zipper.
- **Drive:** soft clip, hard clip, tube, wavefolder and bitcrush (bits and rate). It has drive,
  tone, output and mix controls and is oversampled. Each layer's drive send sets how much of
  that layer goes through it.
- **EQ:** low shelf, mid peak with Q, and high shelf. The EQ page shows a live FFT of the
  post-EQ signal (4096-point, Hann, dBFS, optional peak hold) behind the response curve. Drag a
  band handle for frequency (left/right) and gain (up/down); on the Mid handle the mouse wheel
  or alt-drag sets Q (the shelves have a fixed Q). Double-click a handle for 0 dB.
- **Dynamics:** compressor (threshold, ratio, attack, release, makeup) and transient shaper
  (attack, sustain). The Dynamics page shows a scrolling history (compressor input, output,
  pre-shaper input and gain reduction) and the transfer curve with a dot at the current level.
  Drag the threshold line or the curve's threshold handle for threshold, and the curve above the
  threshold for ratio.
- **Limiter** (the last tab, formerly "Output"): limiter or clipper (or off), ceiling, release
  and output gain. The page shows the limiter's input, output (before output gain) and gain
  reduction over time, with a draggable ceiling line and GR / peak readouts.
- Dragging in a view while that section is off switches it on. Each drag is one undo step.
  The meters come from the audio thread through lock-free FIFOs (no locks or allocations on
  the audio thread) and are drawn at the display refresh rate.
- **Waveform view.** It shows the rendered hit with its length and peak. Click it to audition.
  The preview renders on a background thread (latest change wins), so turning knobs keeps the
  UI at the display frame rate.
- **Presets:** Init plus six factory kicks: Hardstyle, Techno Rumble, Dubstep Punch, House
  Thump, Trap 808-ish and DnB. User presets are saved and loaded as `.dkick` files. Random
  creates a new kick within musical ranges.
- **Export WAV.** It renders the current kick as 24-bit stereo at 48 kHz into
  `~/Music/dooblr-kick`.
- **Undo/redo** (Cmd-Z / Cmd-Shift-Z) keeps 50 steps. One drag or preset load is one step.
- **Resizable UI** from 940×600 upward.
- **Real-time-safe audio.** The audio thread does not allocate. Samples are swapped in
  atomically.

## Factory samples

There are 21 samples, all **CC0 1.0** (public domain). They are embedded in the plug-in.
Sources, original files and processing are listed in
[Resources/Samples/LICENSES.md](Resources/Samples/LICENSES.md).

| Sample | Source | Length / rate |
|---|---|---|
| Kit Kick Close, Kit Kick Mid | Virtuosity Drums by Austin McMahon (github.com/sfzinstruments/virtuosity_drums), CC0 | 1.6 s, 48 kHz / 24-bit |
| Concert BD Muted, Concert BD Open, Concert BD Boom | VCSL by Versilian Studios (github.com/sgossner/VCSL), CC0 | 1.8 / 3.5 / 4.0 s, 44.1 kHz |
| Cajon Bass, Frame Drum Mute | VCSL, CC0 | 0.67 / 0.8 s, 44.1 kHz |
| Slapstick, Woodblock, Hand Clap, Shaker Hit | VCSL, CC0 | 0.2–0.4 s, 44.1 kHz |
| Synth Kick Punch, Synth Kick Sub, Synth Tom Low, Synth Click, Synth Clap (new in 0.1.1) | FreePats *Synthesizer Percussion* 2022-07-18 by Roberto (freepats.zenvoid.org), CC0 | 0.05–0.72 s, 48 kHz / 24-bit |
| Low Tom Mallet, Timpani Low, Brake Drum Hit, Cabasa Hit, Darbuka Doum (new in 0.1.1) | VCSL, CC0 | 0.5–3.0 s, 44.1 kHz (Timpani 24-bit, others 16-bit) |

Every file is trimmed and faded, peak-normalised to -1 dBFS, and has no clipped samples.

## Build and install

Requirements: Xcode (with the macOS SDK) and CMake 3.22 or later. JUCE is fetched by CMake.
To reuse an existing JUCE checkout, pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

```sh
scripts/build.sh
```

This script does the following:

1. Builds the VST3, the tests and the LoadCheck tool, then runs the tests (`ctest`).
2. Installs `dooblr-kick.vst3` into `/Library/Audio/Plug-Ins/VST3`. It never uses sudo.
3. Checks the code signature (`codesign --verify --deep --strict`).
4. Loads the installed bundle the way a host would (Tools/LoadCheck).

See the top of the script for options. For example, `INSTALL_VST3=0` builds without
installing.

## Using it in Ableton Live

1. Rescan plug-ins: Preferences → Plug-Ins → Rescan, with VST3 system folders turned on.
2. Drag **dooblr-kick** (Plug-Ins → VST3 → dooblr) onto a **MIDI track**. It is an instrument,
   so it does not go on an audio track.
3. Play or program notes. Every note plays the same kick; set its pitch with **Key** (default
   F0; Live names middle C "C3") and the layer tunes. Turn on **Smp key track** on a sample layer
   to play that sample chromatically.

## Tests

`build/Tests/DooblrKickTests` checks the following:

- Pitch accuracy.
- Pitch envelope shape.
- Decay to silence.
- Presets, state and user presets.
- Mute/solo.
- Distortion THD.
- Velocity and gate; the synth output is identical for every MIDI note.
- The -48 .. +24 st tune range (pitch and length at -48 st, live, preview and export).
- Parameter smoothing (no steps when a parameter jumps during a note).
- Migration of 0.1.0 states.
- Factory and user samples.
- That every parameter changes the sound (no dead controls).
- Undo.
- WAV export.
- CPU use, and that the audio thread does not allocate. Only `operator new` is counted, not
  `malloc`.
- Editor layout, wiring, resizing and the background preview render.
- The meter tap (frames and spectrum during playback, full-FIFO behaviour), that the reported
  compressor / limiter gain reduction matches the gain the DSP applied, and the EQ /
  Dynamics / Limiter views (handle, threshold, ratio and ceiling drags with undo, rendering,
  audition).

`build/Tools/FrameBench/DooblrKickFrameBench` measures frame times per page while dragging.

Set `DOOBLRKICK_EDITOR_SNAPSHOT=/path/prefix` to also write editor PNG snapshots.

## Notes

The sound is designed in the spirit of PunchBox and Kick 3. It is not a clone, and it makes no
claim to match either one.
