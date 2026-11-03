# dooblr-kick

A kick drum designer for heavy bass and dance music. It is a VST3 instrument for macOS, and it
uses the same look as dooblr-q. The design ideas come from layered kick synths such as D16
PunchBox and Sonic Academy Kick 3. The code, artwork, presets and sounds are all original. No
D16 or Sonic Academy material is used.

Version 0.3.7 · JUCE 8 · C++20 · VST3 instrument (MIDI in, stereo out) · arm64 macOS 11+

## Features

- **Five layers.** Each layer has its own on/solo switch, level, pan, start offset, drive send,
  and filter (off, LP, HP or BP, with cutoff and resonance).
  - **Click:** synthesised transient (chirp, noise burst, snap, tick or knock), with pitch and
    tone. Its amp envelope sets its length and shape. It can also play a sample.
  - **Body:** the main tone. It has a wave shape, harmonics, tune, start phase and drive. It is
    shaped by multi-point **pitch** and **amp** envelopes (up to 8 points each, with draggable
    curve tension). It can also play a sample, optionally re-pitched by the pitch envelope
    ("Smp pitch env").
  - **Sub:** a sine at the **Key** note plus tune and fine. It has phase and "Body pitch" (how
    far it follows the Body pitch envelope).
  - **Tail:** a rumble/boom layer with a wave, tune, drive and stereo spread. It can also play
    a sample.
  - **Noise:** coloured noise with stereo width. It can also play a sample (with its own sample
    tune).
  - **Every layer has its own pitch and amp envelopes (0.3.6)**, like the Body: see below.
- **The MIDI note does not change the synth.** Every note plays the same kick; only velocity
  (and note length in gate mode) matters. The kick's pitch is set by **Key** (Play panel,
  default F0) and each layer's tune. Sample layers have an opt-in **Smp key track** switch
  (default off) that re-pitches the sample by the played note relative to Key.
- **Synth / Sample switch** on the click, body, tail and noise layers. Pick one of the 21
  factory samples below, or load your own WAV/AIFF/FLAC file. User samples are saved with the
  project. The layer page shows only the controls the selected source uses (Synth hides the
  sample picker, file loader, key tracking and sample-only knobs; Sample hides the synth wave /
  type, harmonics, phase, spread, colour and width). Hidden settings keep their values. The
  layout does not move when you switch: shared controls keep the same place in both modes, and
  each mode's own controls use fixed slots.
- **Level fader.** Each layer's level is a tall vertical fader at the left of its page, with a
  dB readout (double-click for the default, Shift-drag for fine steps). It is the same
  "Level" parameter as before, so projects and presets are unchanged.
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
- **Limiter** (the last tab, formerly "Output"): limiter or clipper, ceiling, release
  and output gain. The page shows the limiter's input, output (before output gain) and gain
  reduction over time, with a draggable ceiling line and GR / peak readouts.
- **On/off is on the tabs.** Each processing tab (Drive, EQ, Dynamics, Limiter) has a power
  toggle; the panels and views have no on/off switches, and editing a section that is off
  leaves it off. A switched-off tab is drawn dimmed and can still be selected. The Dynamics
  toggle switches the transient shaper and compressor together (on restores whichever were on;
  both if none is remembered). The Limiter toggle switches between Off and the last mode
  (Clipper or Limiter; Limiter by default). Each toggle is one undo step and follows
  automation and presets. Each drag in a view is one undo step.
  The meters come from the audio thread through lock-free FIFOs (no locks or allocations on
  the audio thread) and are drawn at the display refresh rate.
