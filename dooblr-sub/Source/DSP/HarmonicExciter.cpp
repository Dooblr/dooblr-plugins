#include "HarmonicExciter.h"

#include <cmath>

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    void HarmonicExciter::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        const auto twoPi = 6.28318530718f;
        lpA = 1.0f - std::exp (-twoPi * 90.0f / (float) sampleRate);
        hpA = 1.0f - std::exp (-twoPi * 70.0f / (float) sampleRate);
        smoothA = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
        reset();
    }

    void HarmonicExciter::reset() noexcept
    {
        amountZ = targetAmount;
        lpL = lpR = hpL = hpR = 0.0f;
    }

    void HarmonicExciter::process (float& left, float& right) noexcept
    {
        amountZ += smoothA * (targetAmount - amountZ);
        if (amountZ < 1.0e-5f && targetAmount < 1.0e-5f)
            return;

        const auto drive = 1.0f + amountZ * 6.5f;
        const auto norm = std::tanh (drive);
        const auto mix = amountZ * 1.35f;

        auto excite = [&] (float x, float& lp, float& hp) noexcept
        {
            lp += lpA * (x - lp);
            const auto odd = std::tanh (lp * drive) / juce::jmax (1.0e-6f, norm);
            // Even term puts a 2nd harmonic on the sub (50 Hz → 100 Hz) so small speakers hear it.
            const auto even = lp * std::abs (lp);
            const auto residual = (odd - lp) + 0.85f * even;
            hp += hpA * (residual - hp);
            return mix * (residual - hp);
        };

        left += excite (left, lpL, hpL);
        right += excite (right, lpR, hpR);
    }
}
