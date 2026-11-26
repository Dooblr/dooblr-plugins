#pragma once

#include <array>
#include <cmath>

#include "../Parameters.h"

namespace dooblrreverb::dsp
{
    /**
        A topology-preserving-transform state-variable filter (Simper / Zavalishin), the suite's
        modulation-safe filter: coefficients can change every sample without clicks or blow-ups.
        Output = m0 * input + m1 * band + m2 * low; every 2nd-order shape used here is one choice
        of (g, k, m0, m1, m2).
    */
    enum class SvfType { bell, lowShelf, highShelf, lowPass, highPass, notch };

    struct SvfCoeffs
    {
        float g = 0.0f, k = 1.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        double freq = 1000.0, gScale = 1.0; // for the analogue response (resolution independent)

        void update() noexcept
        {
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
    };

    /** sampleRate <= 0: analogue prototype (g is the prewarped value for 48 kHz, unused). */
    SvfCoeffs makeSvf (SvfType type, double freq, double sampleRate, double q, double gainDb) noexcept;

    /** |H| in dB at a frequency: digital (the exact response of the TPT filter) when
        sampleRate > 0, else the analogue prototype. */
    double svfMagnitudeDb (const SvfCoeffs& c, double freq, double sampleRate) noexcept;

    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;

        inline float process (float v0, const SvfCoeffs& c) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = c.a1 * ic1 + c.a2 * v3;
            const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };

    //==============================================================================
    /** One Decay Rate EQ band: a multiplier on the decay time around a frequency. */
    struct DecayBand
    {
        bool active = false;
        params::DecayShape shape = params::DecayShape::bell;
        float freq = 1000.0f, rate = 100.0f, q = 1.0f;

        bool operator== (const DecayBand& o) const noexcept
        {
            return active == o.active && shape == o.shape && juce::exactlyEqual (freq, o.freq) && juce::exactlyEqual (rate, o.rate) && juce::exactlyEqual (q, o.q);
        }
        bool operator!= (const DecayBand& o) const noexcept { return ! (*this == o); }
    };
    using DecayBands = std::array<DecayBand, params::numDecayBands>;

    /** The band's contribution, as log2 of the decay multiplier (+1 = twice as long). */
    double decayBandLog2 (const DecayBand& band, double freq) noexcept;
    /** All bands: log2 of the multiplier, clamped to the editable range (12.5 % .. 400 %). */
    double decayCurveLog2 (const DecayBands& bands, double freq) noexcept;

    //==============================================================================
    /** One Post EQ band. */
    struct EqBand
    {
        bool active = false;
        params::EqShape shape = params::EqShape::bell;
        float freq = 1000.0f, gain = 0.0f, q = 0.71f;
        int slope = 1;

        bool operator== (const EqBand& o) const noexcept
        {
            return active == o.active && shape == o.shape && juce::exactlyEqual (freq, o.freq) && juce::exactlyEqual (gain, o.gain) && juce::exactlyEqual (q, o.q) && slope == o.slope;
        }
        bool operator!= (const EqBand& o) const noexcept { return ! (*this == o); }
    };
    using EqBands = std::array<EqBand, params::numEqBands>;

    /** Coefficients of one section of a band (cuts cascade 1, 2 or 4 sections; Butterworth Qs
        scaled by the band's Q, so Q 0.71 is a plain Butterworth cut). */
    SvfCoeffs eqSectionCoeffs (const EqBand& band, int section, double sampleRate) noexcept;
    int eqSections (const EqBand& band) noexcept;

    /** The band's response in dB (sampleRate <= 0: analogue). */
    double eqBandDb (const EqBand& band, double freq, double sampleRate) noexcept;
    double eqCurveDb (const EqBands& bands, double freq, double sampleRate) noexcept;

    inline constexpr double log2Of10Over20 = 0.16609640474436813; // log2 (10) / 20: dB -> log2 (amplitude)
}