- **Envelope page (master envelope).** A multi-point gain envelope over the whole kick, up to
  6000 ms. Its tab has a power toggle (off is a unity bypass; the tab and graph are drawn
  dimmed and can still be edited). Knobs under the graph follow the selected point (Time, Level
  and the Curve into that point; point 0 has Level only) and are the existing "Master Env N"
  host parameters, so they map and automate like any knob. Two extra automatable macros sit
  beside them: **Time Scale** (25 % .. 400 %, stretches every point's time) and **Depth**
  (0 .. 100 %, blends between flat unity and the drawn shape). At their defaults (100 %, 100 %)
  the sound is unchanged.
  Since 0.3.7 the Time Scale knob is twice the diameter of the other knobs, with a larger
  caption and readout; its caption sits level with the panel title and its knob lines up with
  the point knobs beside it.
- **Zoom on envelope graphs.** On the master envelope and the Body pitch and amp envelopes,
  drag up or down in an empty part of the graph (not on a line or point) to zoom the time
  axis in or out around the mouse. Double-click the time ruler to show the whole envelope
  again. Zoom only changes the view, not the sound, and is not an undo step.
- **Pan and auto-fit (0.3.4).** A drag in empty graph space locks to the direction it starts
  in: mostly up / down zooms, mostly sideways pans (hand cursor). A horizontal trackpad swipe
  or Shift + wheel pans too; Cmd / Ctrl + wheel or a pinch zooms. The view stays inside
  0 ms .. the graph's maximum. Unless you have zoomed or panned, the view glides (about
  200 ms, eased) to fit 0 ms .. the last point whenever that changes: adding, removing or
  dragging a point, Time Scale, presets, undo, Random, switching the envelope on. A point
  dragged past the edge stays in view. Double-click the ruler to go back to auto-fit.
- **Playhead (0.3.4).** While a hit plays (MIDI or the Play button) a line sweeps across the
  master envelope and the Body pitch / amp graphs at the hit's real time, with a short trail
  and a dot on the curve; it runs until the sound has actually ended (past the last point if
  the layers ring on), then fades. A retrigger starts it again. The Body graphs account for
  the Body Start delay. The position comes from the audio thread lock-free (one atomic).
- **Header meter (0.3.4).** A small stereo peak meter at the top right shows the final output
  (after Output): green below -6 dBFS, yellow up to 0, red at 0 dBFS and above, with a peak
  hold and clip lights that stay red until you click the meter. The subtitle under the name
  shows the version.
- The envelope graphs carry no help or status captions (0.3.4); hover or drag a point to see its
  time and value.
- **Waveform view.** It shows the rendered hit with its length and peak, drawn anti-aliased. Click it to audition.
  The preview renders on a background thread (latest change wins), so turning knobs keeps the
  UI at the display frame rate.
- **Presets:** Init plus six factory kicks: Hardstyle, Techno Rumble, Dubstep Punch, House
  Thump, Trap 808-ish and DnB. User presets are saved and loaded as `.dkick` files. Random
  creates a new kick within musical ranges, as one undo step. It rolls the whole Click and
  Body layers (Synth or Sample source, sample choice, click type, pitch and length, body wave,
  tune, harmonics and drive, levels and filters), the pitch and amp envelopes, Sub, Tail, Noise
  and the Drive stage. It also switches each of the five layers on or off (Click and Body 80 %
  of the time, Sub 60 %, Tail and Noise 30 %), keeps a soloed layer on, and always leaves at
  least one layer on; a layer heard on its own gets at least -9 dB. It leaves key tracking, solo, the Key, the output gain, EQ, Dynamics,
  the Limiter and the Envelope page as they are.
- **Export (0.3.5).** The header's Export button opens a save dialog (default name
  `dooblr-kick.wav`, in the last folder you exported to, which the plugin state remembers)
  and renders one hit at the Key as 24-bit stereo at 48 kHz. `.wav` is added if you leave it
  off; Cancel does nothing; a failure shows in the waveform view's status line. The Play
  button is a triangle icon (tooltip "Play").
- **Playhead follows the level (0.3.5).** The playhead line, dot and trail dim with the
  layer's effective amplitude at that moment (Depth and Time Scale included): -60 dB .. 0 dB
  maps to 10 % .. full brightness, with the end-of-hit fade on top. On the Body pitch graph it
  follows the Body amp envelope; on the master graph the master envelope (full brightness when
  the master envelope is off).
