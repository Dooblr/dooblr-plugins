#include "PhaseAlign.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    void PhaseAlign::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        const auto maxSamp = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4;
        delay.assign ((size_t) juce::jmax (8, maxSamp), 0.0f);
        delaySize = (int) delay.size();
        smoothA = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
        reset();
    }

    void PhaseAlign::reset() noexcept
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        write = 0;
        polarityZ = targetPolarity;
        delayZ = targetDelaySamp;
    }

    void PhaseAlign::set (bool invert, float delayMs) noexcept
    {
        targetPolarity = invert ? -1.0f : 1.0f;
        targetDelaySamp = juce::jlimit (0.0f, (float) delaySize - 3.0f,
                                        delayMs * 0.001f * (float) sampleRate);
    }

    float PhaseAlign::process (float generated) noexcept
    {
        polarityZ += smoothA * (targetPolarity - polarityZ);
        delayZ += smoothA * (targetDelaySamp - delayZ);

        if (delay.empty())
            return generated * polarityZ;

        delay[(size_t) write] = generated;

        const auto idle = std::abs (polarityZ - 1.0f) < 1.0e-5f && delayZ < 1.0e-4f
                          && std::abs (targetPolarity - 1.0f) < 1.0e-5f && targetDelaySamp < 1.0e-4f;
        if (idle)
        {
            write = (write + 1) % delaySize;
            return generated;
        }

        float y = generated;
        if (delayZ >= 1.0e-4f)
        {
            auto pos = (float) write - delayZ;
            while (pos < 0.0f)
                pos += (float) delaySize;
            const auto i0 = (int) pos;
            const auto i1 = i0 + 1 >= delaySize ? 0 : i0 + 1;
            const auto f = pos - (float) i0;
            y = delay[(size_t) i0] * (1.0f - f) + delay[(size_t) i1] * f;
        }

        write = (write + 1) % delaySize;
        return y * polarityZ;
    }
}
