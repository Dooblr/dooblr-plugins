#pragma once
// Deterministic helpers ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).
// Transcendentals use the host libm; we do not claim bit-identity with the Rust engine.

#include <cmath>
#include <cstdint>

namespace dooblrthroat::dsp
{
    inline constexpr double PI = 3.14159265358979323846264338327950288;
    inline constexpr double TAU = PI * 2.0;

    inline double sin (double x) noexcept { return std::sin (x); }
    inline double cos (double x) noexcept { return std::cos (x); }
    inline double tan (double x) noexcept { return std::tan (x); }
    inline double exp (double x) noexcept { return std::exp (x); }
    inline double ln (double x) noexcept { return std::log (x); }
    inline double log10 (double x) noexcept { return std::log10 (x); }
    inline double log2 (double x) noexcept { return std::log2 (x); }
    inline double pow (double x, double y) noexcept { return std::pow (x, y); }
    inline double tanh (double x) noexcept { return std::tanh (x); }
    inline double atan2 (double y, double x) noexcept { return std::atan2 (y, x); }
    inline double acos (double x) noexcept { return std::acos (x); }
    inline double asin (double x) noexcept { return std::asin (x); }
    inline double atan (double x) noexcept { return std::atan (x); }
    inline double cbrt (double x) noexcept { return std::cbrt (x); }
    inline double hypot (double x, double y) noexcept { return std::hypot (x, y); }
    inline double sinh (double x) noexcept { return std::sinh (x); }
    inline double cosh (double x) noexcept { return std::cosh (x); }
    inline double sqrt (double x) noexcept { return std::sqrt (x); }
    inline double floor (double x) noexcept { return std::floor (x); }
    inline double ceil (double x) noexcept { return std::ceil (x); }
    inline double abs (double x) noexcept { return std::abs (x); }
    inline bool is_nan (double x) noexcept { return std::isnan (x); }
    inline bool is_finite (double x) noexcept { return std::isfinite (x); }
    inline bool is_sign_negative (double x) noexcept { return std::signbit (x); }
    inline bool is_sign_positive (double x) noexcept { return ! std::signbit (x); }

    inline double js_min (double a, double b) noexcept
    {
        if (is_nan (a) || is_nan (b)) return NAN;
        if (a < b) return a;
        if (b < a) return b;
        return is_sign_negative (a) ? a : b;
    }

    inline double js_max (double a, double b) noexcept
    {
        if (is_nan (a) || is_nan (b)) return NAN;
        if (a > b) return a;
        if (b > a) return b;
        return is_sign_positive (a) ? a : b;
    }

    inline double js_round (double x) noexcept
    {
        const auto r = floor (x);
        return (x - r >= 0.5) ? r + 1.0 : r;
    }

    inline double js_sign (double x) noexcept
    {
        if (x > 0.0) return 1.0;
        if (x < 0.0) return -1.0;
        return x;
    }

    inline int32_t js_trunc_i32 (double x) noexcept
    {
        return is_finite (x) ? (int32_t) (int64_t) std::trunc (x) : 0;
    }

    inline double clamp (double x, double lo, double hi) noexcept
    {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }

    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }

    inline double smoothstep (double e0, double e1, double x) noexcept
    {
        const auto t = clamp ((x - e0) / (e1 - e0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    inline double db_to_gain (double db) noexcept { return pow (10.0, db / 20.0); }
    inline double gain_to_db (double g) noexcept { return 20.0 * log10 (js_max (1e-12, g)); }
    inline double midi_to_hz (double m) noexcept { return 440.0 * pow (2.0, (m - 69.0) / 12.0); }
    inline double hz_to_midi (double f) noexcept { return 69.0 + 12.0 * log2 (js_max (1e-6, f) / 440.0); }
    inline double cents_to_ratio (double c) noexcept { return pow (2.0, c / 1200.0); }

    struct Rng
    {
        uint32_t s = 0x9e3779b9u;
        Rng() = default;
        explicit Rng (uint32_t seed) : s (seed == 0 ? 1u : seed) {}

        double next() noexcept
        {
            s += 0x6d2b79f5u;
            auto t = s;
            t = (t ^ (t >> 15)) * (t | 1u);
            t ^= t + (t ^ (t >> 7)) * (t | 61u);
            return double (t ^ (t >> 14)) / 4294967296.0;
        }

        double bipolar() noexcept { return next() * 2.0 - 1.0; }

        double gauss() noexcept
        {
            const auto a = next(), b = next(), c = next(), d = next();
            return (a + b + c + d - 2.0) * 1.7320508;
        }
    };

    struct Smoother
    {
        double value = 0.0;
        double a = 0.0;
        Smoother() = default;
        Smoother (double initial, double tau_samples)
            : value (initial), a (tau_samples > 0.0 ? exp (-1.0 / tau_samples) : 0.0) {}
        void set_tau (double tau_samples) { a = tau_samples > 0.0 ? exp (-1.0 / tau_samples) : 0.0; }
        double step (double target) { value = target + (value - target) * a; return value; }
    };

    struct Drift
    {
        double y[3] {};
        double a[3] {};
        Rng rng;

        Drift() = default;
        Drift (double control_rate, double base_hz, uint32_t seed) : rng (seed)
        {
            const double f[3] = { base_hz, base_hz * 3.0, base_hz * 9.0 };
            for (int i = 0; i < 3; ++i)
                a[i] = exp ((-TAU * f[i]) / control_rate);
        }

        double step() noexcept
        {
            double s = 0.0;
            for (int i = 0; i < 3; ++i)
            {
                const auto ai = a[i];
                y[i] = ai * y[i] + sqrt (1.0 - ai * ai) * rng.gauss();
                s += y[i] / (double (i) + 1.0);
            }
            return s / 1.4;
        }
    };
}
