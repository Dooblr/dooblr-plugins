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
    };
}
