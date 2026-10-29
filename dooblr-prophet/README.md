# dooblr-prophet

An analog-modelled polyphonic synthesizer (VST3 instrument, macOS arm64) covering the
Prophet-5 / 6 / 10 feature set: two CEM3340-style oscillators with Poly-Mod, a 2-pole
high-pass into a switchable 4-pole ladder low-pass, and 1–10 voices with unison and mono.

Original design, code and sounds. "Prophet" describes the instrument family it emulates;
see the disclaimer below.

Version 0.1.1 (milestone 1 plus the single front panel and Random). See [ROADMAP.md](ROADMAP.md) for what is next and
[docs/analog-reference.md](docs/analog-reference.md) for the circuit reference the DSP follows.

## The front panel

Since 0.1.1 every sound control is on one panel, laid out left to right in the classic
five-voice polysynth order (original dooblr design-system styling, no copied artwork):

| Section | Controls |
| --- | --- |
| **Poly-Mod** | Filter Env and Osc B amounts; destinations Freq A (exponential FM), Shape A, Filter (both cutoffs). |
| **LFO / Wheel-Mod** | Free / Sync switch, Wave (triangle, saw, reverse saw, square, sample & hold), Sync Division, Rate, Initial amount, Wheel amount, Noise Mix; seven destinations (Freq A / B, Shape A / B, Low-Pass, High-Pass, Amp). |
| **Oscillator A** | Variable (continuous Shape, P6) / Switched (Saw / Tri / Pulse, P5) switch, Freq, Fine, Shape, PW, Sync (slaved to osc B). |
| **Oscillator B** | Variable / Switched switch, Freq, Fine, Shape, PW, Saw / Tri / Pulse, Lo Freq, Kbd (key track). |
| **Mixer** | Osc A, Osc B, Sub (one octave below osc A), Noise. |
| **Filter** | 3320 (P5 Rev3) / 2040 (P5 Rev1/2, P6) model switch; low-pass Cutoff, Resonance, Env Amt, Kbd, Drive; high-pass (2-pole, resonant) Cutoff, Reso, Env. |
| **Filter Envelope**, **Amp Envelope** | Attack, Decay, Sustain, Release, Velocity amount. |
| **Voices** | Poly / Unison / Mono switch, Voices (1–10, quick 5 / 6 / 10), Unison voices, Detune, Glide, Vintage (per-voice variance), Bend range, aftertouch to filter and to LFO amount; voice and oversampling status. |

Controls that the current mode ignores are dimmed (for example Saw / Triangle / Pulse in
Variable mode, Rate when the LFO is synced).

Header: Undo / Redo, preset menu (factory and user), Random, Save / Load (.dprophet files in
`~/Music/dooblr-prophet/Presets`), Output level and meter.

Random makes a new playable patch in one undo step and shows "Random" as the preset name.
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
writes the whole panel at 940 x 600, 1080 x 660 and 2400 x 1400.

## Using it in Ableton Live

1. Preferences → Plug-Ins: turn on "Use VST3 Plug-In System Folders", then Rescan.
2. Browser → Plug-Ins → VST3 → dooblr → dooblr-prophet; drop it on a MIDI track.
3. Open the editor with the wrench icon.

## Manual validation checklist (not covered by the automated tests)

- Listen through each factory preset at several octaves; check for clicks on note steal
  and on fast release.
- Sweep the low-pass cutoff with full resonance in both models; check the whistle tracks
  smoothly and the level stays sane.
- Poly-Mod Osc 2 → Osc 1 Freq with sync: listen for aliasing at high notes.
- Tempo-synced LFO follows the host tempo; sample & hold steps on time.
- Automation, preset recall and undo in the host; reopen a saved set.
- Resize the window across its range (940 × 600 to 2400 × 1400).

## Tests

`Tests/ProphetTests.cpp` runs on the real processor and editor: silence, bounded output and
release, stealing at 1–10 voices, unison / mono, maximum resonance at four cutoffs in both
models (no NaN, no denormals), 44.1–192 kHz tuning, state round trip, factory and user
presets, every parameter audible, undo, the analog checks (k = 4 self-oscillation at fc,
DC gain 1/(1+k), 5 kHz flattening, Poly-Mod raising the mean pitch), allocation-free
processing and 10-voice CPU, and the editor layout at three sizes.

## Licences

- Code: original, by Dan Feinstein.
- JUCE 8 is used under its own licence terms (AGPLv3 or a commercial JUCE licence).
- VST is a trademark of Steinberg Media Technologies GmbH.

## Trademark disclaimer

Prophet-5, Prophet-6 and Prophet-10 are trademarks of their owner (Sequential / Focusrite
group). dooblr-prophet is an independent emulation and is not affiliated with, endorsed by
or derived from their products' code, artwork or sounds. No logos, panel art, samples or
factory patches are used; all presets are original.
