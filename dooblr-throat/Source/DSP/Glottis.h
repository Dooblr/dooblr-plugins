#pragma once
// Parametric glottal area source, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Math.h"
#include <cstdint>

namespace dooblrthroat::dsp
{
    struct GlottisParams
    {
        double f0 = 110.0, amplitude = 0.16, abduction = 0.0, chink = 0.006;
        double open_quotient = 0.62, skew = 0.62, closure_sharpness = 0.6, effort = 0.7;
        double jitter = 0.004, shimmer = 0.03, vibrato_depth = 0.0, vibrato_rate = 6.0, drift = 6.0;
        double fry = 0.0, diplophonia = 0.0, closure = 0.0, chaos = 0.0;
    };

    inline constexpr GlottisParams DEFAULT_GLOTTIS {};


    // Three interpolating random walks at CHANT-like timescales (~0.05, 0.111, 1.219 s).
    struct PitchFlutter
    {
        struct Band { double period = 0.05, t = 0, a = 0, b = 0; };
        Band bands[3] { { 0.05 }, { 0.111 }, { 1.219 } };
        double vibWalk = 0.0;

        double step (double dt, Rng& rng) noexcept
        {
            double s = 0.0;
            for (auto& b : bands)
            {
                b.t += dt;
                if (b.t >= b.period)
                {
                    b.t -= b.period;
                    if (b.t >= b.period) b.t = 0.0;
                    b.a = b.b;
                    b.b = rng.gauss();
                }
                const auto u = b.period > 1e-9 ? clamp (b.t / b.period, 0.0, 1.0) : 0.0;
                s += b.a + (b.b - b.a) * u;
            }
            return s / 3.0;
        }

        double vibratoWander (double dt, Rng& rng) noexcept
        {
            vibWalk += (rng.gauss() - vibWalk) * (1.0 - exp (-dt / 0.35));
            return vibWalk;
        }
    };

    struct ParametricGlottis
    {
        double fs = 48000.0;
        GlottisParams params = DEFAULT_GLOTTIS;
        double voicing = 0.0, area = 0.0, f0_now = 110.0, threshold = 0.0;

        explicit ParametricGlottis (double fs);
        void reset() noexcept;
        double next (double dp) noexcept;
        double cycle_phase() const noexcept { return phase; }

    private:
        double phase = 0.0, cycle_amp = 1.0, cycle_period_scale = 1.0, chaos_x = 0.37;
        uint64_t cycle_index = 0;
        double vib_phase = 0.0, drift_value = 0.0, pitch_factor = 1.0;
        int32_t drift_counter = 0;
        double voicing_sm = 0.0, dp_sm = 0.0, dp_coef = 0.0, voice_coef = 0.0;
        double static_sm = 0.0, closure_sm = 0.0, amp_sm = 0.0, sm_coef = 0.0;
        Rng rng;
        Drift drift;
        PitchFlutter flutter;
        double flutter_value = 0.0, vib_wander = 0.0;
    };
}
