# dooblr-feedback roadmap

## 0.1.1 (now)

- High-frequency feedback fixed: 59 of 59 room cases at 3–18 kHz caught and held (0.1.0:
  39). Range high defaults to 20 kHz (0.1.0 sessions at 16 kHz are migrated); fast runaway
  path; first cut scaled by the growth rate; bounded, growth-gated lean; early re-centre on
  the real howl.
- One notch per feedback frequency: retune and deepen instead of stacking, merge, consolidate;
  only active notches are processed.
- Max cut up to ∞ (true, click-free notch), default 18 dB.
- Calm marker labels: stable lanes, eased moves, idle labels close to dots.
- Safety duck: feedback-gated output duck (Threshold, Depth to Mute, Recover), badge and meter
  tick, in the Advanced view. The badge has its own toolbar slot (a test checks it never
  overlaps another control at any editor size).
- Ring-out Start factory preset. 24 console tests (553 checks; 555 when the editor snapshots are rendered), LoadCheck, snapshots. Not yet
  tested live.

## 0.1.0

- Zero-latency bank of 24 IIR notches steered by a 4096-point FFT detector (own output or
  sidechain reference mic).
- Growth / persistence / narrowness / harmonic tests, Safe and Aggressive, Sensitivity.
- Float (release) / Hold, fixed (locked) notches, Ring-out, Max cut, Notch Q, frequency range.
- Markers on the spectrum with lock and clear; the notch table; room notes; room presets.

## Next

1. **Test live.** In a room with a real PA and mic, in Live and another VST3 host:
   - time to catch, low and high frequencies;
   - how much gain before feedback it adds;
   - false notches on real voices and instruments;
   - whether the Safety duck threshold and depth suit a real show;
   - CPU at 32/64-sample buffers.
2. **Aggressive mode on bright music.** In the bright-programme test Aggressive notches
   cymbals (8), hi-hats (5), a strummed guitar (8) and a saw chord (1); Safe notches none.
   Gate the runaway path harder on inharmonic, decaying partials (spectral flux, decay
   slope), then re-run the bright and HF tests together.
3. **Slow catches at loop gain 1.02.** High howls that only just run away take up to 1.6 s
   (18 kHz). Try a shorter second detector for the growth test.
4. **Large buffers.** At 2048-sample host buffers, detection (about 1.2 s in the simulation)
   is dominated by the round trip; check real hosts.
5. **Feedback inside a note's series.** It is held back while that note plays. Consider
   phase-coherence (sinusoid-ness over hops) as an extra feature.
6. **Optional AU / AAX**, Windows and Intel builds.
7. **MIDI / OSC "clear all"** for engineers.
