#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    float applyCurve (float x, float curve) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        curve = std::clamp (curve, -1.0f, 1.0f);

        if (std::abs (curve) < 1.0e-3f)
            return x;

        const auto k = 7.0f * curve;
        return (1.0f - std::exp (-k * x)) / (1.0f - std::exp (-k));
    }

    void MultiPointEnvelope::sanitise() noexcept
    {
        numNodes = std::clamp (numNodes, 2, maxEnvelopeNodes);
        nodes[0].timeMs = 0.0f;

        for (int i = 1; i < numNodes; ++i)
            nodes[(size_t) i].timeMs = std::max (nodes[(size_t) i].timeMs, nodes[(size_t) i - 1].timeMs);
    }

    float MultiPointEnvelope::evaluate (float timeMs) const noexcept
    {
        if (timeMs <= 0.0f)
            return nodes[0].value;

        for (int i = 0; i < numNodes - 1; ++i)
        {
            const auto& a = nodes[(size_t) i];
            const auto& b = nodes[(size_t) i + 1];

            if (timeMs < b.timeMs)
            {
                const auto span = b.timeMs - a.timeMs;

                if (span <= 1.0e-6f)
                    return b.value;

                const auto x = (timeMs - a.timeMs) / span;
                return a.value + (b.value - a.value) * applyCurve (x, a.curve);
            }
        }

        return getEndValue();
    }

    float AhdEnvelope::evaluate (float timeMs) const noexcept
    {
        if (timeMs < 0.0f)
            return 0.0f;

        if (timeMs < attackMs)
            return timeMs / attackMs;

        timeMs -= attackMs;

        if (timeMs < holdMs)
            return 1.0f;

        timeMs -= holdMs;

        if (timeMs >= decayMs)
            return 0.0f;

        return 1.0f - applyCurve (timeMs / std::max (decayMs, 1.0e-3f), curve);
    }
}
