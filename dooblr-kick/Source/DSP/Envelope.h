#pragma once

#include <array>

namespace dooblrkick::dsp
{
    constexpr int maxEnvelopeNodes = 8;

    /** Shapes a 0..1 ramp. curve > 0 moves fast at the start (punchy drops), curve < 0 moves
        late, 0 is linear. curve is clamped to [-1, 1]. */
    float applyCurve (float x, float curve) noexcept;

    struct EnvelopeNode
    {
        float timeMs = 0.0f;
        float value = 0.0f;
        float curve = 0.0f; // shape of the segment from this node to the next
    };

    /**
        Multi-point breakpoint envelope (pitch or amplitude). Node 0 always sits at 0 ms;
        times are forced non-decreasing. Values are interpolated in whatever domain the caller
        stores (the pitch envelope stores log2 Hz so sweeps are exponential in frequency).
        After the last node the envelope holds the last value.
    */
    struct MultiPointEnvelope
    {
        std::array<EnvelopeNode, maxEnvelopeNodes> nodes {};
        int numNodes = 2;

        void sanitise() noexcept;
        float getEndTimeMs() const noexcept { return nodes[(size_t) (numNodes - 1)].timeMs; }
        float getEndValue() const noexcept { return nodes[(size_t) (numNodes - 1)].value; }
        float evaluate (float timeMs) const noexcept;

        /** True when every active node sits at this value (the envelope is a constant). */
        bool isFlatAt (float value) const noexcept
        {
            for (int i = 0; i < numNodes; ++i)
                if (nodes[(size_t) i].value < value || nodes[(size_t) i].value > value)
                    return false;
            return true;
        }

        /** v0.3.6 layer amp envelopes: the envelope up to its last node, then the last value, held
            while a gated note sustains, otherwise faded out over 10 ms. */
        float evaluateLevel (float timeMs, bool sustain) const noexcept
        {
            const auto end = getEndTimeMs();

            if (timeMs <= end)
                return evaluate (timeMs);

            if (sustain)
                return getEndValue();

            const auto fade = 1.0f - (timeMs - end) / endFadeMs;
            return fade > 0.0f ? getEndValue() * fade : 0.0f;
        }

        static constexpr float endFadeMs = 10.0f;
    };

    /** Attack (linear) / hold / curved decay to zero. (v0.3.6: no layer uses it any more; every
        layer has a multi-point amp envelope whose default reproduces its old AHD shape. Kept as a
        reference shape for the tests.) */
    struct AhdEnvelope
    {
        float attackMs = 0.0f, holdMs = 0.0f, decayMs = 100.0f, curve = 0.5f;

        float getLengthMs() const noexcept { return attackMs + holdMs + decayMs; }
        float evaluate (float timeMs) const noexcept;
    };
}
