# dooblr-bass

A monophonic analog-style bass synthesizer (VST3 instrument). One board, Mini-style
left-to-right signal flow: two main oscillators (saw / square, footage, fine tune on
osc 2), a sub oscillator, a third oscillator that can run in the audio or lo-freq range,
a mixer with drive and overload into a 24 dB/oct transistor-ladder low-pass, filter and
amp ADSR, glide, last/low note priority, and an LFO.

The DSP models the circuitry: ramp-core VCOs with Early-effect bow and reset droop,
exponential CV converters with analog lag, a virtual-earth mixer, a zero-delay transistor
ladder (tanh stages, no bass-loss compensation), and an OTA VCA with residual bleed.

Original design, code and sounds. The architecture is inspired by classic compact bass
synths and three-oscillator monosynths; see the disclaimer below.

Version 0.1.0.

## Board

One page. No page column. Header, a compact graph strip, a mode bar, then the panel in
signal-flow order.

| Section | Controls |
| --- | --- |
| **Osc 1 / 2 / 3** | Osc 1 / 2: Wave (Saw / Square), Octave (32' / 16' / 8' / 4'), pulse Width (square). Osc 2 Tune (±7 st). Osc 3: Triangle / Saw / Square, Octave, Tune. Lo Freq and Key Track sit on the mode bar. The graph shows one cycle of each oscillator. |
| **Mixer** | Osc 1, Osc 2, Osc 3, Sub (with Sub Oct −1 / −2), Noise, Drive into the filter, Overload (filter output fed back into the mixer summer). |
| **Low-pass** | 24 dB/oct transistor-ladder: Cutoff, Resonance, Env Amount (bipolar), Key Track. Drag the handle: left/right for Cutoff, up/down for Resonance (Shift for fine, mouse wheel for Resonance, double-click to reset). The graph is the linear ladder response, including the bass loss at high resonance. |
| **Controllers** | Glide, Bend Range. Legato is on the mode bar with Last / Low note priority. |
| **Filter Env / Amp Env** | Filter ADSR plus Velocity; Amp ADSR. Mode: ADSR, or Shared Decay (release follows decay on both envelopes). The graph shows both curves. |
| **LFO** | Wave (triangle, saw, reverse saw, square, sample & hold), Rate or tempo Sync / Division, amounts to Pitch and Filter, Wheel (extra pitch vibrato from the mod wheel). The graph is one cycle, with a live marker. |

Controls that the current mode ignores are dimmed (for example pulse Width on a saw,
Rate when the LFO is synced, Release when Shared Decay is on). Dimmed controls stay
clickable and editable.

Header: Undo / Redo, preset menu (factory and user), Random, Save / Load (`.dbass` files in
`~/Music/dooblr-bass/Presets`), Output level and meter.

Random makes a new playable bass patch in one undo step and shows "Random" as the preset
name. It never touches Output, Bend Range, Note Priority or the LFO sync settings.

Shortcuts: Cmd-Z undo, Cmd-Shift-Z or Cmd-Y redo (50 steps; only knob gestures are
recorded, not host automation). Double-click a knob to reset it; Shift-drag for fine moves.

MIDI: notes and velocity, pitch bend, mod wheel (LFO Wheel amount), sustain pedal.

## Sound

- Oscillators: PolyBLEP saw and square, polyBLAMP triangle. Analog ramp core: Early-effect
  bow on the charging transistor, finite reset `f_eff = 1 / (1/f + 2 µs)`, expo-converter
  cubic warp, and a short RC on the pitch CV after glide.
- Mixer: virtual-earth inverting summer. Drive is more current into the node; Overload is
  the filter output into the same node.
- Filter: zero-delay-feedback 4-stage transistor ladder (tanh pair per stage, Newton on the
  resonance cell, biased output pair). Self-oscillation from k = 4. DC gain 1/(1+k) — no
  bass-loss compensation. Cutoff CV has analog expo lag. Always 2× oversampled, with a
  half-band decimator.
- VCA: OTA-style pair with control-current lag and a little bleed when closed.
- Envelopes: analog RC attack (slight overshoot) and exponential decay / release.
- One voice, last- or low-note priority, legato (no envelope retrigger) or retrigger, glide.

Factory patches: Init, Rubber Sub, Disco Envelope, Dark Square, Growl Fifth, Mini Pluck,
Acid Slide, Sub Drone, Filter Wobble. All original.

## Build and install

Requirements: CMake 3.22+, a C++20 compiler. On macOS, Xcode command-line tools. JUCE 8.0.15
is fetched by CMake, or set `JUCE_SOURCE_DIR` to a local copy.

```sh
./scripts/build.sh
```

This configures `~/dooblr-build/dooblr-bass`, builds the VST3, builds and runs the
tests, ad-hoc signs and installs `dooblr-bass.vst3` into
`/Library/Audio/Plug-Ins/VST3` (when writable; no sudo), and then loads the installed bundle
the way a host does (Tools/LoadCheck). Options are listed at the top of the script.

Editor snapshots: `DOOBLRBASS_EDITOR_SNAPSHOT=/tmp/bass ~/dooblr-build/dooblr-bass/Tests/DooblrBassTests`
writes a PNG of the board.

## Using it in Ableton Live

1. Preferences → Plug-Ins: turn on "Use VST3 Plug-In System Folders", then Rescan.
2. Browser → Plug-Ins → VST3 → dooblr → dooblr-bass; drop it on a MIDI track.
3. Open the editor with the wrench icon.

## Manual validation checklist (not covered by the automated tests)

- Listen through each factory preset in the bass and tenor octaves; check for clicks on
  legato vs retrigger and on fast release.
- Sweep cutoff with full resonance; the whistle should track smoothly and the level stay sane.
- Overload and Drive: the filter should grind without blowing up.
- Tempo-synced LFO follows the host tempo; sample & hold steps on time.
- Automation, preset recall and undo in the host; reopen a saved set.
- Resize the window across its range (940 × 600 to 2400 × 1400). Everything stays on one page.

## Tests

`Tests/BassTests.cpp` runs on the real processor and editor: silence, bounded output and
release, maximum resonance at four cutoffs and 44.1–192 kHz (no NaN, no denormals),
self-oscillation at k = 4, DC gain 1/(1+k), analog model (expo converter, mixer summer),
pitch tracking, state round trip, factory and user presets, every parameter audible, undo,
last/low note priority and legato, Shared Decay, oscillator aliasing sanity, allocation-free
processing, a random patch, and the one-page editor at three sizes with a filter-handle check.

## Licences

- Code: original, by Dan Feinstein.
- JUCE 8 is used under its own licence terms (AGPLv3 or a commercial JUCE licence).
- VST is a trademark of Steinberg Media Technologies GmbH.

## Trademark disclaimer

dooblr-bass is an original instrument. Names of other synthesizers appear only as
historical references in this README. The plug-in is not affiliated with, endorsed by
or derived from those products' code, artwork or sounds. No logos, panel art, samples or
factory patches from other instruments are used; all presets are original.
