#pragma once

#include <vector>

namespace dooblrsub::dsp
{
    /**
        Polarity and a short delay (0–12 ms) on the generated sub, so it can sit in
        phase with the source. Neutral (no invert, 0 ms) is a no-op (bit-identical).
        Delay time and polarity are smoothed.
    */
    class PhaseAlign
    {
    public:
        static constexpr float maxDelayMs = 12.0f;

        void prepare (double sampleRateToUse);
        void reset() noexcept;
        void set (bool invert, float delayMs) noexcept;

        float process (float generated) noexcept;

    private:
        double sampleRate = 44100.0;
        std::vector<float> delay;
        int write = 0, delaySize = 1;
        float targetPolarity = 1.0f, polarityZ = 1.0f;
        float targetDelaySamp = 0.0f, delayZ = 0.0f;
        float smoothA = 0.02f;
    };
}
