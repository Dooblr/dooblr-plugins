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
    };

    /** Attack (linear) / hold / curved decay to zero. */
    struct AhdEnvelope
    {
        float attackMs = 0.0f, holdMs = 0.0f, decayMs = 100.0f, curve = 0.5f;

        float getLengthMs() const noexcept { return attackMs + holdMs + decayMs; }
        float evaluate (float timeMs) const noexcept;
    };
}
