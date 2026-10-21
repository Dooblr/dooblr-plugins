#pragma once

#include <cmath>
#include <complex>

/**
    Topology-preserving-transform state-variable filter (Andrew Simper / Cytomic,
    "Linear Trapezoidal Integrated SVF"). It is the bilinear transform of the
    analogue prototype H(s) = (m0 (s^2 + k s + 1) + m1 s + m2) / (s^2 + k s + 1),
    stays stable for any positive g and k, and tolerates coefficient changes
    while running, which is why it is used for the modulated dynamic bands.
*/
namespace dooblrq::dsp
{
    enum class FilterType
    {
        bell = 0,
        lowShelf,
        highShelf,
        lowCut,
        highCut,
        notch,
        tilt   // appended last: saved sessions store the type index, so the order is fixed
    };

    inline constexpr int numFilterTypes = 7;

    /** Dynamics are meaningful only for types that have a gain. */
    constexpr bool supportsDynamics (FilterType type) noexcept
    {
        return type == FilterType::bell || type == FilterType::lowShelf || type == FilterType::highShelf
            || type == FilterType::tilt;
    }

    constexpr bool hasGain (FilterType type) noexcept
    {
        return supportsDynamics (type);
    }

    /** Stereo width applies to the band's own region for types that shape a region
        (bell: around the centre, shelves: the shelved side, tilt: above the pivot).
        Cuts and notches remove content rather than shape it, so width does not apply. */
    constexpr bool supportsWidth (FilterType type) noexcept
    {
        return supportsDynamics (type);
    }

    struct SvfCoefficients
    {
        // Prototype parameters, kept for analysing the response.
        double g = 0.0, k = 1.0;
        // Per-sample processing coefficients.
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        // Output mix of (input, band-pass, low-pass).
        float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    };

    struct SvfState
    {
        float ic1eq = 0.0f, ic2eq = 0.0f;

        void reset() noexcept { ic1eq = ic2eq = 0.0f; }
    };

    /** Coefficients for an EQ band. Frequency is clamped below Nyquist. */
    SvfCoefficients makeEqCoefficients (FilterType type, double frequency, double q,
                                        double gainDb, double sampleRate) noexcept;

    /** Unity-peak side-chain detector coefficients for a band: band-pass around bells
        and notches, low-pass under a low shelf, high-pass above a high shelf, and
        broadband (unfiltered) for a tilt, so a dynamic tilt follows the overall level. */
    SvfCoefficients makeDetectorCoefficients (FilterType type, double frequency, double q,
                                              double sampleRate) noexcept;

    /** Side-signal region filter for stereo width: unity-peak band-pass (bell, Q of the
        band), Butterworth low-pass (low shelf) or high-pass (high shelf, tilt) at the band
        frequency. Width w turns the side signal S into S + (w - 1) * region(S). */
    SvfCoefficients makeWidthRegionCoefficients (FilterType type, double frequency, double q,
                                                 double sampleRate) noexcept;

    /** Analogue prototype of a band: H(s) = m0 + (m1 s + m2) / (s^2 + k s + 1) with
        s = j f / (frequency * scale). */
    struct AnalogPrototype
    {
        double scale = 1.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    AnalogPrototype makeEqPrototype (FilterType type, double q, double gainDb) noexcept;
    AnalogPrototype makeWidthRegionPrototype (FilterType type, double q) noexcept;

    /** Response of the analogue prototype (no bilinear warping, so no cramping near
        Nyquist). This is the target of the Natural and Linear phase modes. */
    std::complex<double> analogResponseAt (const AnalogPrototype& p, double bandFrequency, double frequency) noexcept;

    /** Exact complex response of the digital filter at a frequency. */
    std::complex<double> responseAt (const SvfCoefficients& c, double frequency, double sampleRate) noexcept;

    inline double magnitudeAt (const SvfCoefficients& c, double frequency, double sampleRate) noexcept
    {
        return std::abs (responseAt (c, frequency, sampleRate));
    }

    inline float processSvf (float input, SvfState& s, const SvfCoefficients& c) noexcept
    {
        const auto v3 = input - s.ic2eq;
        const auto v1 = c.a1 * s.ic1eq + c.a2 * v3;
        const auto v2 = s.ic2eq + c.a2 * s.ic1eq + c.a3 * v3;
        s.ic1eq = 2.0f * v1 - s.ic1eq;
        s.ic2eq = 2.0f * v2 - s.ic2eq;
        return c.m0 * input + c.m1 * v1 + c.m2 * v2;
    }
}
