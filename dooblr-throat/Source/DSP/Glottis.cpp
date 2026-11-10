#include "Glottis.h"
#include "Physics.h"

namespace dooblrthroat::dsp
{
    static double shape (double ph, double oq, double skew, double sharp) noexcept
    {
        if (ph >= oq) return 0.0;
        const auto t = ph / oq;
        const auto s = clamp (skew, 0.3, 0.9);
        if (t < s) return 0.5 - 0.5 * cos ((PI * t) / s);
        const auto u = (t - s) / (1.0 - s);
        const auto soft = 0.5 + 0.5 * cos (PI * u);
        const auto hard = cos (0.5 * PI * u);
        return soft + (hard - soft) * sharp;
    }

    ParametricGlottis::ParametricGlottis (double fsIn)
        : fs (fsIn), rng (777), drift (fsIn / 32.0, 0.3, 4242)
    {
        threshold = 2.5 * CM_H2O;
        dp_coef = 1.0 - exp (-TAU * 30.0 / fs);
        voice_coef = 1.0 - exp (-TAU * 25.0 / fs);
        sm_coef = 1.0 - exp (-1.0 / (0.004 * fs));
    }

    void ParametricGlottis::reset() noexcept
    {
        phase = 0.0;
        voicing_sm = 0.0;
        dp_sm = 0.0;
    }

    double ParametricGlottis::next (double dp) noexcept
    {
        const auto p = params;
        dp_sm += dp_coef * (dp - dp_sm);

        drift_counter -= 1;
        if (drift_counter <= 0)
        {
            drift_counter = 32;
            drift_value = drift.step();
            vib_phase += ((p.vibrato_rate * 32.0) / fs) * (1.0 + 0.04 * drift_value);
            if (vib_phase > 1.0) vib_phase -= 1.0;
            const auto cents = p.vibrato_depth * sin (TAU * vib_phase) + p.drift * drift_value;
            pitch_factor = pow (2.0, cents / 1200.0);
        }
        auto f0 = p.f0 * pitch_factor;
        if (p.fry > 0.0)
            f0 = f0 * (1.0 - p.fry) + js_min (f0, 48.0) * p.fry;
        f0_now = f0;

        const auto pressure_gate = smoothstep (threshold * 0.8, threshold * 1.6, dp_sm);
        const auto adduct_gate = 1.0 - smoothstep (0.08, 0.22, static_sm - p.chink);
        const auto target = pressure_gate * adduct_gate * (1.0 - closure_sm);
        voicing_sm += voice_coef * (target - voicing_sm);
        voicing = voicing_sm;

        phase += (f0 / fs) / cycle_period_scale;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            cycle_index += 1;
            const auto alt = (cycle_index & 1) == 1 ? 1.0 : -1.0;
            auto chaos_p = 0.0, chaos_a = 0.0;
            if (p.chaos > 0.0)
            {
                const auto r = 2.9 + 1.1 * js_min (1.0, p.chaos);
                chaos_x = r * chaos_x * (1.0 - chaos_x);
                if (! (chaos_x > 0.0 && chaos_x < 1.0)) chaos_x = 0.37;
                const auto dev = chaos_x - 0.5;
                chaos_p = 0.45 * p.chaos * dev;
                chaos_a = 1.1 * p.chaos * dev;
            }
            cycle_period_scale = 1.0 + p.jitter * rng.gauss() + p.diplophonia * 0.08 * alt + chaos_p;
            cycle_period_scale = clamp (cycle_period_scale, 0.55, 1.8);
            cycle_amp = js_max (0.12, 1.0 + p.shimmer * rng.gauss() - p.diplophonia * 0.35 * (alt > 0.0 ? 1.0 : 0.0) + chaos_a);
        }

        auto oq = clamp (p.open_quotient - 0.18 * (p.effort - 0.5), 0.25, 0.98);
        if (p.fry > 0.0)
            oq = oq * (1.0 - p.fry) + 0.28 * p.fry;
        auto pulse = shape (phase, oq, p.skew, p.closure_sharpness);
        if (p.fry > 0.3)
        {
            const auto ph2 = phase - 0.38;
            if (ph2 > 0.0)
                pulse += 0.55 * smoothstep (0.3, 0.8, p.fry) * shape (ph2, oq * 0.8, p.skew, p.closure_sharpness);
        }
        const auto effort_scale = 0.45 + 0.75 * p.effort;
        const auto sc = sm_coef;
        amp_sm += sc * (p.amplitude * effort_scale - amp_sm);
        closure_sm += sc * (p.closure - closure_sm);
        static_sm += sc * (p.abduction + p.chink - static_sm);
        const auto vib = amp_sm * cycle_amp * voicing_sm * pulse;
        const auto statics = static_sm * (1.0 - 0.92 * closure_sm);
        const auto a = js_max (0.0, vib + statics);
        area = a;
        return a;
    }
}
