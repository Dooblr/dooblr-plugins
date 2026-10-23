#pragma once

#include <array>
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


    /** Per-band slope (choice index of bN_slope, appended in 0.7.0). The value is the
        number of cascaded second-order sections minus one, except Brickwall. */
    enum class Slope
    {
        db12 = 0,
        db24,
        db36,
        db48,
        db60,
        brickwall
    };

    inline constexpr int numSlopes = 6;

    /** Brickwall = a 32nd-order Butterworth (16 sections): about 192 dB/oct asymptotically,
        -3 dB at the corner and about -26 dB only 10 % past it. */
    inline constexpr int brickwallSections = 16;
    inline constexpr int maxSections = brickwallSections;

    /** Slope changes the response of cuts, shelves and tilt (they become higher-order
        Butterworth designs). A bell's and a notch's shape is set by Q/Width alone. */
    constexpr bool slopeApplies (FilterType type) noexcept
    {
        return type != FilterType::bell && type != FilterType::notch;
    }

    constexpr int sectionsForSlope (Slope slope) noexcept
    {
        return slope == Slope::brickwall ? brickwallSections : (int) slope + 1;
    }

    constexpr int numSectionsFor (FilterType type, Slope slope) noexcept
    {
        return slopeApplies (type) ? sectionsForSlope (slope) : 1;
    }

    /** Asymptotic roll-off of a cut at this slope, dB per octave. */
    constexpr int slopeDbPerOctave (Slope slope) noexcept
    {
        return 12 * sectionsForSlope (slope);
    }

    /** Q of section i (0-based, ascending Q) of an order-2n Butterworth filter. */
    double butterworthSectionQ (int numSections, int index) noexcept;

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

    /** Analogue prototype of a band: H(s) = m0 + (m1 s + m2) / (s^2 + k s + 1) with
        s = j f / (frequency * scale). */
    struct AnalogPrototype
    {
        double scale = 1.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    AnalogPrototype makeEqPrototype (FilterType type, double q, double gainDb) noexcept;

    /**
        A band as a cascade of second-order sections (1 for 12 dB/oct and for bells/notches).

        Cuts: Butterworth sections (product of the section levels at the corner = 0.707, so
        the corner is -3 dB at every order). The band's Q is applied as resonance to the
        lowest-Q section (Q_section * Q / 0.707), so Q 0.707 is the plain Butterworth and
        the level at the corner is always 20 log10 (Q) dB, as for the 12 dB/oct filter.
        Shelves / tilt: Butterworth shelving of order 2n (zeros and poles on Butterworth
        circles a gain-dependent ratio apart), so the transition gets steeper (up to 6 dB
        per order per octave) while the midpoint stays at half the gain. Q again scales
        the lowest-Q section. With one section every type equals makeEqPrototype exactly.
    */
    struct BandPrototype
    {
        std::array<AnalogPrototype, maxSections> sections {};
        int numSections = 1;
    };

    BandPrototype makeBandPrototype (FilterType type, Slope slope, double q, double gainDb) noexcept;

    struct BandCoefficients
    {
        std::array<SvfCoefficients, maxSections> sections {};
        int numSections = 1;
    };

    /** Digital sections of a band (same prewarp and clamping as makeEqCoefficients). */
    BandCoefficients makeBandCoefficients (FilterType type, Slope slope, double frequency, double q,
                                           double gainDb, double sampleRate) noexcept;
    void makeBandCoefficients (const BandPrototype& prototype, double frequency, double sampleRate,
                               BandCoefficients& destination) noexcept;

    /** Response of the analogue prototype (no bilinear warping, so no cramping near
        Nyquist). This is the target of the Natural and Linear phase modes. */
    std::complex<double> analogResponseAt (const AnalogPrototype& p, double bandFrequency, double frequency) noexcept;

    std::complex<double> analogResponseAt (const BandPrototype& p, double bandFrequency, double frequency) noexcept;

    /** Exact complex response of the digital filter at a frequency. */
    std::complex<double> responseAt (const SvfCoefficients& c, double frequency, double sampleRate) noexcept;

    inline double magnitudeAt (const SvfCoefficients& c, double frequency, double sampleRate) noexcept
    {
        return std::abs (responseAt (c, frequency, sampleRate));
    }

    std::complex<double> responseAt (const BandCoefficients& c, double frequency, double sampleRate) noexcept;

    inline double magnitudeAt (const BandCoefficients& c, double frequency, double sampleRate) noexcept
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
