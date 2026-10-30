# dooblr-prophet

An analog-modelled polyphonic synthesizer (VST3 instrument, macOS arm64) covering the
Prophet-5 / 6 / 10 feature set: two CEM3340-style oscillators with Poly-Mod, a 2-pole
high-pass into a switchable 4-pole ladder low-pass, and 1–10 voices with unison and mono.

Original design, code and sounds. "Prophet" describes the instrument family it emulates;
see the disclaimer below.

Version 0.3.0 (milestone 1 plus Random, draggable filter handles, filter env Sync to Amp, Echo and Reverb, and a Max for Live panel). See [ROADMAP.md](ROADMAP.md) for what is next and
[docs/analog-reference.md](docs/analog-reference.md) for the circuit reference the DSP follows.

## Pages and controls

| Page | Controls |
| --- | --- |
| **Oscillators** | Per oscillator: Variable (continuous Shape, P6) or Switched (Saw / Triangle / Pulse, P5) mode, Coarse, Fine, Shape, Pulse Width. Osc 1 Sync (slaved to osc 2). Osc 2 Low Freq and Key Track. Mixer: Osc 1, Osc 2, Sub (one octave below osc 1), Noise. The graph shows both live waveforms. |
| **Filters** | High-pass (2-pole, resonant): Cutoff, Resonance, Env Amount. Low-pass (4-pole ladder): Cutoff, Resonance, Drive, Env Amount, Key Track. Low-pass model: 3320 (P5 Rev3) or 2040 (P5 Rev1/2, P6). The graph shows the combined response, with a draggable handle per filter: drag left/right for Cutoff, up/down for Resonance (Shift for fine, mouse wheel for Resonance, double-click to reset that filter; one drag is one undo step). |
| **Envelopes** | Filter and amp ADSR, each with a velocity amount. Sync to Amp (filter envelope panel, automatable, off by default): the filter envelope follows the amp ADSR while keeping its own velocity, env amounts and Poly-Mod; its own ADSR knobs are dimmed but keep their values, and the graph shows the envelope in effect. |
| **Modulation** | Poly-Mod: Filter Env and Osc 2 amounts to Osc 1 Freq, Osc 1 Shape, Filter (both cutoffs). LFO: Wave (triangle, saw, reverse saw, square, sample & hold), Free Rate or Tempo Sync Division, Initial amount, Wheel amount, Noise Mix, and seven destinations. |
| **Voices** | Poly / Unison / Mono. Voices (1–10, quick 5 / 6 / 10), Unison voices and Detune, Glide (mono), Vintage (per-voice variance). Bend Range, aftertouch to filter and to LFO amount. |
| **FX** | A global stereo stage after the voice mix: Echo, then Reverb. Each has an automatable On switch (off by default), a Preset menu (10 each) and Mix. Echo: Time (20–2000 ms) or tempo Sync with Division (1/32 to 1/1, dotted and triplet), Feedback (up to 95%), and Ping-Pong (automatable; the mono-summed input starts on the left and each repeat crosses sides). Reverb: Size / Decay and Tone. Choosing a preset sets its character and the macros it starts from (one undo step); the macros and Ping-Pong then move freely. The graph shows the echo taps (left above, right below) and the reverb decay. |

Controls that the current mode ignores are dimmed (for example Saw / Triangle / Pulse in
Variable mode, Rate when the LFO is synced). Dimmed controls stay clickable and editable, so
you can set them up before switching mode (0.1.4 fixes 0.1.0–0.1.3, where they ignored clicks).

Header: Undo / Redo, preset menu (factory and user), Random, Save / Load (.dprophet files in
`~/Music/dooblr-prophet/Presets`), Output level and meter.

Random makes a new playable patch in one undo step and shows "Random" as the preset name;
undo also brings back the previous preset name.
It sets the oscillators, mixer (never all off), filters (resonance mostly moderate; a dark
cutoff always gets envelope), envelopes (amp sustain at least 40%), Poly-Mod, LFO, glide and
vintage. It never touches Output, the voice mode and counts, Bend, aftertouch, osc A tuning
or the LFO sync settings.

Shortcuts: Cmd-Z undo, Cmd-Shift-Z or Cmd-Y redo (50 steps; only knob gestures are
recorded, not host automation). Double-click a knob to reset it; Shift-drag for fine moves.

MIDI: notes and velocity, pitch bend, mod wheel (LFO Wheel amount), channel aftertouch,
sustain pedal.

## Analog behaviour (see docs/analog-reference.md)

The model constants are representative values from clone datasheets and the reference,
not measurements of original instruments; each one is commented in
`Source/DSP/AnalogModel.h` with the section of the reference it comes from.

- Oscillators: exponential pitch, high-frequency flattening `f_eff = 1/(1/f + t_r)` with
  t_r = 2 µs (about 17 cents flat at 5 kHz), PolyBLEP saw and pulse, sub-sample hard sync
  with a finite reset, triangle asymmetry and reset glitch, small PWM-to-pitch leak.
- Poly-Mod frequency modulation goes through the exponential input, so it raises the mean
  pitch; the oscillators run 4x oversampled while it is active (2x otherwise).
- Low-pass: zero-delay-feedback 4-stage ladder solved with two Newton iterations; DC gain
  1/(1+k), self-oscillation from k = 4; saturation at the input, in the resonance cell and
  at a biased output stage. The 2040 model adds softer per-stage saturation and a
  different resonance law.
