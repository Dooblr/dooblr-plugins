#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "Effects.h"

namespace dooblrvocals::dsp
{
    enum ChainStage { gateStage = 0, eqStage, dynamicsStage, limiterStage, numChainStages };
    inline constexpr int numChainOrders = 24; // 4! orders of Gate, EQ, Comp, Limiter

    enum class CorrectionMode { keyed = 0, chromatic };
    enum class QualityMode { live = 0, hq };
    enum class InputType { soprano = 0, alto, tenor, baritone, bass, numTypes };
    enum class ScaleType
    {
        major = 0, minor, dorian, phrygian, lydian, mixolydian, locrian,
        harmonicMinor, melodicMinor, custom, numTypes
    };

    /** Pitch-class bitmask: bit 0 = C, bit 1 = C#, ... bit 11 = B. Degrees are from C. */
    inline constexpr int maskFromDegrees (int b0, int b1, int b2, int b3, int b4, int b5, int b6) noexcept
    {
        return (1 << b0) | (1 << b1) | (1 << b2) | (1 << b3) | (1 << b4) | (1 << b5) | (1 << b6);
    }

    inline constexpr std::array<int, (int) ScaleType::numTypes> scaleMasks {{
        maskFromDegrees (0, 2, 4, 5, 7, 9, 11), // major
        maskFromDegrees (0, 2, 3, 5, 7, 8, 10), // natural minor
        maskFromDegrees (0, 2, 3, 5, 7, 9, 10), // dorian
        maskFromDegrees (0, 1, 3, 5, 7, 8, 10), // phrygian
        maskFromDegrees (0, 2, 4, 6, 7, 9, 11), // lydian
        maskFromDegrees (0, 2, 4, 5, 7, 9, 10), // mixolydian
        maskFromDegrees (0, 1, 3, 5, 6, 8, 10), // locrian
        maskFromDegrees (0, 2, 3, 5, 7, 8, 11), // harmonic minor
        maskFromDegrees (0, 2, 3, 5, 7, 9, 11), // melodic minor (ascending)
        maskFromDegrees (0, 2, 4, 5, 7, 9, 11)  // custom default (major)
    }};

    inline int maskForScale (ScaleType scale) noexcept
    {
        const auto i = (int) scale;
        return scaleMasks[(size_t) std::clamp (i, 0, (int) ScaleType::custom)];
    }

    /** Rotate a C-rooted mask so bit 0 is the given key (0 = C). */
    inline int rotateMask (int mask, int key) noexcept
    {
        key = ((key % 12) + 12) % 12;
        const auto twelve = mask & 0x0fff;
        return key == 0 ? twelve : ((twelve << key) | (twelve >> (12 - key))) & 0x0fff;
    }

    inline int effectiveMask (CorrectionMode mode, ScaleType scale, int key, int customMask) noexcept
    {
        if (mode == CorrectionMode::chromatic)
            return 0x0fff;

        if (scale == ScaleType::custom)
        {
            const auto m = customMask & 0x0fff;
            return m == 0 ? 0x0fff : m; // empty custom mask would snap nowhere
        }

        return rotateMask (maskForScale (scale), key);
    }

    struct PitchRange
    {
        float minHz = 82.0f, maxHz = 1100.0f;
    };

    inline PitchRange rangeFor (InputType type) noexcept
    {
        switch (type)
        {
            case InputType::soprano:  return { 196.0f, 1100.0f };
            case InputType::alto:     return { 175.0f, 700.0f };
            case InputType::tenor:    return { 130.0f, 530.0f };
            case InputType::baritone: return { 98.0f, 400.0f };
            case InputType::bass:     return { 82.0f, 330.0f };
            default:                  return { 82.0f, 1100.0f };
        }
    }

    /** Lowest F0 the live detector must cover (below bass E2), used to size the delay. */
    inline constexpr float liveMinHz = 70.0f;

    inline int liveLatencySamples (double sampleRate) noexcept
    {
        const auto grain = (int) std::ceil (2.0 * sampleRate / liveMinHz);
        int n = 1;
        while (n < grain)
            n <<= 1;
        return n;
    }

    inline int hqLatencySamples (double sampleRate) noexcept
    {
        return liveLatencySamples (sampleRate) * 4;
    }

    struct PitchSettings
    {
        bool enabled = true;
        CorrectionMode mode = CorrectionMode::keyed;
        int key = 0;                 // 0 = C .. 11 = B
        ScaleType scale = ScaleType::major;
        int customMask = maskFromDegrees (0, 2, 4, 5, 7, 9, 11);
        float retuneMs = 20.0f;      // 0 = instant
        float humanize = 0.15f;      // 0 .. 1
        float amount = 1.0f;         // 0 = dry delayed, 1 = fully corrected
        float a4Hz = 440.0f;
        bool formant = true;
        InputType input = InputType::tenor;
        QualityMode quality = QualityMode::live;
    };

    struct VocalSettings
    {
        PitchSettings pitch;
        GateSettings gate;
        EqSettings eq;
        CompressorSettings compressor;
        LimiterSettings limiter;
        int chainOrder = 0;
        bool nonRealtime = false;
    };
}