- **Pitch and amp envelopes on every layer (0.3.6).** Click, Sub, Tail and Noise now have
  the same two multi-point envelopes as the Body (up to 8 points, curve tension, zoom, pan,
  auto-fit, playhead, point readouts), picked with the Pitch / Amp switch above the graph on
  every layer page. Each page remembers its own choice.
  - **Amp** replaces the old Click Length and the Sub / Tail / Noise Attack / Hold / Decay /
    Curve knobs (removed; the points do their job). The defaults are the old shapes (Click:
    100 % to 0 over 12 ms; Sub 1 / 60 / 350 ms; Tail 30 / 0 / 900 ms; Noise 0.5 / 5 / 150 ms).
    The last point is the layer's end: at 0 % the layer stops there, above 0 % it fades out
    over 10 ms (or holds while a gated note is held; the Click never holds).
  - **Pitch** is a semitone offset (-48 .. +48 st) on top of tune and key tracking, flat at
    0 st by default. In Synth mode it moves the oscillator (Click, Sub, Tail). In Sample mode
    it bends the playback rate, scaled by the layer's "Smp pitch env" knob (Click, Tail, Noise;
    100 % by default, so +12 st is exactly an octave). Synth Noise has no pitch, so its pitch
    envelope moves a tone filter instead: below 0 st a low-pass closes from 20 kHz by one
    semitone per semitone, above 0 st a high-pass opens from 20 Hz.
  - All points are automatable host parameters ("Sub Pitch Env 2 Time", "Click Amp Env 3
    Level" ...). Envelope changes are smoothed per block, so moving one under a held note
    does not zipper. The Sub's old "Env amt" knob is now "Body pitch" ("Sub Follow Body Pitch").
  - Random rolls the Click's amp (3 .. 80 ms, sometimes with a held top) and a pitch drop
    (up to +12 st on chirp, knock and samples, up to +4 st on the bright types), mild attack / hold / decay shapes for Sub, Tail and Noise, and flat or gentle
    pitch envelopes for them (within 2 st; the Noise filter within 12 st).
- **Each layer page shows its own envelope (0.3.5; superseded by the 0.3.6 envelopes above).** Click showed its length shape, Sub,
  Tail and Noise their attack / hold / decay (drag the points; the decay diamond set the
  curve), Body its pitch or amp envelope. Previously one graph stayed bound to the Body on
  every page.
- **Pitch / Amp switch (0.3.5; every layer page since 0.3.6).** A sliding switch above the graph picks
  the envelope: the thumb eases over about 180 ms and the graph cross-fades over about 200 ms.
  Click, drag the thumb, or use Space / Left / Right; it changes only the view.
- **Key Track switch per layer (0.3.5).** A vertical "KEY" switch under each layer's Level
  fader (off by default, automatable as "<Layer> Key Track", not touched by Random). On, the
  layer follows the played note relative to the Key, in Synth and Sample mode. Noise in Synth
  mode has no pitch, so its switch is disabled there.
- **Resizable envelope / waveform split (0.3.5).** Drag the bar between the envelope and the
  waveform view sideways; double-click it to go back to 60 / 40. The split is kept as a ratio
  when the window resizes and is saved in the plugin state (not automatable, not an undo step).
- **Console-style layer faders (0.3.5).** The five Level faders use a fader taper: the top
  75 % of travel covers -24 .. +12 dB (0 dB at 78 %), the next 15 % -48 .. -24 dB, and the
  bottom 10 % -48 dB .. off. dB ticks are drawn along the fader. Shift-drag is fine
  adjustment; double-click resets.
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

1. Builds the VST3, the tests and the LoadCheck tool in `~/dooblr-build/dooblr-kick` (override with `BUILD_DIR=...`),
   then runs the tests (`ctest`). The source lives in `~/Dropbox/dooblr_plugins/dooblr-kick`; builds
   stay outside Dropbox so it never syncs build output or adds xattrs to the bundle.
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

`~/dooblr-build/dooblr-kick/Tests/DooblrKickTests` checks the following:

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
- The processor on/off toggles on the tabs (parameters, undo, not selecting the tab, moving
  with a chain reorder, no switches left in the panels).
- The meter tap (frames and spectrum during playback, full-FIFO behaviour), that the reported
  compressor / limiter gain reduction matches the gain the DSP applied, and the EQ /
  Dynamics / Limiter views (handle, threshold, ratio and ceiling drags with undo, rendering,
  audition).

`~/dooblr-build/dooblr-kick/Tools/FrameBench/DooblrKickFrameBench` measures frame times per page while dragging.

Set `DOOBLRKICK_EDITOR_SNAPSHOT=/path/prefix` to also write editor PNG snapshots.

## Notes

The sound is designed in the spirit of PunchBox and Kick 3. It is not a clone, and it makes no
claim to match either one.
