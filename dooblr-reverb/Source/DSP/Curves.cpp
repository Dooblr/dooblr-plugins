#include "Curves.h"

#include <complex>

namespace dooblrreverb::dsp
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double analogPrewarpRate = 48000.0;
    }

    SvfCoeffs makeSvf (SvfType type, double freq, double sampleRate, double q, double gainDb) noexcept
    {
        SvfCoeffs c;
        const auto rate = sampleRate > 0.0 ? sampleRate : analogPrewarpRate;
        freq = juce::jlimit (5.0, rate * 0.49, freq);
        q = juce::jlimit (0.025, 40.0, q);
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto g0 = std::tan (pi * freq / rate);
        c.freq = freq;

        double g = g0, k = 1.0 / q, m0 = 1.0, m1 = 0.0, m2 = 0.0, scale = 1.0;
        switch (type)
        {
            case SvfType::bell:      k = 1.0 / (q * A); m1 = k * (A * A - 1.0); break;
            case SvfType::lowShelf:  scale = 1.0 / std::sqrt (A); m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case SvfType::highShelf: scale = std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
            case SvfType::lowPass:   m0 = 0.0; m2 = 1.0; break;
            case SvfType::highPass:  m1 = -k; m2 = -1.0; break;
            case SvfType::notch:     m1 = -k; break;
        }
        g = g0 * scale;
        c.g = (float) g; c.k = (float) k; c.m0 = (float) m0; c.m1 = (float) m1; c.m2 = (float) m2;
        c.gScale = scale;
        c.update();
        return c;
    }

    double svfMagnitudeDb (const SvfCoeffs& c, double freq, double sampleRate) noexcept
    {
        // H(s) = m0 + (m1 s + m2) / (s^2 + k s + 1), with s = j w on the filter's own scale.
        const auto w = sampleRate > 0.0 ? std::tan (pi * juce::jlimit (0.0, sampleRate * 0.4999, freq) / sampleRate) / std::max (1.0e-9, (double) c.g)
                                        : freq / (c.freq * c.gScale);
        const std::complex<double> s (0.0, w);
        const auto h = (double) c.m0 + ((double) c.m1 * s + (double) c.m2) / (s * s + (double) c.k * s + 1.0);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    //==============================================================================
    double decayBandLog2 (const DecayBand& band, double freq) noexcept
    {
        if (! band.active)
            return 0.0;

        using params::DecayShape;
        const auto rate = band.shape == DecayShape::notch ? (double) params::minRate : (double) band.rate;
        const auto db = 20.0 * std::log10 (juce::jlimit ((double) params::minRate, (double) params::maxRate, rate) / 100.0);
        const auto type = band.shape == DecayShape::lowShelf ? SvfType::lowShelf
                        : band.shape == DecayShape::highShelf ? SvfType::highShelf : SvfType::bell;
        const auto q = band.shape == DecayShape::notch ? band.q * 2.0 : (double) band.q;
        const auto c = makeSvf (type, band.freq, 0.0, q, db);
        return svfMagnitudeDb (c, freq, 0.0) * log2Of10Over20;
    }

    double decayCurveLog2 (const DecayBands& bands, double freq) noexcept
    {
        double sum = 0.0;
        for (const auto& b : bands)
            sum += decayBandLog2 (b, freq);
        return juce::jlimit (std::log2 (params::minRate / 100.0), std::log2 (params::maxRate / 100.0), sum);
    }

    //==============================================================================
    int eqSections (const EqBand& band) noexcept
    {
        return params::eqShapeIsCut (band.shape) ? params::slopeSections (band.slope) : 1;
    }

    SvfCoeffs eqSectionCoeffs (const EqBand& band, int section, double sampleRate) noexcept
    {
        using params::EqShape;
        switch (band.shape)
        {
            case EqShape::bell:      return makeSvf (SvfType::bell, band.freq, sampleRate, band.q, band.gain);
            case EqShape::lowShelf:  return makeSvf (SvfType::lowShelf, band.freq, sampleRate, band.q, band.gain);
            case EqShape::highShelf: return makeSvf (SvfType::highShelf, band.freq, sampleRate, band.q, band.gain);
            case EqShape::lowCut:
            case EqShape::highCut:
            default:
            {
                // Butterworth section Qs for 2, 4 and 8 poles; the band's Q (0.71 = flat) scales the
                // resonance of the last section only, so the slope stays a clean Butterworth.
                static constexpr double q2[] { 0.70710678 };
                static constexpr double q4[] { 0.54119610, 1.30656296 };
                static constexpr double q8[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
                const auto n = eqSections (band);
                const auto* qs = n == 1 ? q2 : (n == 2 ? q4 : q8);
                auto q = qs[juce::jlimit (0, n - 1, section)];
                if (section == n - 1)
                    q *= band.q / 0.70710678;
                return makeSvf (band.shape == EqShape::lowCut ? SvfType::highPass : SvfType::lowPass, band.freq, sampleRate, q, 0.0);
            }
        }
    }

    double eqBandDb (const EqBand& band, double freq, double sampleRate) noexcept
    {
        if (! band.active)
            return 0.0;
        double db = 0.0;
        for (int s = 0; s < eqSections (band); ++s)
            db += svfMagnitudeDb (eqSectionCoeffs (band, s, sampleRate), freq, sampleRate);
        return db;
    }

    double eqCurveDb (const EqBands& bands, double freq, double sampleRate) noexcept
    {
        double sum = 0.0;
        for (const auto& b : bands)
            sum += eqBandDb (b, freq, sampleRate);
        return sum;
    }
}