- Vintage: per-voice scale, offset and slow drift, warm-up, filter stage mismatch and
  cutoff offset; small cutoff and resonance CV feedthrough; a thermal noise floor.
- The high-pass runs before the low-pass. The order on the Prophet-6 is unconfirmed (the
  reference notes it); this is a design choice to revisit.

## Build and install

Requirements: macOS on Apple silicon, CMake 3.22+, Xcode command-line tools. JUCE 8.0.15
is fetched by CMake, or set `JUCE_SOURCE_DIR` to a local copy.

```sh
./scripts/build.sh
```

This configures `~/dooblr-build/dooblr-prophet`, builds the VST3, builds and runs the
tests, ad-hoc signs and installs `dooblr-prophet.vst3` into
`/Library/Audio/Plug-Ins/VST3` (when writable; no sudo), and then loads the installed bundle
the way a host does (Tools/LoadCheck). Options are listed at the top of the script.

Editor snapshots: `DOOBLRPROPHET_EDITOR_SNAPSHOT=/tmp/prophet ~/dooblr-build/dooblr-prophet/Tests/DooblrProphetTests`
writes one PNG per page.

## Using it in Ableton Live

1. Preferences → Plug-Ins: turn on "Use VST3 Plug-In System Folders", then Rescan.
2. Browser → Plug-Ins → VST3 → dooblr → dooblr-prophet; drop it on a MIDI track.
3. Open the editor with the wrench icon.

### Max for Live panel (0.3.0)

`m4l/` holds a Max for Live MIDI effect that shows the same pages in Live's device view, two-way
(it follows automation, presets and Random), so the plug-in window can stay closed. Put it
before the VST3 on the same track. It pairs with that plug-in instance through three
channel-16 CCs and then talks OSC over 127.0.0.1. Install with `node m4l/scripts/build.js
--install`. **Untested in Live so far**; see [m4l/README.md](m4l/README.md) for how it works and
its limits.

## Manual validation checklist (not covered by the automated tests)

- Listen through each factory preset at several octaves; check for clicks on note steal
  and on fast release.
- Sweep the low-pass cutoff with full resonance in both models; check the whistle tracks
  smoothly and the level stays sane.
- Poly-Mod Osc 2 → Osc 1 Freq with sync: listen for aliasing at high notes.
- Tempo-synced LFO follows the host tempo; sample & hold steps on time.
- Automation, preset recall and undo in the host; reopen a saved set.
- Resize the window across its range (940 × 600 to 2400 × 1400).

## FX presets

Original voicings, aimed at poly pads and unison / mono leads and basses.

- Echo: Tape Slap, Dotted 8th Lead, Ping-Pong Pad, Analog BBD Echo, Dub Feedback, Mono Bass
  Tight, Wide Stereo 1/4, Ducked Lead, Lo-Fi Wobble, Long Ambient.
- Reverb: Small Plate, Bright Plate, Vintage Spring-ish, Room, Hall, Big Hall, Dark Pad Wash,
  Lead Ambience, Unison Wide, Short Tight.

With both modules off the output is the synth's, bit for bit. Random turns Echo on about 15%
and Reverb about 20% of the time (with a random preset). Factory patches leave both off.

## Tests

`Tests/ProphetTests.cpp` runs on the real processor and editor: silence, bounded output and
release, stealing at 1–10 voices, unison / mono, maximum resonance at four cutoffs in both
models (no NaN, no denormals), 44.1–192 kHz tuning, state round trip, factory and user
presets, every parameter audible, undo, the analog checks (k = 4 self-oscillation at fc,
DC gain 1/(1+k), 5 kHz flattening, Poly-Mod raising the mean pitch), allocation-free
processing and 10-voice CPU, the editor layout at three sizes, filter env Sync to Amp
(sample-exact against the amp envelope, independence when off, state round trip), and a
real-mouse test: at 940 × 600, 1080 × 660 and 2400 × 1400, in Variable and Switched mode,
every showing button, switch, knob and menu is hit-tested at its centre, and every parameter
toggle, mode segment, page button, voice-count button, Random / Undo / Redo and knob is
clicked or dragged through JUCE's mouse dispatch, dimmed or not. FX: bit-identical bypass
(also after use), all 20 presets bounded and decaying at maximum feedback / size, ping-pong
timing (L at T, R at 2T, L at 3T), tempo-synced times at 44.1–192 kHz, preset undo and state,
and CPU with 10 voices plus both modules on. Max for Live link: the OSC codec (with fuzzed
input), the pairing CCs (any order, notes in between, wrong check, other channel, split blocks),
an end-to-end session over loopback UDP against the real processor (sync, edits as undo steps,
host changes echoed, presets, Random, timeout), and editor / device layout parity.

## Licences

- Code: original, by Dan Feinstein.
- JUCE 8 is used under its own licence terms (AGPLv3 or a commercial JUCE licence).
- VST is a trademark of Steinberg Media Technologies GmbH.

## Trademark disclaimer

Prophet-5, Prophet-6 and Prophet-10 are trademarks of their owner (Sequential / Focusrite
group). dooblr-prophet is an independent emulation and is not affiliated with, endorsed by
or derived from their products' code, artwork or sounds. No logos, panel art, samples or
factory patches are used; all presets are original.
