#pragma once

#include "Biquad.h"

namespace dooblrmeter::dsp
{
    /** ITU-R BS.1770-4/5 K-weighting: the high-shelf "pre-filter" followed by the RLB high-pass.
        At 48 kHz the published coefficients are used verbatim; at other rates the analog
        prototypes are bilinear-transformed (same f0 / Q / gain as the 48 kHz design). */
    struct KWeighting
    {
        Biquad shelf, rlb;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept { shelf.reset(); rlb.reset(); }
        inline double process (double x) noexcept { return rlb.process (shelf.process (x)); }

        /** Combined magnitude response in dB (used by tests and the translation model). */
        double magnitudeDb (double hz, double sampleRate) const noexcept
        {
            return shelf.magnitudeDb (hz, sampleRate) + rlb.magnitudeDb (hz, sampleRate);
        }
    };

    /** BS.1770 loudness of a mean-square sum (channel weights already applied). */
    inline double meanSquareToLufs (double weightedMeanSquare) noexcept
    {
        return weightedMeanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (weightedMeanSquare) : -150.0;
    }

    inline double lufsToMeanSquare (double lufs) noexcept
    {
        return std::pow (10.0, (lufs + 0.691) / 10.0);
    }
}
