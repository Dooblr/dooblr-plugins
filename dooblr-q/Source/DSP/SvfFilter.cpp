#include "SvfFilter.h"

#include <algorithm>
#include <complex>
#include <utility>

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

    namespace
    {
        /** Section of a cascade from monic polynomials: (s^2 + b1 s + b0) / (s^2 + a1 s + a0). */
        AnalogPrototype sectionFromPolynomials (double b1, double b0, double a1, double a0) noexcept
        {
            const auto w0 = std::sqrt (a0);
            const auto k = a1 / w0;
            return { w0, k, 1.0, b1 / w0 - k, b0 / a0 - 1.0 };
        }

        /** The two roots of s^2 - b s + 1 (product 1), the larger magnitude first. */
        std::pair<std::complex<double>, std::complex<double>> unitProductRoots (std::complex<double> b) noexcept
        {
            const auto d = std::sqrt (b * b - 4.0);
            auto r1 = 0.5 * (b + d), r2 = 0.5 * (b - d);
            if (std::abs (r1) < std::abs (r2))
                std::swap (r1, r2);
            return { r1, r2 };
        }

        /** Bell / notch of order 2n as n sections (see BandPrototype). */
        void makeBandpassFamily (FilterType type, int n, double q, double gainDb, BandPrototype& p) noexcept
        {
            const auto beta = 1.0 / std::max (q, 0.025);       // bandwidth: edges at centre * 2^(+-BW/2)
            const auto v = std::pow (10.0, gainDb / 20.0);
            const auto wz = std::pow (v, 1.0 / (2.0 * n));      // shelf zeros / poles radii
            const auto wp = 1.0 / wz;
            int index = 0;

            for (int i = 0; i < n; ++i)
            {
                // Order-n Butterworth poles; conjugates (lower half) are covered by their partners.
                const auto angle = pi * (2.0 * i + n + 1.0) / (2.0 * n);
                if (angle > pi + 1.0e-9)
                    continue;

                const auto pole = std::polar (1.0, angle);
                auto& sections = p.sections;

                if (std::abs (pole.imag()) < 1.0e-9) // the real pole (odd n): one section
                {
                    sections[(size_t) index++] = type == FilterType::bell
                        ? sectionFromPolynomials (wz * beta, 1.0, wp * beta, 1.0)
                        : sectionFromPolynomials (0.0, 1.0, beta, 1.0);
                    continue;
                }

                // Low-pass to band-pass (bell): each prototype root x maps to the roots of
                // s^2 - x beta s + 1. Low-pass to band-stop (notch): poles at the roots of
                // s^2 - (beta / x) s + 1, zeros at +-j (the centre).
                const auto poles = type == FilterType::bell ? unitProductRoots (wp * pole * beta)
                                                            : unitProductRoots (beta / pole);
                const auto zeros = type == FilterType::bell ? unitProductRoots (wz * pole * beta)
                                                            : std::make_pair (std::complex<double> (0.0, 1.0), std::complex<double> (0.0, 1.0));

                for (const auto& [z, r] : { std::make_pair (zeros.first, poles.first), std::make_pair (zeros.second, poles.second) })
                    sections[(size_t) index++] = sectionFromPolynomials (-2.0 * z.real(), std::norm (z), -2.0 * r.real(), std::norm (r));
            }

            p.numSections = index;
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

        if (type == FilterType::bell || type == FilterType::notch)
        {
            makeBandpassFamily (type, n, q, gainDb, p);
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
                    break; // handled above
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

        if (type == FilterType::bell || type == FilterType::notch)
        {
            // The bilinear transform squeezes frequencies towards Nyquist, which a steep bell or
            // notch would show as Width edges off their marks. Correct the design for it, using
            // the edges' warped positions o1 < 1 < o2 (relative to the warped centre):
            //  - Bell: centre the band between them (warped geometric mean), so the half-gain
            //    points land exactly on the Width edges. The bell's top is its maximum, so the
            //    small centre move barely changes the gain at the frequency (flat at high slopes).
            //  - Notch: keep the null exactly at the frequency and put the lower -3 dB point
            //    exactly on its edge; the warp then pulls the upper -3 dB point a little inside
            //    its mark (negligible below a few kHz; see the README).
            const auto fc = std::clamp (frequency, 10.0, sampleRate * 0.49);
            const auto w = std::tan (pi * fc / sampleRate);
            const auto beta = 1.0 / std::max (q, 0.025);
            const auto ratio = 0.5 * (beta + std::sqrt (beta * beta + 4.0)); // upper edge / centre
            const auto o1 = std::tan (pi * fc / ratio / sampleRate) / w;
            const auto upperAngle = pi * fc * ratio / sampleRate;
            auto designQ = q, centreScale = 1.0;

            if (type == FilterType::bell && upperAngle < pi * 0.4999)
            {
                const auto o2 = std::tan (upperAngle) / w;
                centreScale = std::sqrt (o1 * o2);
                designQ = centreScale / (o2 - o1);
            }
            else
            {
                designQ = 1.0 / (1.0 / o1 - o1); // band edges at o1 and 1 / o1 around the centre
            }

            auto prototype = makeBandPrototype (type, slope, designQ, gainDb);
            for (int i = 0; i < prototype.numSections; ++i)
                prototype.sections[(size_t) i].scale *= centreScale;
            makeBandCoefficients (prototype, frequency, sampleRate, c);
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

    void analogResponse (const BandPrototype& p, double bandFrequency, double sampleRate, int fftLength,
                         int numBins, std::complex<double>* destination) noexcept
    {
        if (p.numSections <= 1)
        {
            // Exactly as before 0.7.1 for single-section bands.
            for (int bin = 0; bin < numBins; ++bin)
                destination[bin] = analogResponseAt (p, bandFrequency, bin * sampleRate / fftLength);
            return;
        }

        // Cascades: numerator and denominator products in plain real arithmetic (no complex
        // division per section), renormalised every few sections, one division at the end.
        std::array<double, maxSections> inverseScale {};
        for (int i = 0; i < p.numSections; ++i)
            inverseScale[(size_t) i] = 1.0 / (std::max (bandFrequency, 1.0) * p.sections[(size_t) i].scale);

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto f = bin * sampleRate / fftLength;
            double nr = 1.0, ni = 0.0, dr = 1.0, di = 0.0;

            for (int i = 0; i < p.numSections; ++i)
            {
                const auto& s = p.sections[(size_t) i];
                const auto x = f * inverseScale[(size_t) i];
                const auto denRe = 1.0 - x * x, denIm = s.k * x;
                const auto numRe = s.m0 * denRe + s.m2, numIm = s.m0 * denIm + s.m1 * x;

                const auto tnr = nr * numRe - ni * numIm, tni = nr * numIm + ni * numRe;
                const auto tdr = dr * denRe - di * denIm, tdi = dr * denIm + di * denRe;
                nr = tnr; ni = tni; dr = tdr; di = tdi;

                if ((i & 3) == 3)
                {
                    const auto norm = 1.0 / (std::abs (dr) + std::abs (di) + 1.0e-300);
                    nr *= norm; ni *= norm; dr *= norm; di *= norm;
                }
            }

            const auto inv = 1.0 / (dr * dr + di * di);
            destination[bin] = { (nr * dr + ni * di) * inv, (ni * dr - nr * di) * inv };
        }
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
