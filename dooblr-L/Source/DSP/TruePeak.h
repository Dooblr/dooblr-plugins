#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace dooblrl::dsp
{
    /** 4x true-peak reconstruction (windowed-sinc polyphase interpolator).

        Used for metering and for Auto Comp (catch inter-sample overs only while they last).
        Coefficients are computed once in prepare; audio-thread process is allocation-free.
    */
    class TruePeak
    {
    public:
        static constexpr int factor = 4;
        static constexpr int tapsPerPhase = 12;
        static constexpr int taps = factor * tapsPerPhase; // 48

        void prepare (double sampleRate);
        void reset() noexcept;

        /** Push one sample per channel; returns the max abs of the reconstructed 4x points
            covering this input sample. */
        float pushAndTruePeak (int channel, float sample) noexcept;

        /** True peak of a buffer. Resets the interpolator first. */
        float measureBuffer (const float* data, int numSamples, int channel = 0);

        /** Reconstruct 4x true-peak of a ring buffer whose newest sample is at `newest`. */
        float peakFromRing (const float* ring, int ringSize, int newest) const noexcept;

        static constexpr int latency() noexcept { return tapsPerPhase - 1; }
        int latencySamples() const noexcept { return latency(); }

    private:
        void design();

        std::array<float, taps> fir {};
        std::array<std::array<float, tapsPerPhase>, 2> z {};
        std::array<int, 2> write { 0, 0 };
        bool designed = false;
    };

    float linearToDb (float linear) noexcept;
    float dbToLinear (float db) noexcept;
}
