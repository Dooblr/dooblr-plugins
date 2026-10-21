#include "SvfFilter.h"

#include <algorithm>

namespace dooblrq::dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        double prewarp (double frequency, double sampleRate) noexcept
        {
            const auto clamped = std::clamp (frequency, 10.0, sampleRate * 0.49);
            return std::tan (pi * clamped / sampleRate);
        }

        SvfCoefficients finalise (double g, double k, double m0, double m1, double m2) noexcept
        {
            SvfCoefficients c;
            c.g = g;
            c.k = k;

            const auto a1 = 1.0 / (1.0 + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;

            c.a1 = (float) a1;
            c.a2 = (float) a2;
            c.a3 = (float) a3;
            c.m0 = (float) m0;
            c.m1 = (float) m1;
            c.m2 = (float) m2;
            return c;
        }
    }

    SvfCoefficients makeEqCoefficients (FilterType type, double frequency, double q,
                                        double gainDb, double sampleRate) noexcept
    {
        const auto w = prewarp (frequency, sampleRate);
        const auto k = 1.0 / std::max (q, 0.025);
        const auto a = std::pow (10.0, gainDb / 40.0);

        switch (type)
        {
            case FilterType::bell:
            {
                const auto kBell = 1.0 / (std::max (q, 0.025) * a);
                return finalise (w, kBell, 1.0, kBell * (a * a - 1.0), 0.0);
            }
            case FilterType::lowShelf:
                return finalise (w / std::sqrt (a), k, 1.0, k * (a - 1.0), a * a - 1.0);
            case FilterType::highShelf:
                return finalise (w * std::sqrt (a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a);
            case FilterType::lowCut:
                return finalise (w, k, 1.0, -k, -1.0);
            case FilterType::highCut:
                return finalise (w, k, 0.0, 0.0, 1.0);
            case FilterType::notch:
                return finalise (w, k, 1.0, -k, 0.0);
            case FilterType::tilt:
                // A high shelf of the full tilt amount scaled down by half of it (1/a): -gain/2
                // at DC, +gain/2 at Nyquist and exactly 0 dB at the pivot (the shelf midpoint).
                // Q shapes the transition like a shelf's (0.707 = steepest without overshoot).
                return finalise (w * std::sqrt (a), k, a, k * (1.0 - a), (1.0 - a * a) / a);
        }

        return finalise (w, k, 1.0, 0.0, 0.0);
    }

    SvfCoefficients makeDetectorCoefficients (FilterType type, double frequency, double q,
                                              double sampleRate) noexcept
    {
        const auto w = prewarp (frequency, sampleRate);
        constexpr auto butterworthK = 1.4142135623730951;

        switch (type)
        {
            case FilterType::lowShelf:
                return finalise (w, butterworthK, 0.0, 0.0, 1.0);
            case FilterType::highShelf:
                return finalise (w, butterworthK, 1.0, -butterworthK, -1.0);
            case FilterType::tilt:
                return finalise (w, butterworthK, 1.0, 0.0, 0.0); // pass-through: broadband level
            case FilterType::bell:
            case FilterType::notch:
            case FilterType::lowCut:
            case FilterType::highCut:
                break;
        }

        // k * band-pass output has unity gain at the centre frequency.
        const auto k = 1.0 / std::max (q, 0.025);
        return finalise (w, k, 0.0, k, 0.0);
    }

    std::complex<double> responseAt (const SvfCoefficients& c, double frequency, double sampleRate) noexcept
    {
        // The TPT SVF maps the analogue prototype through the bilinear transform with
        // prewarping, so the digital response equals H(j * tan(pi f / fs) / g).
        const auto omega = std::tan (pi * std::min (frequency, sampleRate * 0.4999) / sampleRate) / c.g;
        const std::complex<double> s (0.0, omega);
        const auto denominator = s * s + c.k * s + 1.0;
        return (double) c.m0 + ((double) c.m1 * s + (double) c.m2) / denominator;
    }
}
