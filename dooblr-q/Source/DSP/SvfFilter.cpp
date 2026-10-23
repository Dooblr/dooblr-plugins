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

    double butterworthSectionQ (int numSections, int index) noexcept
    {
        // Order N = 2n: poles at angles (2i - 1) pi / (2N) from the negative real axis.
        static const auto table = []
        {
            std::array<std::array<double, maxSections>, maxSections + 1> t {};

            for (int n = 1; n <= maxSections; ++n)
                for (int i = 0; i < n; ++i)
                    t[(size_t) n][(size_t) i] = 1.0 / (2.0 * std::cos ((2.0 * i + 1.0) * pi / (4.0 * n)));

            return t;
        }();

        numSections = std::clamp (numSections, 1, maxSections);
        return table[(size_t) numSections][(size_t) std::clamp (index, 0, numSections - 1)];
    }

    BandPrototype makeBandPrototype (FilterType type, Slope slope, double q, double gainDb) noexcept
    {
        BandPrototype p;
        const auto n = numSectionsFor (type, slope);

        if (n <= 1)
        {
            p.sections[0] = makeEqPrototype (type, q, gainDb);
            p.numSections = 1;
            return p;
        }

        p.numSections = n;
        const auto resonance = std::max (q, 0.025) / 0.70710678118654752;
        const auto order = 2.0 * n;
        const auto v = std::pow (10.0, gainDb / 20.0); // linear gain of the shelf / full tilt

        for (int i = 0; i < n; ++i)
        {
            auto sectionQ = butterworthSectionQ (n, i);
            if (i == 0)
                sectionQ *= resonance;

            const auto k = 1.0 / sectionQ;
            auto& s = p.sections[(size_t) i];

            switch (type)
            {
                case FilterType::lowCut:  s = { 1.0, k, 1.0, -k, -1.0 }; break;
                case FilterType::highCut: s = { 1.0, k, 0.0, 0.0, 1.0 }; break;

                case FilterType::highShelf:
                case FilterType::lowShelf:
                case FilterType::tilt:
                {
                    // High shelf section: poles at f * rho, zeros at f / rho, rho = V^(1/2N);
                    // normalised to the poles its numerator is rho^4 S^2 + rho^2 k S + 1.
                    // Low shelf = V * highShelf (1/V); tilt = highShelf (V) / sqrt (V), with
                    // the constant spread evenly over the sections.
                    const auto shelfGain = type == FilterType::lowShelf ? 1.0 / v : v;
                    const auto rho = std::pow (shelfGain, 1.0 / (2.0 * order));
                    const auto r2 = rho * rho, r4 = r2 * r2;
                    const auto scale = type == FilterType::lowShelf ? std::pow (v, 1.0 / n)
                                     : type == FilterType::tilt     ? std::pow (v, -0.5 / n)
                                                                    : 1.0;
                    s = { rho, k, r4 * scale, k * r2 * (1.0 - r2) * scale, (1.0 - r4) * scale };
                    break;
                }

                case FilterType::bell:
                case FilterType::notch:
                    break; // single section (handled above)
            }
        }

        return p;
    }

    void makeBandCoefficients (const BandPrototype& prototype, double frequency, double sampleRate,
                               BandCoefficients& destination) noexcept
    {
        const auto w = prewarp (frequency, sampleRate);
        destination.numSections = prototype.numSections;

        for (int i = 0; i < prototype.numSections; ++i)
        {
            const auto& p = prototype.sections[(size_t) i];
            destination.sections[(size_t) i] = finalise (w * p.scale, p.k, p.m0, p.m1, p.m2);
        }
    }

    BandCoefficients makeBandCoefficients (FilterType type, Slope slope, double frequency, double q,
                                           double gainDb, double sampleRate) noexcept
    {
        BandCoefficients c;

        if (numSectionsFor (type, slope) <= 1)
        {
            c.sections[0] = makeEqCoefficients (type, frequency, q, gainDb, sampleRate);
            c.numSections = 1;
            return c;
        }

        makeBandCoefficients (makeBandPrototype (type, slope, q, gainDb), frequency, sampleRate, c);
        return c;
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

    AnalogPrototype makeEqPrototype (FilterType type, double q, double gainDb) noexcept
    {
        // Mirrors makeEqCoefficients: same k and output mix, with the frequency scale that
        // makeEqCoefficients applies to the prewarped g.
        const auto k = 1.0 / std::max (q, 0.025);
        const auto a = std::pow (10.0, gainDb / 40.0);

        switch (type)
        {
            case FilterType::bell:
            {
                const auto kBell = 1.0 / (std::max (q, 0.025) * a);
                return { 1.0, kBell, 1.0, kBell * (a * a - 1.0), 0.0 };
            }
            case FilterType::lowShelf:  return { 1.0 / std::sqrt (a), k, 1.0, k * (a - 1.0), a * a - 1.0 };
            case FilterType::highShelf: return { std::sqrt (a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a };
            case FilterType::lowCut:    return { 1.0, k, 1.0, -k, -1.0 };
            case FilterType::highCut:   return { 1.0, k, 0.0, 0.0, 1.0 };
            case FilterType::notch:     return { 1.0, k, 1.0, -k, 0.0 };
            case FilterType::tilt:      return { std::sqrt (a), k, a, k * (1.0 - a), (1.0 - a * a) / a };
        }

        return {};
    }

    std::complex<double> analogResponseAt (const AnalogPrototype& p, double bandFrequency, double frequency) noexcept
    {
        const std::complex<double> s (0.0, frequency / (std::max (bandFrequency, 1.0) * p.scale));
        return p.m0 + (p.m1 * s + p.m2) / (s * s + p.k * s + 1.0);
    }

    std::complex<double> analogResponseAt (const BandPrototype& p, double bandFrequency, double frequency) noexcept
    {
        std::complex<double> h (1.0, 0.0);

        for (int i = 0; i < p.numSections; ++i)
            h *= analogResponseAt (p.sections[(size_t) i], bandFrequency, frequency);

        return h;
    }

    std::complex<double> responseAt (const BandCoefficients& c, double frequency, double sampleRate) noexcept
    {
        const auto t = std::tan (pi * std::min (frequency, sampleRate * 0.4999) / sampleRate);
        std::complex<double> h (1.0, 0.0);

        for (int i = 0; i < c.numSections; ++i)
        {
            const auto& section = c.sections[(size_t) i];
            const std::complex<double> s (0.0, t / section.g);
            h *= (double) section.m0 + ((double) section.m1 * s + (double) section.m2) / (s * s + section.k * s + 1.0);
        }

        return h;
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
