#include "DepthCurve.h"

#include <cmath>

namespace dooblrsoothe::dsp
{
    namespace
    {
        using params::Shape;

        float octavesFrom (float frequency, float centre) noexcept
        {
            return std::log2 (std::fmax (frequency, 1.0f) / std::fmax (centre, 1.0f));
        }

        /** Gaussian on a log-frequency axis; Q 1 is about one octave wide at half height. */
        float bellShape (float octaves, float q) noexcept
        {
            const auto sigma = 0.45f / std::fmax (q, 0.05f);
            const auto x = octaves / sigma;
            return std::exp (-0.5f * x * x);
        }

        /** 1 below the corner, 0 above, a logistic transition (Q sets the steepness). */
        float lowShelfShape (float octaves, float q) noexcept
        {
            const auto k = 2.0f + 6.0f * q;
            return 1.0f / (1.0f + std::exp (k * octaves));
        }

        /** Cut mask, Butterworth-like magnitude; order from Q (Q 1 = 3rd order, about 18 dB/oct). */
        float cutMask (float octaves, float q, bool lowCut) noexcept
        {
            const auto order = 0.5f + 2.5f * q;
            const auto ratio = std::exp2 ((lowCut ? -octaves : octaves) * order * 2.0f);
            return 1.0f / std::sqrt (1.0f + ratio);
        }

        /** Flat-topped band-pass region, half width 1 / Q octaves. */
        float bandPassShape (float octaves, float q) noexcept
        {
            const auto halfWidth = 1.0f / std::fmax (q, 0.05f);
            const auto x = std::fabs (octaves) / halfWidth;
            const auto x2 = x * x;
            return 1.0f / (1.0f + x2 * x2 * x2);
        }

        float contributionDb (const Band& b, float octaves) noexcept
        {
            switch (b.shape)
            {
                case Shape::bell:      return b.gain * bellShape (octaves, b.q);
                case Shape::lowShelf:  return b.gain * lowShelfShape (octaves, b.q);
                case Shape::highShelf: return b.gain * lowShelfShape (-octaves, b.q);
                case Shape::bandPass:  return b.gain * bandPassShape (octaves, b.q);
                case Shape::lowCut:
                case Shape::highCut:   break;
            }
            return 0.0f;
        }

        float toDb (float amount, float floorDb) noexcept
        {
            return amount <= 0.0f ? floorDb : std::fmax (floorDb, 20.0f * std::log10 (amount));
        }
    }

    float curveAmount (const Bands& bands, float frequency) noexcept
    {
        float db = 0.0f, mask = 1.0f, passUnion = 0.0f;
        bool anyPass = false;

        for (const auto& b : bands)
        {
            if (! b.active)
                continue;

            const auto octaves = octavesFrom (frequency, b.freq);
            db += contributionDb (b, octaves);

            if (b.shape == Shape::lowCut || b.shape == Shape::highCut)
                mask *= cutMask (octaves, b.q, b.shape == Shape::lowCut);
            else if (b.shape == Shape::bandPass)
            {
                anyPass = true;
                passUnion = std::fmax (passUnion, bandPassShape (octaves, b.q));
            }
        }

        if (anyPass)
            mask *= passUnion;

        // Gain reads like an EQ: below 0 dB means more reduction, above means less.
        return std::pow (10.0f, -db / 20.0f) * mask;
    }

    float curveDb (const Bands& bands, float frequency, float ceilingDb) noexcept
    {
        return -toDb (curveAmount (bands, frequency), -ceilingDb);
    }

    float bandDb (const Band& b, float frequency, float ceilingDb) noexcept
    {
        const auto octaves = octavesFrom (frequency, b.freq);
        if (b.shape == Shape::lowCut || b.shape == Shape::highCut)
            return -toDb (cutMask (octaves, b.q, b.shape == Shape::lowCut), -ceilingDb);
        return contributionDb (b, octaves);
    }

    float bandRegion (const Band& b, float frequency) noexcept
    {
        const auto octaves = octavesFrom (frequency, b.freq);
        switch (b.shape)
        {
            case Shape::bell:      return bellShape (octaves, b.q);
            case Shape::lowShelf:  return lowShelfShape (octaves, b.q);
            case Shape::highShelf: return lowShelfShape (-octaves, b.q);
            case Shape::lowCut:    return cutMask (octaves, b.q, true);
            case Shape::highCut:   return cutMask (octaves, b.q, false);
            case Shape::bandPass:  return bandPassShape (octaves, b.q);
        }
        return 0.0f;
    }
}
