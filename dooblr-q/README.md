# dooblr-q

A dynamic parametric EQ plug-in (VST3) built with [JUCE](https://juce.com) and CMake.

**Version 0.9.2:** six-band dynamic EQ with a **hover Range ring** and blue **S** (Solo) on every graph handle, per-band **Solo**, dynamic Low / High Cut, an outer **Range ring** around each band's main knob, per-band analogue-style **Models** (Digital, Neve 1073/1081, SSL 4000 E and G, API 550A/B, Pultec EQP-1A, Harrison 32C, Trident A-Range, Helios, EMI TG) with Drive, Stepped switch positions and Pultec Atten, per-band slopes (12–60 dB/oct and Brickwall) for every band type, Tilt bands, Width (bandwidth in octaves), three
phase modes (Zero Latency / Natural Phase / Linear Phase), a native JUCE editor with a
live frequency-response display over a smoothed spectrum analyzer with a continuously
updating Freeze overlay, 50-step undo/redo, on/off switches in the band tabs, and
automated tests. (0.3.0 added the analyzer, Freeze and Tilt. 0.4.1 removed 0.4.0's per-band
stereo width: "Width" now means the band's bandwidth. 0.5.0 added undo/redo, the band-tab
switches, the live Freeze overlay and display smoothing. 0.5.1 ties averaging to Freeze: it
starts fresh when Freeze is turned on and stops and clears when it is turned off. 0.6.0
makes band 6 a High Cut (low-pass) by default, alongside band 1's Low Cut (high-pass), and
gives every band type 2D handle dragging: horizontal = frequency, vertical = gain or, for
cuts and notches, Q. 0.6.1 colours dynamic activity per band, only over the range the
moving band affects, instead of recolouring the whole curve. 0.7.0 adds a per-band
**Slope**: 12, 24, 36, 48, 60 dB/oct or Brickwall for cuts, shelves and tilt. 0.7.1 extends
Slope to Bell and Notch, so it applies to every band type. 0.8.0 adds the per-band Model,
Stepped, Drive and Atten controls; see [Models](#models-080). 0.9.0 adds dynamics to Low /
High Cut and the Range ring; see [Dynamic cuts and the Range ring](#dynamic-cuts-and-the-range-ring-090). 0.9.1 adds a Solo (S) button to every band; see [Band Solo](#band-solo-091). 0.9.2 adds the hover ring and blue S on the graph; see [Hover ring and blue S](#hover-ring-and-blue-s-092).)

The version shown in the editor comes from `project(dooblr-q VERSION ...)` in
`CMakeLists.txt` (via `JucePlugin_VersionString`); a test checks they match.

## Layout

```
CMakeLists.txt                 Project, pinned JUCE dependency, plug-in target (DooblrQ)
Source/
  DSP/SvfFilter.h/.cpp         TPT state-variable filter, coefficients, analytic response
  DSP/DynamicEQBand.h/.cpp     One band: smoothing, side-chain detector, envelope, dynamic gain
  DSP/FirEqEngine.h/.cpp       Natural/Linear phase: kernel design + partitioned FFT convolution
  Analysis/SpectrumAnalyzer.*  Lock-free analyzer FIFO, FFT analyzer, long-term average, freeze (no GUI)
  Analysis/SpectrumDisplay.*   Display smoothing: 1/12-octave + per-point rise/fall (no GUI)
  UndoHistory.h/.cpp           Undo/redo of UI gestures on juce::UndoManager (50 steps)
  Parameters.h/.cpp            Parameter IDs, ranges, defaults (APVTS layout)
  PluginProcessor.h/.cpp       Buses, processBlock, bypass, state
  PluginEditor.h/.cpp          Editor layout and parameter attachments
  UI/Theme.h/.cpp              Colours and look-and-feel
  UI/ResponseCurveComponent.*  Frequency-response display with draggable band handles
Tests/
  CMakeLists.txt               Test executable + CTest registration
  PluginSmokeTests.cpp         Console tests against the real processor/editor
scripts/build.sh               Configure, build, test, copy bundle, report paths
```

The CMake target is `DooblrQ` (CMake target names are kept hyphen-free); the
product, bundle and host-visible plug-in name is **dooblr-q**.

## Features

Six bands (`params::numBands` in `Source/Parameters.h`), each with:

| Parameter | Range | Default |
|---|---|---|
| Enabled | on/off | off |
| Type | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Tilt | per band (below) |
| Frequency | 20 Hz – 20 kHz, logarithmic | per band (below) |
| Gain (static) | ±24 dB | 0 dB |
| Q, shown as **Width** | Q 0.1 – 18 (logarithmic) = 6.67 – 0.08 octaves | Q 0.707 (shelves/cut), 1.0 (bells) |
| Dynamic | on/off | off |
| Range | ±24 dB (negative = cut when loud, positive = boost when loud, 0 = static) | 0 dB |
| Threshold | −60 – 0 dB | −24 dB |
| Attack | 0.1 – 200 ms | 10 ms |
| Release | 5 – 2000 ms | 120 ms |
| Slope | 12, 24, 36, 48, 60 dB/oct, Brickwall (every band type; Digital model only) | 12 dB/oct |
| Model | Digital, Neve 1073/1081, SSL 4000 E, SSL 4000 G, API 550A/B, Pultec EQP-1A, Harrison 32C, Trident A-Range, Helios, EMI TG | Digital |
| Stepped | on/off: snap Frequency (and API / Helios-treble Gain) to the model's switch positions | off |
| Drive | −12 – +24 dB into the model's colour stage (analogue models only) | 0 dB |
| Atten | 0 – 18 dB, Pultec EQP-1A Low/High Shelf only | 0 dB |

Band defaults: 1 Low Cut 30 Hz, 2 Low Shelf 120 Hz, 3 Bell 400 Hz,
4 Bell 1.5 kHz, 5 Bell 4 kHz, 6 High Cut 18 kHz (band 1 is a high-pass and band 6 a
low-pass, both Q 0.707 Butterworth). All bands start disabled, so a fresh instance is an
exact pass-through; dragging a handle switches its band on. Saved sessions keep the band
types they were saved with (each type is stored explicitly).

Global: **Output Gain** (±24 dB), **Stereo Link** (on by default: one
detector drives both channels; off: each channel gets its own dynamic gain) and
**Phase Mode** (Zero Latency by default; see below).

**Width** is the band's Q shown as a bandwidth in octaves, BW = 2/ln2 · asinh(1/(2Q))
(Q 1.414 = 1 octave, Q 0.667 = 2 octaves). The editor knob works in octaves (clockwise =
wider) with a readout like `1.00 oct (Q 1.41)`, but the stored parameter is still Q
(`bN_q`), so sessions are unaffected. For a bell, the Width edges centre ÷ 2^(BW/2) and
centre × 2^(BW/2) are where the band is at half its dB gain (so for a −6 dB bell, its
−3 dB points). For shelves, cuts and tilt the same Q controls the transition.

Phase Mode was added in 0.4.0 after every older parameter (old order and IDs unchanged);
sessions saved before it load at Zero Latency. 0.4.0 also had a per-band stereo
`bN_width` parameter; it was removed in 0.4.1, and 0.4.0 sessions containing it load
normally (the unknown parameter is ignored and not saved again).

The six Slope parameters (`b1_slope` … `b6_slope`, added in 0.7.0) come after every older
parameter, so no older ID or index moved. Sessions saved before 0.7.0 have no slope and
load at 12 dB/oct, which is bit-identical to the old filters; `stateVersion` stays 1.
Bells and notches saved by 0.7.0 at a slope other than 12 dB/oct (where it had no effect)
now sound at that slope; 0.7.0 always saved 12 dB/oct unless the slope was changed while the
band was a cut, shelf or tilt.

### Slope (0.7.0, Bell and Notch since 0.7.1)

| Type | What Slope does |
|---|---|
| Low Cut / High Cut | A cascade of Butterworth sections: 12 dB/oct = 2nd order (1 section) … 60 dB/oct = 10th order (5 sections). The −3 dB corner stays at the set frequency for every order. Width/Q adds resonance: the corner level is 20·log10(Q) at every slope (−3 dB at Q 0.707), so the handle mapping is unchanged. |
| Brickwall | 32nd-order Butterworth (16 sections), about 192 dB/oct asymptotically: −3 dB at the corner, ≈ −29 dB 10 % past it, below −110 dB (measured, float noise floor) one octave past it. |
| Low / High Shelf, Tilt | Higher-order Butterworth shelving (order 2–10, 32 for Brickwall): the transition gets steeper (max ≈ 9, 19, 28, 38, 47, 149 dB/oct for an 18 dB shelf), the midpoint stays at half the gain and the plateaus are exact. Q still shapes the transition. |
| Bell | 12 dB/oct is the old bell (bit-identical). Higher slopes are a Butterworth band-shelf of order 2n (n = 2, 3, 4, 5 sections, 16 for Brickwall): an order-n Butterworth low shelf mapped low-pass→band-pass. The centre is exactly the Gain, the Width edges (centre·2^±Width/2) are exactly half the Gain in dB, and the skirts get steeper and the top flatter. Measured (±12 dB, 1 octave, analogue = Natural/Linear): max skirt 15.3 / 23.4 / 33.6 / 44.1 / 54.7 / 172.6 dB/oct; 90 %→10 % of the gain 7.3 / 15.8 / 24.2 / 32.4 / 40.7 / 130.4 dB/oct; within 10 % of the gain over 0.30 / 0.55 / 0.67 / 0.74 / 0.79 / 0.93 octave (Brickwall ≈ rectangular). Half an octave beyond an edge: +2.5 / +0.17 (36) / +0.01 (60) / 0.00 dB. |
| Notch | 12 dB/oct is the old notch (bit-identical). Higher slopes are a Butterworth band-stop of order 2n (order-n Butterworth low-pass mapped low-pass→band-stop): the null stays at the frequency, −3 dB stays at the Width edges, and the edges get steeper so the rejection band gets wider and flatter. Measured (1 octave): −3→−20 dB at 37.9 / 50.2 / 64.4 / 79.1 / 94.1 / 262.2 dB/oct; below −20 dB over 0.10 / 0.32 / 0.47 / 0.57 / 0.64 / 0.87 octave. |

- Click-safe: changing a band's slope or type while audio runs crossfades the old and new
  filter cascades over 10 ms (type changes were abrupt before 0.7.0).
- Dynamics work at every slope (the band's whole cascade follows static + dynamic gain).
- In Natural/Linear Phase the kernel is built from the same analogue cascade. Measured with a
  1 kHz Brickwall low cut: ≈ −139 dB (Natural) / −138 dB (Linear) at 500 Hz. The kernel
  length limits how steep a very low corner can be (5.9 / 11.7 Hz bins), and the long
  impulse response of a 32nd-order filter rings (a steep corner always does; in Linear Phase
  the ringing is pre- and post-).

## Hover ring and blue S (0.9.2)

On the graph, moving the pointer within about **40 px** of an enabled band's handle fades in
(100 ms, eased out; 150 ms to fade out) a ring around the **nearest** handle. No new
parameters: it edits the band's existing Range (`bN_dyn_range`).

- **Counter-clockwise = downward compression** (orange arc): a louder signal turns a bell /
  shelf / tilt down, or closes a Low / High Cut (moves its cutoff into the passband).
  **Clockwise = upward expansion** (green arc): a louder signal turns the band up, or opens a
  cut. 0 is at 12 o'clock; ±135° = ±24 dB. In Range terms: gain types CCW = negative Range,
  CW = positive; cuts CCW = positive Range, CW = negative.
- Drag **around** the ring or **up/down** (up = upward, 0.2 dB per pixel), with the panel
  ring's direction lock (the first ~4 px choose); **Shift** = fine, **double-click** = 0 dB.
  The amount ("Downward 6.0 dB" / "Upward 6.0 dB") shows only while dragging; a dot marks
  the live dynamic amount.
- Moving it off 0 **switches the band's Dynamic on**, in the same undo step as the Range
  change. Going back to 0 leaves Dynamic on.
- Only the ring's annulus (about 15–34 px from the handle) takes clicks, and every handle
  (any band's) wins over a ring, so dragging handles works as before. Notch has no ring.
- A **blue S** (19 px) on the ring's top-right edge toggles the band's Solo, the same as the
  band's S buttons (**Alt/Option-click** = exclusive). A soloed band's S stays visible, filled
  blue, without hovering; it is the graph's only Solo indicator.

## Band Solo (0.9.1)

Every band has an **S** button: on its band tab (top right) and next to the band's on/off
switch in the panel. While any band is soloed you hear only the region each soloed band
works on, at its EQ'd level (the region filters have unity gain at their centre / in their
passband):

- **Bell, Notch:** a band-pass at the band's frequency with the band's Q / Width (-3 dB at the
  Width edges, more than 30 dB down a decade away).
- **Low Shelf / High Shelf:** the shelved side (4th-order low-pass / high-pass at the shelf).
- **Tilt:** the side it raises (high side for positive gain, low side for negative).
- **Low / High Cut:** a band-pass around the cutoff; with dynamics on it follows the live
  (dynamic) cutoff. Dynamic bells and shelves keep their region while their gain moves.
- **Several solos sum.** Alt/Option-click a band's S to solo that band only (alt-click it again
  to un-solo); that change is one undo step.
- A **disabled band cannot be soloed:** its S is greyed out and a stored Solo on it is ignored
  until the band is switched on.

Solo runs after the EQ (all phase modes, all Models) with no added latency, a 20 ms fade on
every on / off / switch, and a 10 ms glide when a soloed band's frequency or Q moves. With no
band soloed it is an exact pass-through. The display dims everything, lights the soloed
regions in the band colour (with a dashed outline), fades the other bands' handles and shows
a filled blue S on soloed handles (since 0.9.2; it was a small amber badge in 0.9.1). Parameters "Band N Solo" (IDs `b1_solo` … `b6_solo`) are appended
after all older parameters, so existing sessions load unchanged.

## Dynamic cuts and the Range ring (0.9.0)

**No new parameters.** The ring and the cut dynamics use the existing per-band
`bN_dyn_on`, `bN_dyn_range`, `bN_dyn_threshold`, `bN_dyn_attack`, `bN_dyn_release`;
parameter IDs, order and `stateVersion` are unchanged, and sessions load as before.

### Range ring

- A concentric outer ring around the band's **Gain** knob (bells, shelves, tilt) or, for
  **Low / High Cut**, around its **Frequency** knob. The knob it surrounds is drawn a little
  smaller to make room; other knobs are unchanged.
- It is the same parameter as the **Range** knob (Dynamic section), so both always show the
  same value (two-way, through the parameter).
- Its arc, in the band colour, runs from the knob's current value to where the dynamics can
  take it (gain + Range, or the moved cutoff for cuts), with a tick at the limit, a mark at
  0 dB Range and a white dot for the live position while the band is moving.
- Only the ring (an annulus about 14 px wide) takes clicks; the knob inside works as before.
- Drag **up/down** (0.2 dB per pixel, 240 px = full ±24 dB span) or **around the ring**
  (the knob's full sweep = 48 dB). Since 0.9.2 the direction is the same for every type:
  **up / clockwise = upward expansion, down / counter-clockwise = downward compression**
  (for gain types that is +/- Range; for cuts it is -/+ Range, since positive Range closes a
  cut). The first ~4 px of a drag pick the mode:
  mostly vertical or radial movement = up/down, movement along the ring = rotation.
  **Shift** = fine (×0.1), **double-click** = 0 dB, hover highlight, tooltip. Each drag (or
  reset) is one undo step.
- Dimmed and disabled when the band's Dynamic switch is off or the type has no dynamics (Notch).

### Dynamic Low / High Cut

- Range moves the **cutoff** instead of a gain: **1 dB of dynamic amount = 1/6 octave**
  (±24 dB = ±4 octaves), clamped to 20 Hz – 20 kHz.
- **Positive Range: more signal moves the cutoff into the passband**, closing the filter
  (High-pass/Low Cut moves **up**, Low-pass/High Cut moves **down**).
  **Negative Range: more signal opens the filter** (Low Cut moves down, High Cut moves up).
- Detector: as for bells, a band-pass on the band's input at the **static** cutoff (Q from the
  band), so it measures the level around the corner; then threshold, 6 dB knee, attack/release
  as for the other types.
- Works with every slope (12–60 dB/oct, Brickwall), every phase mode (in Natural / Linear
  Phase the kernel is rebuilt from the moved cutoff) and every model (analogue models keep
  their own cut shapes at the moved frequency). The shift is slewed (≈1.5 ms); in Zero Latency the cascade is
  redesigned every 16 samples, in Natural / Linear the kernel crossfades as before, so
  moving cutoffs don't zipper or click (tested).
- Display: dashed line = static cutoff, faint line + thin arrow = the full-Range limit, solid
  line + bold arrow = the live cutoff while it moves; the dynamic colouring covers the
  frequencies between the static and moved curves.

## Models (0.8.0)

Every band has a **Model**. **Digital** (the default) is exactly the 0.7.1 filter: a test
checks Digital output is bit-identical to 0.7.1 in all three phase modes (against output
hashes recorded from the 0.7.1 build), and sessions saved
before 0.8.0 load as Digital, so they sound exactly as before. The four new parameters per
band (`bN_model`, `bN_stepped`, `bN_drive`, `bN_atten`) come after every older parameter
(no older ID or index moved); `stateVersion` stays 1.

**These are original behavioural models built from publicly documented characteristics of
the units (published switch positions, how their Q and shelves are described to behave,
their topology: transformers, discrete op-amps, passive networks, tube make-up). They are
not circuit simulations and have not been compared against or matched to hardware; no claim
of hardware-accurate matching is made.** Neve, SSL, Solid State Logic, API, Pultec,
Harrison, Trident, Helios, EMI and the model numbers are trademarks of their respective
owners, used here only descriptively to name the style of behaviour; dooblr-q is not
affiliated with or endorsed by any of them, and uses none of their artwork or logos.

Each analogue model has two parts:

1. **Linear shaping**: the model's own filter shapes for every band type (its Q-versus-gain
   law for bells, its shelf curves, its cut orders). Slope does not apply (the model sets its
   own orders), so the Slope selector is disabled with a tooltip saying so. Notch and Tilt use
   the Digital 12 dB/oct shapes.
2. **Colour**: a level-dependent saturation stage after the band (transformer / amplifier
   style: soft odd-order clipping, a model-specific amount of even harmonics, and more
   low-frequency saturation for transformer models). It is near-clean at low level and grows
   with level; **Drive** pushes the band into it (0 dB = nominal). Digital has no colour
   stage, and Drive is disabled for it.

| Model | Bell (Q vs gain) | Shelves | Cuts | Stepped positions | Colour |
|---|---|---|---|---|---|
| Neve 1073/1081 | broad, inductor-style: Q × 0.55, narrowing a little with gain | resonant (bump next to the shelf) | low 18 dB/oct, high 12 dB/oct, soft corner (Q 0.6) | HPF 50/80/160/300 Hz, low 35/60/110/220 Hz, mid 0.36–7.2 kHz (6), high fixed 12 kHz | transformer: strongest LF saturation, some even harmonics |
| SSL 4000 E | constant Q (same width at every gain) | gentle | low 18 dB/oct, Butterworth | continuous | cleanest, mostly odd |
| SSL 4000 G | proportional: wide at low gain, narrower as gain rises | slight overshoot | low 18 dB/oct, Butterworth | continuous | cleanest, mostly odd |
| API 550A/B | proportional Q (strongly narrows with gain), the same for boost and cut | steepen with gain | 12 dB/oct | the 550's frequency positions per band, HPF 50 Hz, LPF 15 kHz; Gain in steps of −12 … +12 dB | discrete op-amp style, moderate |
| Pultec EQP-1A | very broad (Q × 0.5); cuts 1.6 × narrower | low: boost shelf and separate **Atten** cut at the same frequency — together a shelf with a dip above it; high: broad bell boost, Atten = 1st-order high-shelf cut | 12 dB/oct, Q 0.6 | low 20/30/60/100 Hz, high boost 3–16 kHz (7), atten 5/10/20 kHz | tube make-up: most even harmonics |
| Harrison 32C | proportional, moderate | smooth | 12 dB/oct | continuous | clean, light |
| Trident A-Range | broad, constant (Q × 0.7) | 1st-order high shelf | low 18 dB/oct, Butterworth | low 50/80/100/150 Hz, mid 250 Hz–9 kHz (8), high 8/10/12/15 kHz, HPF 25/50/100 Hz, LPF 9/12/15 kHz | moderate, some even |
| Helios | boost Q × 0.8, cut Q × 1.4 (cuts narrower) | peaky bass boost, 1st-order bass cut | 12 dB/oct, Q 0.6 | bass 50–300 Hz (5), mid 0.7–6 kHz (8), treble fixed 10 kHz with Gain steps −16 … +12 dB | strongest overall |
| EMI TG | broad (Q × 0.6) | 1st-order low shelf; the treble boost is a broad bell an octave below the set frequency | 12 dB/oct, Q 0.6 | low fixed 50 Hz, high fixed 10 kHz | moderate, some even |

Measured (tests, 48 kHz):

- SSL bell width at +3 / +15 dB: E 1.39 / 1.39 octaves (constant), G 2.35 / 1.22 octaves.
- API bell width at 2 / 4 / 8 / 12 dB: 2.93 / 2.41 / 1.77 / 1.39 octaves, identical for cuts.
- Pultec low, 60 Hz, Boost 10 + Atten 10: +3.9 dB at 20 Hz, +4.3 dB at 30 Hz, a −1.3 dB dip
  at ≈ 115 Hz; Boost alone has no dip. Measured with a quiet sine in every phase mode it is
  within 0.05 dB of the model curve.
- THD+N of a 100 Hz sine through a flat band, Zero Latency, at −40 / −20 / −6 / 0 dBFS:

  | Model | −40 dBFS | −20 dBFS | −6 dBFS | 0 dBFS |
  |---|---|---|---|---|
  | Neve 1073/1081 | 0.017 % | 0.17 % | 0.93 % | 2.36 % |
  | SSL 4000 E | 0.001 % | 0.010 % | 0.083 % | 0.29 % |
  | SSL 4000 G | 0.001 % | 0.008 % | 0.063 % | 0.21 % |
  | API 550A/B | 0.005 % | 0.054 % | 0.34 % | 1.00 % |
  | Pultec EQP-1A | 0.025 % | 0.25 % | 1.27 % | 2.79 % |
  | Harrison 32C | 0.002 % | 0.023 % | 0.15 % | 0.45 % |
  | Trident A-Range | 0.011 % | 0.12 % | 0.66 % | 1.70 % |
  | Helios | 0.026 % | 0.26 % | 1.40 % | 3.38 % |
  | EMI TG | 0.017 % | 0.17 % | 0.87 % | 1.97 % |

  Digital at 0 dBFS with Drive +24: 0.000002 %. Neve at −20 dBFS with Drive −12 / 0 / +12 dB:
  0.04 / 0.17 / 0.71 %. 2nd harmonic at −3 dBFS: Pultec −39.3 dBc, SSL E −64.0 dBc.

**Stepped** snaps Frequency (and Gain where the unit's gain is switched: API, Helios treble)
to the positions in the table, in the audio, the curve and the handles; a model/type with a
single position fixes Frequency (the knob is disabled with a tooltip). Stepped is disabled
for SSL, Harrison and Digital, which have continuous controls.

**Atten** is enabled only for Pultec EQP-1A Low and High Shelf bands; otherwise it is
disabled with a tooltip.

**Phase and latency.** Models change no latency in any mode (Zero Latency 0, Natural 1024,
Linear 4352 samples at 48 kHz, as before). In Zero Latency the model's filters are
minimum-phase IIRs like Digital. In Natural / Linear Phase the model's magnitude response is
built into the FIR kernel (so Linear Phase stays linear-phase), and the colour stage runs per
band on the FIR output. The colour stage oversamples 4 × with polyphase IIR filters, but
only the distortion residual (output minus input) goes through them: the dry signal has no
added delay, and the small harmonic residual lags it by ≈ 4.4 samples at 48 kHz (inaudible,
but it means the colour is not strictly linear-phase in Linear Phase mode).

Dynamics work with every model (the model's whole response follows static + dynamic gain;
the colour stage follows the band's level). Changing Model while audio runs crossfades the
old and new filters over 10 ms (as for type and slope changes) and glides the colour
character over 20 ms: the largest sample-to-sample step
during a switch is within 1 % of the steady-state step (click-free). Model, Stepped, Drive and
Atten are in the band's right-click menu (Model and Stepped) and the controls panel, and
take part in undo / redo.

### DSP

- Each band is a topology-preserving-transform state-variable filter (Simper
  / Cytomic SVF). It is stable for any coefficients and handles modulation
  without blowing up, which matters when the gain moves with the signal.
- Coefficients are recomputed every 16 samples from smoothed parameters
  (30 ms smoothing; frequency and Q in the log domain). Enabling/disabling a
  band crossfades over 10 ms.
- Dynamics (Bell, Low Shelf, High Shelf, Tilt and, since 0.9.0, Low / High Cut, which move their cutoff instead; see above): the band's input feeds a
  side-chain detector filter (band-pass at the band's frequency/Q for bells,
  low-pass/high-pass at the corner for shelves, and the unfiltered (broadband) input for
  Tilt, so a dynamic tilt follows the overall level), then a peak envelope follower
  with the attack/release times. The dynamic gain is
  `sign(range) × min(|range|, 0.5 × dB over threshold)`, a 2:1 ratio-style law
  with a 6 dB soft knee. The band then uses static gain + dynamic gain (total clamped to ±30 dB).
- Low/High Cut are 12 dB/octave (one SVF) by default, up to 60 dB/oct or Brickwall with
  Slope (a cascade of SVF sections, see Slope above), with Q as resonance.
- **Tilt** pivots around the band frequency. Gain is the total tilt: +6 dB means
  −3 dB in the lows, 0 dB at the pivot and +3 dB in the highs (positive = brighter).
  It is one SVF: a high shelf of the full gain scaled by −gain/2, so it is as
  modulation-safe as the other bands and dynamics work on it. Q sets the steepness of
  the transition like a shelf's Q: 0.707 is the steepest without overshoot, lower is
  gentler, higher adds a dip/bump either side of the pivot.
  Tilt was added at the end of the type list (index 6), so sessions saved before it
  load unchanged; `stateVersion` stays 1. (A host automation lane recorded on a Type
  parameter stores a normalised 0–1 value, which now maps onto 7 choices instead of 6.)
- Zero latency. Bypass is an exact pass-through.
- No allocation, locks, logging or file access on the audio thread.

### Phase modes

| Mode | What it does | Latency @ 44.1 / 48 / 96 kHz |
|---|---|---|
| Zero Latency | The original minimum-phase SVF filters (bilinear transform). | 0 |
| Natural Phase | FIR built from each band's analogue prototype, magnitude **and** phase, so the response is not "cramped" near Nyquist and the phase is the analogue filter's. 4096-tap kernel with 768 samples of pre-delay. | 1024 / 1024 / 1792 samples (23 / 21 / 19 ms) |
| Linear Phase | Symmetric FIR from the analogue magnitude, no phase shift. 8192-tap Hann-windowed kernel. | 4352 / 4352 / 8448 samples (99 / 91 / 88 ms) |

Kernel lengths double at 88.2/96 kHz and quadruple above 100 kHz, so their length in
milliseconds stays about the same. Latency = kernel delay + 256 samples of partition
buffering. It is reported to the host with `setLatencySamples` and updated when the mode
changes. How quickly a host re-applies delay compensation after a change during playback
depends on the host (not tested in a DAW yet).

How it works:

- Convolution is uniformly partitioned overlap-save (256-sample partitions, 512-point FFTs);
  both channels share one kernel, each with its own convolution state.
- Dynamics still work: the zero-latency band filters and detectors run on a copy of the
  input to measure each band's dynamic gain, and the kernel is rebuilt from static +
  dynamic gains when anything has moved (at most every 512 samples at 44.1/48 kHz),
  crossfading to the new kernel over 256 samples. Because the audio is delayed and the
  detector is not, the gain change effectively has the mode's latency as look-ahead.
- Kernel rebuilds happen on the audio thread, in preallocated memory (no locks or
  allocation), so offline renders are deterministic.
- Bypass is latency-compensated: in Natural/Linear Phase the bypassed output is the input
  delayed by exactly the reported latency, and the EQ keeps running underneath so
  bypass/un-bypass is click-free and aligned.
- Switching modes changes the latency, so the output jumps; it is faded in over 256 samples.
- The display draws the analogue response in Natural/Linear Phase (what those modes
  play) and the bilinear response in Zero Latency.

Measured CPU (this Mac, test build, 48 kHz stereo, 512-sample blocks, 6 bands on with one
dynamic band, 12 dB/oct): Zero Latency ≈ 0.6 %, Natural ≈ 1.6 %, Linear ≈ 2.3 % of one core.
All six bands on Brickwall (16 sections each) with one dynamic band: Zero Latency ≈ 3.4 %,
Natural ≈ 4.2 %, Linear ≈ 5.2 %. Worst case, all six bands Brickwall bells, all dynamic (so
every band's response changes every block): Zero Latency ≈ 5.0 %, Natural ≈ 9.4 %,
Linear ≈ 13.9 %. Per model, six bands (cut, two shelves, two bells, cut) all dynamic,
Drive +6 dB:

| Model | Zero Latency | Natural Phase | Linear Phase |
|---|---|---|---|
| Digital | 0.6 % | 2.1 % | 3.6 % |
| Neve 1073/1081 | 4.2 % | 5.5 % | 7.1 % |
| Pultec EQP-1A | 4.2 % | 5.6 % | 6.9 % |
| Helios | 4.2 % | 5.6 % | 7.1 % |

(Most of the model cost is the 4 × oversampled colour stage, about 0.6 % per band;
every analogue model costs about the same.) (Since 0.7.0 the FIR modes cache each band's response and only recompute
bands that changed; since 0.7.1 multi-section responses are evaluated in one pass per bin.)

Zero Latency and high frequencies: the bilinear transform squeezes the top of the spectrum,
and steep skirts magnify that. dooblr-q corrects it per band: a steep bell keeps both Width
edges at exactly half the gain at any frequency (its peak can read slightly low near
Nyquist: 11.81 dB for a +12 dB 24 dB/oct bell at 15 kHz, exact at 10 kHz and below), and a
steep notch keeps the null exactly at the frequency and the lower −3 dB point exactly on its
edge, but its upper −3 dB point falls inside its mark as the frequency rises (upper edge
−2.99 / −2.86 dB at 1 kHz for 24 dB/oct / Brickwall, −2.55 / −0.67 dB at 5 kHz, ≈ 0 dB at
15 kHz for every steep slope). Natural and Linear Phase are exact.

Limitations: FIR modes have a finite frequency resolution (5.9 Hz bins at 48 kHz for
Linear, 11.7 Hz for Natural), so very narrow/deep low-frequency features are smoothed
), and low, high-Q bands that ring longer than the kernel are truncated (most
audible in Natural Phase). Linear Phase has pre-ringing on sharp boosts, as all
linear-phase EQs do. With Stereo Link off, the FIR modes use the louder channel's dynamic
gain for both channels.

### Editor

Dark, resizable editor (default 980×640). The header has the Phase Mode selector. It has a frequency-response display computed
from the same filter maths as the DSP. It draws the combined static curve (always in the
accent colour). Dynamic activity is shown per band: while a band's dynamics are actually
moving its gain, the area between the curve without and with that band's dynamic gain is
filled in the band's colour, and the live curve is drawn in that colour over the same span,
only where that band's own change exceeds 0.1 dB. A bell shows a local dip or bump around
its centre, a shelf only its shelf side, a tilt its moving side(s); several active bands
each show their own region, and a band with dynamics on but idle (no gain change) shows
nothing. (Before 0.6.1, any band with dynamics switched on greyed the static curve and drew
the whole live curve in amber across the full spectrum.) The display redraws
immediately on any parameter change (automation from the audio thread is picked up within
one 60 Hz frame).

Each band has a handle that sits on that band's own response at its frequency, so the
curve passes through it:

- Bell: at the gain.
- Shelves: at the shelf midpoint (half the gain, where the shelf's slope is centred).
  Dragging the handle 6 dB moves the shelf gain by 12 dB.
- Low/High Cut: at the corner level (−3 dB at Q 0.707).
- Notch: on the 0 dB line, since its centre is −∞.
- Tilt: on the curve at the pivot (always 0 dB). Dragging sideways moves the pivot;
  dragging up/down changes the tilt amount (moving 3 dB on the scale = 6 dB of tilt,
  i.e. the high end follows the mouse). The amount is shown above the handle.

Handle mouse actions:

- Click a handle to select its band.
- Drag it in two dimensions, for every band and every type: **horizontal = frequency**;
  **vertical = gain** for Bell, Low Shelf, High Shelf and Tilt, and **vertical = Q** for
  the gainless Low Cut, High Cut and Notch. Each drag is one undo step.
  - Bell: the handle is the gain. Shelves: the handle is the shelf midpoint (half the gain).
    Tilt: the handle stays on its 0 dB pivot and moving up/down by d dB changes the tilt by
    2d (shown as `Tilt +x.x dB` above it).
  - Low/High Cut: up = higher Q, i.e. a bigger resonant peak at the cutoff; down = lower Q,
    a gentler corner. The handle sits on the curve at the cutoff, whose level is exactly
    20·log10(Q): −3 dB at Q 0.707, 0 dB at Q 1, +6 dB at Q 2, +12 dB at Q 4; Q range 0.1–18.
  - Notch: up = narrower (higher Q), down = wider. Its centre is −∞ dB, so its handle height
    shows the Q on the same scale (0 dB line = Q 1) and a `Q x.xx` label is shown above it.
  - The vertical change is relative to where the drag starts, so a purely sideways drag
    never changes gain or Q.
- Dragging a disabled band's handle switches the band on.
- Double-click a handle to toggle its band on or off.
- The selected band also shows two small **bandwidth handles** on the curve at
  centre ÷ 2^(BW/2) and centre × 2^(BW/2) (BW = bandwidth in octaves from Q). Drag either
  one sideways: outward widens (lower Q), inward narrows; the other mirrors it so the
  centre frequency stays put, and a readout (`Width 2.27 oct (Q 0.57)`) shows while dragging. The
  cursor becomes a left-right arrow over them. The same pair is shown for shelves, cuts
  and tilt, where it marks the transition width that Q controls.
- Scroll over a handle to change Q; Option/Alt-drag a handle vertically to change Q (any type).
- Disabled bands show grey, hollow handles.
- Right-click (or Ctrl-click) a handle for a **Slope** menu (12–60 dB/oct, Brickwall; the
  current one ticked; every band type). Choosing one is one undo step. The curve and handles follow the slope.

Below the display are six band tabs and the selected band's controls: on/off, type,
Slope (under Type), frequency, gain, Width, dynamic on/off, range, threshold, attack, release. Controls that
currently have no audible effect (e.g. gain on a cut filter) are dimmed but still work.

Each band tab has a small **on/off switch on its left** (bound to that band's on/off
parameter): clicking the switch turns the band on or off without selecting the tab;
clicking anywhere else on the tab selects the band. The switch is filled in the band's
colour when on and drawn as a dim outline when off. It follows the parameter from
anywhere (Band on toggle, double-click on a handle, automation) and is undoable.

### Undo / redo

- **Cmd-Z** (macOS) / **Ctrl-Z** (Windows) undoes; **Cmd-Shift-Z** / **Ctrl-Shift-Z** or
  **Cmd-Y** / **Ctrl-Y** redoes. Small **Undo** / **Redo** buttons in the header do the same
  and are greyed out when there is nothing to undo or redo.
- Up to **50 actions** are kept (the oldest are dropped).
- One action = one user gesture: a whole knob or slider drag, a handle drag (frequency and
  gain together), a bandwidth-handle drag, an Option-drag, a type or phase-mode change, a
  toggle, a band-tab switch click, a double-click on a handle. Quick scroll-wheel steps on
  one control (less than 0.6 s apart) merge into one action.
- Only the plug-in's own UI creates entries. Host automation (and loading a session or
  preset) changes parameters without gestures and never fills the history; loading a
  state clears it. Undo/redo report their changes to the host as normal edits.
- Implementation: `ParameterUndoHistory` (Source/UndoHistory.*) listens for parameter
  gestures and records one `juce::UndoManager` transaction per gesture. It deliberately
  does not pass an UndoManager to the `AudioProcessorValueTreeState`: APVTS records into
  it from its 10–50 Hz flush timer, which would also record host automation and split
  gestures across timer ticks.
- Keyboard focus: the editor takes keyboard focus when you click anywhere in it, and
  claims Cmd/Ctrl-Z(-Shift)/Y only (other keys go to the host). **Host caveat:** whether
  the host passes the key to the plug-in window at all is up to the host. In Ableton Live
  the plug-in window must be the focused window (click inside it first); if Live still
  handles Cmd-Z itself (Live's own undo), use the Undo / Redo buttons.

### Spectrum analyzer and Freeze

The response display has a spectrum analyzer behind the grid, curve and handles.

- **Spectrum** toggle: shows/hides it. While hidden (or while no editor is open) the
  audio thread does not feed it at all.
- **Post-EQ / Pre-EQ**: what it analyses (default Post-EQ, after the output gain).
  Channels are summed to mono.
- **Tilt 0 / 3 / 4.5 dB/oct**: display slope around 1 kHz (default 4.5), so typical
  music reads roughly flat. It only changes the drawing.
- Scale: the analyzer has its own dBFS scale, 0 dBFS at the top to −90 dBFS at the
  bottom (labelled −30 and −60 dBFS on the right), independent of the EQ's ±24 dB scale.
- The live spectrum (soft blue-grey): per FFT frame an instant attack and a 30 dB/s fall,
  then display smoothing (below).

Freeze averages the spectrum, and only while it is on:

- With **Freeze** off there is no long-term averaging at all: just the live spectrum
  (with its light display smoothing). The readout shows `Live`.
- Turning **Freeze** on starts a fresh running **power average** of every FFT frame from
  that moment, drawn as a pink overlay. `Frozen: N s` counts the audio since Freeze was
  engaged (only FFT frames played entirely after that moment are averaged, so the first
  one lands ~85 ms in and nothing from before leaks in). While frozen the average keeps accumulating the live audio and the overlay is
  redrawn every frame, gliding as it changes; the live spectrum keeps moving on top.
- Turning **Freeze** off stops the averaging and clears the overlay. Turning it on
  again starts a new average from zero.
- **Clear** resets the peak-hold and, while frozen, restarts the average from zero
  (Freeze stays on).
- **Peak**: also draws the peak-hold since the last Clear as a dashed line (the peak-hold
  runs whether or not Freeze is on).
- The readout counts in 0.1 s steps in a fixed-width font (it only repaints when the
  text changes, so it does not flicker or jitter).
- The analyzer settings (visibility, source, tilt, peak, freeze) live in the plug-in
  instance and survive closing/reopening the editor, but they are **not saved** with
  the session or preset.

Display smoothing (message thread only; the audio thread is unchanged):

- Frequency: the live spectrum and the overlay are drawn on a 480-point log-frequency
  grid (20 Hz–20 kHz). Each point is the power average of the bins within its 1/12-octave
  band; at low frequencies, where a band holds fewer than ~3 bins, it is a Catmull-Rom
  interpolation between the neighbouring bins (limited so it never overshoots), blended
  smoothly into the average. As with any fractional-octave smoothing, a pure tone reads
  lower than its per-bin level (a −6 dBFS 1 kHz sine shows about −11 dBFS); the dashed
  peak-hold line is not smoothed and still shows tone peaks.
- Time: per grid point a one-pole filter in dB, advanced by the real elapsed time (so it
  behaves the same at any frame rate): live rise 30 ms, fall 160 ms; the overlay glides
  with 250 ms.
- Drawing: quadratic-curve paths through the grid points (no polyline corners).

Analyzer details: 4096-point FFT, periodic Hann window, 75% overlap (hop 1024; 11.7 Hz
bins and a frame every 21 ms at 48 kHz). Calibration: a sine of peak amplitude A reads
20·log10(A) dBFS, so a full-scale sine reads 0 dBFS when it sits on a bin and up to
1.42 dB lower between bins (Hann scalloping). The floor is −120 dBFS. On screen the
live spectrum and overlay are smoothed as described above; the peak-hold line shows the
loudest bin within ±1/24 octave.

CPU / real-time: the audio thread only mixes to mono and copies into a preallocated
lock-free FIFO (32768 samples), and only while an editor shows the analyzer (an atomic
flag). If the editor stalls, the FIFO drops the newest samples instead of blocking. The
FFT, averaging and drawing run on the message thread in the 60 Hz display timer. A test
checks that `processBlock` output is bit-identical with the analyzer on and off.

## Dependencies

| Tool | Notes |
|------|-------|
| CMake ≥ 3.22 | Tested with 3.31.2 |
| C++20 compiler | macOS: Xcode / Apple clang (tested: Apple clang 21, macOS 27 SDK, arm64). Windows: Visual Studio 2022. Linux: GCC ≥ 10 or Clang ≥ 12 plus JUCE's Linux packages. |
| git + network at **configure** time | `FetchContent` downloads JUCE once into `build/_deps`. |
| Ninja | Optional; used by `scripts/build.sh` if installed, otherwise Unix Makefiles. |

JUCE is pinned to the official release tag **8.0.15** (`DOOBLRQ_JUCE_TAG`).
To use a local JUCE checkout instead: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

No runtime network dependencies (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`).

## Building

```bash
./scripts/build.sh
```

Configures `build/` in Release, builds `DooblrQ_VST3`, builds and runs the
tests with CTest, and then installs the bundle, replacing only `dooblr-q.vst3` in the VST3 folder:

- The install goes to `/Library/Audio/Plug-Ins/VST3`, unless dooblr-q is installed only in
  `~/Library/Audio/Plug-Ins/VST3` (then that copy is updated).
- It never uses sudo. If the folder isn't writable, the script stops and reports it.
- The installed copy is checked against the build output, must carry no
  `com.apple.quarantine` flag, and is verified with `codesign --verify --deep --strict`.
- Finally the installed bundle is loaded like a host would load it, by
  `DooblrQLoadCheck` (Tools/LoadCheck): `dlopen` + `GetPluginFactory`, then JUCE's VST3
  hosting scans it, instantiates it, checks the version and processes audio. The script fails
  if this fails (`LOAD_CHECK=0` skips it).

**Why the quarantine check:** this project lives in an iCloud-synced (File Provider)
`~/Documents`, and the bundle folder in the build tree picked up `com.apple.quarantine`.
Copying it preserved the flag. A quarantined ad-hoc-signed plug-in still passes `codesign`,
but macOS refuses to load it into a host ("library load disallowed by system policy";
Finder shows "Apple could not verify dooblr-q.vst3 is free of malware"). Ableton then reports
"The bundle couldn't be loaded" / "not a plugin". The build strips the flag from the build
output after linking (POST_BUILD), and the install copies with `ditto --noqtn` and checks
again.

Rescan plug-ins in your DAW (or restart it) to load a new build.

Options:

```bash
BUILD_DIR=build-debug CONFIG=Debug INSTALL_VST3=0 ./scripts/build.sh   # Debug build, no install
RUN_TESTS=0 ./scripts/build.sh                                         # skip tests
INSTALL_VST3=0 ./scripts/build.sh                                      # build only
INSTALL_VST3_DIR=~/Library/Audio/Plug-Ins/VST3 ./scripts/build.sh      # choose the folder
LOAD_CHECK=0 ./scripts/build.sh                                        # skip the host-style load test
COPY_TO_PROJECT=1 ./scripts/build.sh                                   # also copy to ./dooblr-q.vst3
JOBS=4 ./scripts/build.sh
```

Manual equivalent:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DooblrQ_VST3 --parallel
cmake --build build --config Release --target DooblrQTests --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target DooblrQLoadCheck --parallel
"$(cat build/artefact-paths/DooblrQLoadCheck-Release.txt)" /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3
```

The build output is `build/DooblrQ_artefacts/Release/VST3/dooblr-q.vst3`
(the script reads the real path from
`build/artefact-paths/DooblrQ_VST3-<Config>.txt`, generated by CMake).

### Adding formats later

```bash
cmake -S . -B build -DDOOBLRQ_PLUGIN_FORMATS="VST3;AU"
cmake --build build --target DooblrQ_AU
```

Windows VST3 uses the same `CMakeLists.txt` with `-G "Visual Studio 17 2022"`.

## Plug-in metadata

| | |
|---|---|
| Product name | dooblr-q |
| Type | Audio effect, VST3 categories `Fx|EQ|Dynamics` |
| Manufacturer / plug-in code | `Dblr` / `Dblq` |
| Bundle ID | `com.dooblr.dooblrq` |
| Buses | Mono→mono or stereo→stereo (default stereo) |
| MIDI | None |
| Latency / tail | 0 (Zero Latency), 1024 (Natural), 4352 (Linear) samples at 48 kHz / 0.5 s |
| State | APVTS value tree with a `stateVersion` attribute (currently 1; analyzer settings are not saved) |

## Tests

`Tests/PluginSmokeTests.cpp` links the plug-in's shared-code target, so it
tests the same compiled code that goes into the VST3:

- instantiation, factory, name, MIDI flags, latency, parameter count and order (69: the
  slopes come after every older parameter);
- supported/rejected bus layouts;
- neutral settings (defaults, and all bands enabled at 0 dB with dynamics at 0 dB range) are identical to the input;
- +12 dB bell at 1 kHz raises a 1 kHz sine by 12 ± 0.3 dB and leaves 60 Hz and 16 kHz within 0.5 dB.
  The display maths agrees with the measured gain, and a 200 Hz low cut attenuates 50 Hz by > 20 dB;
- dynamic band (threshold −30 dB, range ∓12 dB): a −50 dBFS tone is unchanged,
  a −6 dBFS tone is cut (or boosted) by up to the range. The live display value matches;
- output stays finite under random per-block automation of every parameter
  (44.1 and 192 kHz) and at extreme settings;
- static EQ output is identical with one large block vs mixed block sizes,
  zero-length blocks are handled, dynamic processing with varying sizes is finite;
- bypass is bit-exact with active settings (mono and stereo);
- buffer channels beyond the bus are untouched;
- state round-trip restores every parameter; malformed/foreign state is ignored;
- editor: opens at default size, controls drive the selected band's parameters,
  re-selecting a band rebinds them, it resizes, paints, and closes cleanly (twice).

- curve follows handles. Dragging a disabled bell up and a disabled low shelf down:
  - enables both bands and moves the handles to the drop points;
  - puts the computed curve level at each handle frequency equal to the handle level;
  - renders curve pixels beside each handle that match the computed curve (within 1.5 px).

  The live dynamic curve equals static + dynamic gain. Double-click toggles a band.
  Off-thread automation schedules a redraw.

- per-band dynamic regions: an idle dynamic bell draws nothing; a loud 1 kHz tone gives one
  region confined to ~155 Hz–6 kHz whose depth at 1 kHz equals the dynamic gain, with no
  change at 60 Hz or 15 kHz; a dynamic low shelf driven by noise gets its own region from
  the bottom edge to ~620 Hz and nothing at 12 kHz, alongside the bell's; a static band has
  none; against the same display with dynamics off, pixels change at 40 Hz and 1 kHz and
  none at 10 or 15 kHz; dynamics off removes the regions.
- band 1/6 defaults and 2D drag: band 1 = Low Cut 30 Hz and band 6 = High Cut 18 kHz
  (Q 0.707, off), handles at −3 dB. For all seven types on band 1 and on band 6, a drag
  60 px up and from 1 to 2 kHz sets the frequency and raises gain (Bell = Δ dB, shelves and
  Tilt = 2Δ) or Q (cuts and Notch: Q = 10^(Δ/20)) without touching the other; the handle
  ends under the pointer (Tilt: on its pivot) and on the drawn curve; dragging down lowers
  it; each drag is one undo step and two undos restore the band. A sideways drag of a Q-18
  cut keeps Q 18.

- spectrum analyzer core (no GUI): on-bin sine calibration (−6.02 dBFS for A = 0.5),
  1 kHz full-scale in bin 85, live release, white noise flat within 1 dB, silence at
  exactly the floor with no NaN/Inf, FIFO overflow bounded and counted, variable block
  sizes lose nothing; the average is idle and empty while Freeze is off, starts at zero
  on engage, contains both tones played while frozen, keeps accumulating while frozen
  (a 9 kHz tone raises it there by ~80 dB), stops and clears on disengage, restarts fresh
  on re-engage; Clear restarts it while frozen and resets the peak-hold either way;
  sample-rate change resets;
- display smoothing: flat input stays flat; white noise spread across 1–20 kHz 4.9 dB per
  bin / 1.9 dB old display → 1.1 dB smoothed; live flicker at 5 kHz 0.73 → 0.10 dB per frame;
  smaller low-end corners; a tone still peaks at its frequency; rise/fall time constants
  hold at any frame rate and the fall is slower than the rise;
- undo/redo: 300 host-automation changes (message thread and another thread) create no
  entries; a knob drag, a handle drag (frequency + gain, or frequency + Q), a type change, a band-tab switch,
  a double-click enable and a bandwidth-handle drag are one step each; undo walks back
  through them exactly, redo reapplies; a new edit clears redo; Cmd/Ctrl-Z,
  Cmd/Ctrl-Shift-Z and Cmd/Ctrl-Y work and other keys are not claimed; the Undo/Redo buttons
  work and their enabled state follows the history; quick wheel ticks merge; 60 actions keep
  exactly the last 50; loading a state clears the history;
- band-tab switches: one per tab, on its left and inside it; states follow the
  parameters; clicks on the switch toggle the band without selecting the tab, clicks on
  the tab select it without toggling; undoable; on = band colour, off = dim;
- analyzer transparency: `processBlock` output is bit-identical with the analyzer off,
  on post-EQ and on pre-EQ; post-EQ analysis includes the output gain, pre-EQ does not;
- Tilt: display maths and measured audio give −3 / 0 / +3 dB for a +6 dB tilt at 1 kHz
  (and mirror for −6 dB); Q changes the transition; dynamic tilt tilts darker on loud
  material; dragging the handle sets the amount and the handle stays on the curve;
- a hand-written pre-Tilt (v0.2.x format) saved state restores every band type;
- phase modes: reported latency per mode at 44.1/48/96 kHz and after switching while
  running; a +12 dB bell measures +12 dB at its centre in every mode; near Nyquist the
  FIR modes follow the analogue design (+7.6 dB at 20 kHz for a +9 dB 15 kHz bell at
  44.1 kHz, vs +1.4 dB bilinear); analogue prototypes match the digital filters below
  Nyquist; the linear-phase impulse response peaks at the reported latency and is
  symmetric; neutral settings are identity after delay compensation (error < 2e-7);
  bypass is the input delayed by exactly the latency, aligned across toggles; random
  mode switching stays finite; dynamics give the same gain in all three modes; CPU per mode;
- Width: Q 1.414 = 1.000 oct, Q 0.667 = 1.999 oct, round trip over the Q range; for a −6 dB
  bell at Widths of 1, 2 and 3 octaves the measured level at the Width edges is −3.0 dB
  (±0.04) in Zero Latency and Linear Phase, and a wider Width cuts more at 1.5 kHz;
  a v0.4.0 state containing `bN_width` loads and the obsolete IDs are not re-saved;
- editor: dragging the upper bandwidth handle 40 px outward lowers Q, keeps the centre,
  and moves the lower handle 40 px the other way; inward narrows; scrolling changes Q;
  the Width knob shows octaves and setting 1.00 / 2.00 oct gives Q 1.414 / 0.667;
- editor: version label = `JucePlugin_VersionString` = CMake project version; Freeze,
  Peak, Clear, Spectrum controls and the readout; `Live` and no averaging while Freeze is
  off; Freeze on reads `Frozen: 0.0 s` and counts up from there while the overlay keeps
  changing with new audio; Freeze off clears it; display levels stay finite; closing the
  editor stops the feed;
- slope (0.7.0): 12 dB/oct is bit-identical to the old single-section filter for every type;
  for low and high cuts at every slope the corner is −3.01 dB and the analogue response is
  the exact order-N Butterworth; 1→2 octaves past the corner drops 11.8 / 24.1 / 36.1 /
  48.2 / 60.2 dB (low cut, digital) for 12–60 dB/oct; Brickwall is below −25 dB 10 % past
  the corner; the corner level is 20·log10(Q) for every slope; shelves/tilt get steeper with
  the midpoint at half the gain; measured through the plugin (Zero Latency, sines), a 1 kHz
  low cut gives −3.01 dB at 1 kHz and −12.3 / −24.1 / −36.2 / −48.2 / −60.3 / −137 dB at 500 Hz;
  Natural and Linear Phase match (Brickwall < −70 dB one octave out); Bell/Notch output is
  identical at any slope; slope and type changes while running cause no click; a dynamic
  48 dB/oct shelf follows its dynamic gain; a pre-0.7.0 state loads at 12 dB/oct and slopes
  round-trip; the editor's Slope selector, its disabled state and tooltip for a Bell, the
  right-click menu, undo/redo of slope changes, handles on their band's curve; CPU with
  all bands on Brickwall per mode;
- bell / notch slopes (0.7.1): per slope, bell centre exactly at the gain and Width edges at
  half the gain (analogue exact, digital within 0.15 dB at 1, 5, 10, 15 kHz), skirt
  steepness and flat width increase monotonically, Brickwall bell > 100 dB/oct (90→10 %) and
  notch > 200 dB/oct (−3→−20 dB); notch −3 dB at the edges and null at the frequency
  (digital: null and lower edge up to 15 kHz); 12 dB/oct bit-identical to before; sine
  measurements in all three phase modes; a dynamic Brickwall bell follows its dynamic gain
  in every mode; extreme Brickwall bells/notches stay finite; a running 12 → Brickwall bell
  change is click-free; the editor's Slope selector is enabled with a Bell/Notch tooltip,
  handles and width handles sit at the gain / half gain for every slope, the right-click
  menu and undo; CPU for six dynamic Brickwall bells per mode;
- models (0.8.0): Digital bit-identical to 0.7.1 in every mode and a pre-0.8.0 session loads
  as Digital; parameter count and the new IDs appended after the old ones; SSL E constant
  and SSL G proportional bell width, API width shrinking with gain for boost and cut, Pultec
  Boost + Atten shelf-with-dip and no dip from Boost alone, Neve / Helios / EMI shapes;
  Stepped snapping of frequency and gain to every table, fixed positions, Stepped off is
  continuous; colour: THD rises with level and Drive for every model, stays under 0.05 %
  at −40 dBFS, Digital is clean at any Drive, the same THD in all three phase modes, Pultec
  more 2nd harmonic than SSL; every model × type × mode at extreme settings stays finite,
  latency unchanged, FIR modes match the model curve with a quiet sine, a dynamic band
  follows its dynamic gain with every model; model state round-trips, undo / redo, a
  click-free switch between every model while running; editor Model selector and right-click
  menu items, Slope / Stepped / Drive / Atten enabled or disabled with tooltips, the drawn
  curve shows the model's response, and every knob readout fits its text box at the default
  and minimum editor sizes; CPU per model and mode.

Set `DOOBLRQ_EDITOR_SNAPSHOT=/path/editor.png` when running
`build/Tests/DooblrQTests` to save images of the rendered editor. It writes `editor.png`,
plus `editor-drag-before.png`, `editor-drag-after.png`, `editor-drag-dynamic.png`,
`editor-tilt.png`, `editor-spectrum-live.png`, `editor-spectrum-frozen.png`,
`editor-width-handles.png`, `editor-overview.png` (spectrum + frozen overlay + width handles)
`editor-v0.5.1.png` (the same, showing the band-tab switches and the Undo/Redo buttons)
`editor-v0.6.0.png` (the default band 1 high-pass and band 6 low-pass switched on) and
`editor-v0.6.1.png` (a dynamic low shelf and a dynamic bell, each with its own region) and
`editor-v0.7.0.png` (48 dB/oct low cut, 24 dB/oct low shelf, a bell and a resonant
Brickwall high cut) and `editor-v0.7.1.png` (bells at 12, 24, 48 dB/oct, Brickwall and
36 dB/oct, and a Brickwall notch) and `editor-v0.8.0.png` (Trident stepped low cut, Pultec
low shelf with Boost + Atten, API stepped bell, SSL G bell cut, Helios high shelf, Digital
high cut).

## Validation that still needs doing manually

1. **pluginval** (Tracktion, not bundled): download from
   <https://github.com/Tracktion/pluginval/releases>, then
   `pluginval --strictness-level 10 --validate-in-process /Library/Audio/Plug-Ins/VST3/dooblr-q.vst3`.
2. **Steinberg VST3 validator** (`validator` from the VST3 SDK).
3. **DAW**: rescan plug-ins (or restart the host) after each install, then load it in REAPER, Ableton
   Live, Bitwig, Cubase or Studio One. Check scanning, mono/stereo insert,
   automation, bypass, editor resize, and project save/reload. Logic Pro needs
   AU, which is not built yet.
