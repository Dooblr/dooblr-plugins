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
        static_sm = 0.0;
        closure_sm = 0.0;
        amp_sm = 0.0;
        cycle_amp = 1.0;
        cycle_period_scale = 1.0;
        cycle_index = 0;
        chaos_x = 0.37;
        vib_phase = 0.0;
        drift_value = 0.0;
        pitch_factor = 1.0;
        drift_counter = 0;
        flutter.reset();
        flutter_value = 0.0;
        vib_wander = 0.0;
        voicing = 0.0;
        area = 0.0;
        f0_now = params.f0;
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
            const auto dt32 = 32.0 / fs;
            flutter_value = flutter.step (dt32, rng);
            vib_wander = flutter.vibratoWander (dt32, rng);
            const auto rateMul = 1.0 + 0.04 * drift_value + 0.05 * vib_wander;
            vib_phase += ((p.vibrato_rate * 32.0) / fs) * rateMul;
            if (vib_phase > 1.0) vib_phase -= 1.0;
            // Vibrato only when the depth knob is up; no residual flutter or pitch walk.
            double cents = 0.0;
            if (p.vibrato_depth > 1.0e-6)
            {
                const auto depthMul = 1.0 + 0.08 * vib_wander;
                cents = p.vibrato_depth * depthMul * sin (TAU * vib_phase);
            }
            if (p.drift > 1.0e-9)
                cents += p.drift * drift_value;
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
            // Cycle jitter is capped at 0.5% so a held note stays inside the 15-cent bound.
            const auto jit = clamp (p.jitter * rng.gauss(), -0.005, 0.005);
            cycle_period_scale = 1.0 + jit + p.diplophonia * 0.08 * alt + chaos_p;
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

    static double lfOpenIntegral (double E0, double a, double w, double t) noexcept
    {
        const auto d = a * a + w * w;
        if (d < 1.0e-18) return 0.0;
        auto F = [a, w, d] (double x) noexcept {
            return exp (a * x) * (a * sin (w * x) - w * cos (w * x)) / d;
        };
        return E0 * (F (t) - F (0.0));
    }

    double LfGlottis::flow (double phase, double f0, double Rd, double ugPeak) noexcept
    {
        if (! (f0 > 1.0) || ! is_finite (f0))
        {
            ug = 0.0;
            return 0.0;
        }
        Rd = clamp (Rd, 0.3, 2.7);
        ugPeak = js_max (0.0, ugPeak);
        const auto wrapped = phase + 1.0e-9 < prev_phase;
        prev_phase = phase;
        if (wrapped || T0 < 1.0e-6 || omega <= 0.0)
        {
            T0 = 1.0 / f0;
            // Fant 1997 default R-parameter prediction from Rd.
            auto Ra = clamp ((-1.0 + 4.8 * Rd) * 0.01, 0.01, 0.15);
            auto Rk = clamp ((22.4 + 11.8 * Rd) * 0.01, 0.15, 0.55);
            auto Oq = clamp (0.27 + 0.23 * Rd, 0.32, 0.90);
            te = Oq * T0;
            tp = te / (1.0 + Rk);
            ta = Ra * T0;
            if (tp < 0.15 * T0) tp = 0.15 * T0;
            if (te < tp + 0.02 * T0) te = tp + 0.02 * T0;
            if (te > 0.95 * T0) te = 0.95 * T0;
            if (ta < 0.004 * T0) ta = 0.004 * T0;
            if (te + ta > T0) ta = js_max (0.002 * T0, 0.5 * (T0 - te));
            omega = PI / js_max (tp, 1.0e-6);
            Ee = 1.0;
            const auto tb = js_max (1.0e-6, T0 - te);
            // 1 - exp(-eps*tb) = eps * ta
            eps = 1.0 / js_max (ta, 1.0e-6);
            for (int i = 0; i < 10; ++i)
            {
                const auto e = exp (-eps * tb);
                const auto f = 1.0 - e - eps * ta;
                const auto fp = tb * e - ta;
                if (abs (fp) < 1.0e-12) break;
                eps -= f / fp;
                if (eps < 1.0) eps = 1.0;
            }
            // Open-phase growth alpha so the cycle integral of Ug' is zero.
            alpha = 0.0;
            auto solveE0 = [&] (double a) noexcept {
                const auto s = exp (a * te) * sin (omega * te);
                return (abs (s) > 1.0e-9) ? (-Ee / s) : 0.0;
            };
            auto areaOpen = [&] (double a) noexcept { return lfOpenIntegral (solveE0 (a), a, omega, te); };
            const auto kret = Ee / (eps * ta);
            const auto Iret = kret * ((1.0 - exp (-eps * tb)) / eps - tb * exp (-eps * tb));
            // We need areaOpen(alpha) + Iret = 0.
            for (int i = 0; i < 10; ++i)
            {
                const auto f = areaOpen (alpha) + Iret;
                const auto h = 2.0;
                const auto fp = (areaOpen (alpha + h) - areaOpen (alpha - h)) / (2.0 * h);
                if (abs (fp) < 1.0e-12) break;
                alpha -= f / fp;
                alpha = clamp (alpha, -2000.0, 8000.0);
            }
            E0 = solveE0 (alpha);
            ug_te = lfOpenIntegral (E0, alpha, omega, te);
            // Peak of the unit-Ee pulse, for scaling to ugPeak.
            peak = 0.0;
            for (int k = 1; k <= 24; ++k)
            {
                const auto t = (double (k) / 24.0) * te;
                const auto u = lfOpenIntegral (E0, alpha, omega, t);
                if (u > peak) peak = u;
            }
            if (peak < 1.0e-9) peak = 1.0;
        }

        const auto t = clamp (phase, 0.0, 1.0) * T0;
        double u = 0.0;
        if (t <= te)
            u = lfOpenIntegral (E0, alpha, omega, t);
        else if (t < T0)
        {
            const auto dt = t - te;
            const auto tb = T0 - te;
            const auto kret = Ee / (eps * ta);
            u = ug_te + kret * ((1.0 - exp (-eps * dt)) / eps - dt * exp (-eps * tb));
            if (u < 0.0) u = 0.0;
        }
        const auto scale = (peak > 1.0e-9) ? (ugPeak / peak) : 0.0;
        ug = is_finite (u * scale) ? (u * scale) : 0.0;
        return ug;
    }
}
