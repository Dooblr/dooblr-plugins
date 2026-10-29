# dooblr-prophet roadmap

The goal is the union of the Prophet-5 (Rev1/2, Rev3, Rev4), Prophet-6 and Prophet-10
feature sets in one original-design instrument. The circuit behaviour follows
[docs/analog-reference.md](docs/analog-reference.md).

## Milestone 1: 0.1.0, playable core (done)

- 1–10 voices with oldest-first stealing (5 / 6 / 10 quick buttons), Unison (1–10 stacked
  voices with detune) and Mono (last-note priority, legato glide).
- Two CEM3340-style oscillators: P6-style continuous shape (triangle → saw → pulse) or
  P5-style switched saw / triangle / pulse, pulse width, hard sync (osc 1 slaved to osc 2,
  as on the P5), osc 2 Low Freq and Key Track off.
- Mixer: osc 1, osc 2, sub-octave (P6), noise.
- 2-pole resonant high-pass (P6) into a 4-pole ladder low-pass with a switchable model:
  3320 (P5 Rev3 / Rev4 "Rev3" mode) and 2040 (Rev1/2, Rev4 "Rev2" mode, P6).
- Filter and amp ADSR with velocity amounts.
- Poly-Mod: filter envelope and osc 2 (bipolar) to osc 1 frequency (exponential FM, 4x
  oversampled), osc 1 shape, and both filter cutoffs.
- LFO: triangle, saw, reverse saw, square, sample & hold; free rate or tempo sync; initial
  amount, mod-wheel amount, noise mix; destinations osc 1 / 2 frequency, osc 1 / 2 shape,
  low-pass, high-pass, amp.
- Vintage: per-voice oscillator scale / offset / drift, warm-up, filter stage mismatch and
  cutoff offset (0 = matched voices).
- Performance: pitch-bend range, channel aftertouch to filter and LFO amount, sustain pedal.
- Eight factory presets plus Init; user presets (.dprophet) with Save / Load.
- Undo / redo (50 gesture steps), state round trip with a state version.

## 0.1.1: one front panel and Random (done)

- Every sound control on one panel in classic order (Poly-Mod, LFO / Wheel-Mod, Osc A,
  Osc B / Mixer, Filter, Filter Env, Amp Env, Voices) instead of five pages. The waveform and
  filter-response graphs of 0.1.0 were dropped to make room.
- Random button: a musically usable random patch as one undo step.

## Milestone 2: layers and split (P10 / P5 Rev4 bi-timbral)

- Two independent layers with layer / split / stack modes, split point and per-layer voice
  allocation (P10 5 + 5, P6 / P5 Rev4 stack and split).
- Per-layer volume and pan; panel edit target A / B.

## Milestone 3: arpeggiator and polyphonic step sequencer (P6 / Rev4)

- Arp: up / down / up-down / random / assign, 1–3 octaves, host sync, latch.
- Poly step sequencer: 64 steps, record from MIDI, rests and ties, host sync.

## Milestone 4: effects (P6 dual digital effects)

- Two effect slots: BBD-style delay, digital delay, chorus, phaser, flanger, room / hall /
  plate / spring reverb, ring mod, distortion; mix and two parameters each.
- Distortion and the P6 "pre-filter" option.

## Milestone 5: remaining panel details

- P6 Slop control mapping refinement against hardware measurements; per-voice tuning
  table display.
- Rev4 velocity / aftertouch destination matrix and unison chord memory.
- Release on/off switch for the envelopes (Rev4), envelope linear / exponential option.
- Poly-Mod to osc 1 pulse width as a separate destination (P6).
- MPE and polyphonic aftertouch routing.

## Analog refinements (pending hardware measurements, see the reference's last section)

- Fit the representative constants (t_r, triangle asymmetry, PWM→pitch leak, resonance
  law, clip points) to measured voices.
- Confirm the high-pass → low-pass order on the P6; it is a choice in this build.
- SSM2040 per-stage saturation is approximated inside the Newton solve.
- The P6 continuous-shape VCO keeps a full-level triangle (the half-level triangle applies
  to the switched P5 mixer).
