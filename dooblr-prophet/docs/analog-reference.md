# Prophet 5/6/10 analog emulation reference (from Dan, 2026-10-28)

## Revisions
- P5 Rev1/2: SSM chips (SSM2040 filter). Rev3 (classic): CEM3340 VCOs + CEM3320 4-pole LPF.
- P5/10 Rev4: CEM3340 VCOs, switchable filters: SSM2040-derived "2140" (Rev2 mode) and CEM3320 (Rev3 mode).
- P6: discrete VCOs, continuously variable shape (tri/saw/var-width pulse); discrete 4-pole LPF based on SSM2040; per-voice 2-pole resonant HPF (order vs LPF unconfirmed).
- Numbers come from Alfa AS3340A/AS3320 clone datasheets: representative, not original silicon.

## Oscillator (CEM3340 style)
- Expo converter 1V/oct, I ∝ 2^CV; ramp (saw) core with comparator + reset. Tri ≈ half saw amplitude (≈10V saw, 5V tri pre-scaling). Pulse = comparator on ramp, VC width.
- Rev3: Osc A is the sync slave; Osc B same circuit without sync, uses triangle.
- Scale error ≤ ~3.5 cents (not the audible source of mistuning).
- Temp: ~+3300 ppm/°C -> per-voice slow drift + warm-up exponential; autotune re-zeros.
- HF flattening: f_eff = 1/(1/f_ideal + t_r), t_r ≈ 1–3 µs (2 µs @5 kHz ≈ 17 cents flat).
- Finite reset (~1 µs); triangle glitch at reset; triangle asymmetry 45–55%.
- Small PWM->pitch crosstalk.
- Poly Mod FM through the EXPONENTIAL input: per-sample exp2, asymmetric (raises mean pitch), oversample.
- Hard sync: slave integrator reset on master edge, BLEP + finite reset time.
- Per-voice variance (scale, offset, drift) -> "Vintage" knob randomizes these.
- P6 VCOs: no schematic; sync osc1->osc2; Poly Mod sources filter env + osc2 (bipolar) -> osc1 freq, shape, PW, both filter cutoffs.

## Filter CEM3320
- 4 OTA+buffer one-pole stages, expo converter, resonance VCA. f_p = AI0/(2π R_EQ C)·exp(−Vc/VT), AI0≈0.9, R_EQ = 100k ∥ 1M.
- H(s) = 1/((1+s/ωc)^4 + k); self-osc at k=4 at ωc; peak ≈ ωc·k^(1/4)/√2 for k<4; DC gain 1/(1+k) (≈ −14 dB bass loss at k=4).
- Passband THD 0.1%; self-osc sine THD 0.5% (clean, amplitude-limited).
- Clip 12 Vpp with 6.5 V DC offset at output; 10,000:1 range; 60 mV/decade.
- Tempco 3300 ppm, warm-up drift 0.5%. CV feedthrough: 60 mV cutoff, 0.2 V resonance (model as small HP-coupled term ∝ CV).
- Buffer slew 3 V/µs limits very high cutoffs. Scale error 4% (definition unclear).
- NOT Moog-style tanh at mV levels: saturation mainly at mixer level, resonance cell, output stage near rails.
- Per-voice randomize each stage's cutoff (cap mismatch affects peak shape and oscillation threshold).

## SSM2040 / 2140 (Rev1/2, Rev4 Rev2 mode, P6)
- Same architecture; differs in gain-cell linearity, resonance law, clip points. Community: saturates earlier and softer (hypothesis, unverified).

## Blueprint
Oscillator:
  CV_total = pitch + fine + polymod_exp(t) + per-voice(scale_err, offset, drift(t))
  f_ideal = f_ref * 2^CV_total
  f_eff = 1/(1/f_ideal + t_r)
  phase += f_eff/fs
  saw = BLEP ramp, finite reset; tri = fold of ramp (or integrated square with BLAMP), slight asymmetry
  pulse = ramp > PW threshold + small PWM->pitch leak; sync = sub-sample BLEP slave reset
  4–8x oversampling when Poly Mod FM active.
Filter (4 TPT one-poles, ZDF):
  g = tan(pi*fc/fs); G = g/(1+g); G4 = G^4
  S = (1-G)*(G^3 s1 + G^2 s2 + G s3 + s4)
  y4 = (G4 x + S)/(1 + k G4)
  u = x - k y4
  v1 = G(u-s1);  y1 = v1+s1; s1 = y1+v1
  v2 = G(y1-s2); y2 = v2+s2; s2 = y2+v2
  v3 = G(y2-s3); y3 = v3+s3; s3 = y3+v3
  v4 = G(y3-s4); y4 = v4+s4; s4 = y4+v4
  fc exponential with scale/offset errors; k linear in resonance control with a knob taper.
  Nonlinear: soft clip at mixer/filter input; saturating (tanh-like) resonance cell in feedback (sets osc amplitude, low THD);
  output clip with bias offset; optional mild per-stage saturation (stronger for SSM variants);
  1–2 Newton / fixed-point iterations per sample at >=2x oversampling.

## Measurements to fit later (on real hardware)
V/oct sweep per voice; PW sweep for PWM->pitch; sync waveforms; filter AC sweeps vs resonance & level + THD vs level;
self-osc amplitude/THD; step response to cutoff CV (feedthrough); resonance control vs k; Poly Mod depth law; voice-to-voice stats.
