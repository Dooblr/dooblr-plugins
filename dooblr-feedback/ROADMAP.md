# dooblr-feedback roadmap

## 0.1.0 (now)

- Zero-latency bank of 24 IIR notches steered by a 4096-point FFT detector (own output or
  sidechain reference mic).
- Growth / persistence / narrowness / harmonic tests, Safe and Aggressive, Sensitivity.
- Float (release) / Hold, fixed (locked) notches, Ring-out, Max cut, Notch Q, frequency range.
- Markers on the spectrum (frequency, note, cut, Q, hits, time, FIX / FLT) with lock and
  clear; the notch table; room notes; room presets with notches and notes.
- Console tests, LoadCheck, snapshots. Not yet tested live.

## Next

1. **Test live.** In a room with a real PA and mic, in Live and another VST3 host:
   - time to catch;
   - how much gain before feedback it adds;
   - false notches on real voices and instruments;
   - CPU at 32/64-sample buffers.
2. **Faster first catch.** Detection takes 150–250 ms. Try:
   - a second, shorter (1024-point) detector for the growth test, with the 4096-point one
     kept for the frequency estimate;
   - or zero-padding a shorter window.
3. **Large buffers.** At 2048-sample host buffers, detection (about 1 s in the simulation)
   is dominated by the round trip; check real hosts.
4. **Speech in Aggressive.** Aggressive mode put 3 notches on steady synthetic-speech partials.
   Tune with recorded speech, for example with a longer pitch-drift test or spectral-flux
   gating.
5. **Feedback inside a note's series.** It is held back while that note plays. Consider
   phase-coherence (sinusoid-ness over hops) as an extra feature.
6. **Optional AU / AAX**, Windows and Intel builds.
7. **MIDI / OSC "clear all"** for engineers.
