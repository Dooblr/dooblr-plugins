#pragma once

#include <array>

#include "DynamicEQBand.h"
#include "SvfFilter.h"

/**
    Band Solo (0.9.1): audition only the frequency region a band works on.

    The stage runs after the EQ (and after the Natural / Linear phase FIR), so it is IIR,
    adds no latency in any phase mode and works the same for every model. With no band
    soloed it is an exact pass-through. While any band is soloed the output is the sum,
    over the soloed bands, of the EQ'd signal through that band's region filter:

      Bell, Notch     band-pass at the band's frequency, -3 dB at the band's Width edges
                      (two sections whose cascade has the band's Q), so the notched region
                      of a notch is what you hear.
      Low Shelf       low-pass at the shelf midpoint (the side the shelf moves).
      High Shelf      high-pass at the shelf midpoint.
      Tilt            the side the tilt raises: high-pass at the pivot for a positive tilt,
                      low-pass for a negative one.
      Low / High Cut  band-pass around the cutoff (Q at least 1, about 1.9 octaves), at the
                      live cutoff while dynamics move it.

    Every region filter is unity gain at its centre (band-pass) or in its passband, so the
    soloed region plays at the level the EQ gives it: comparable to the normal output.
    Region filters are 4th order (24 dB/oct skirts).

    Switching is click-free: each band's contribution and the dry/solo mix ramp linearly
    over fadeSeconds, and region frequency / Q glide (one-pole, glideSeconds) when a soloed
    band is moved.
*/
namespace dooblrq::dsp
{
    enum class SoloShape { bandPass, lowPass, highPass };

    struct SoloRegion
    {
        SoloShape shape = SoloShape::bandPass;
        double frequency = 1000.0;
        double q = 0.707;
    };

    /** The region a band's solo plays; dynamicGainDb moves a dynamic cut's cutoff. */
    SoloRegion soloRegionFor (const BandSettings& settings, double dynamicGainDb) noexcept;

    struct SoloCoefficients
    {
        std::array<SvfCoefficients, 2> sections {};
    };

    SoloCoefficients makeSoloCoefficients (const SoloRegion& region, double sampleRate) noexcept;

    /** Linear magnitude of a region filter (for the display and the tests). */
    double soloMagnitudeAt (const SoloRegion& region, double frequency, double sampleRate) noexcept;

    class SoloStage
    {
    public:
        static constexpr int maxBands = 6;
        static constexpr int maxChannels = 2;
        static constexpr int updateInterval = 16;
        static constexpr double fadeSeconds = 0.02;
        static constexpr double glideSeconds = 0.01;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        /** Called once per block for every band (audio thread). */
        void setBand (int bandIndex, bool soloed, const SoloRegion& region) noexcept;

        /** In place on the EQ output. */
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        /** True while any band is soloed or a fade is still running. */
        bool isActive() const noexcept;

        /** Current dry/solo mix (0 = normal output, 1 = solo only). */
        float getMix() const noexcept { return mix; }

    private:
        struct Band
        {
            bool target = false, running = false;
            float weight = 0.0f;
            SoloRegion region;
            double logFrequency = 0.0, q = 0.707;
            SoloShape shape = SoloShape::bandPass;
            SoloCoefficients coefficients;
            std::array<std::array<SvfState, 2>, maxChannels> state {};
        };

        void updateCoefficients (Band& band, bool snap) noexcept;

        std::array<Band, maxBands> bands;
        float mix = 0.0f, step = 1.0f / 960.0f;
        double sampleRate = 48000.0, glide = 0.3;
    };
}
