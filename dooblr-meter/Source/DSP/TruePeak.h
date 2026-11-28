#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dooblrmeter::dsp
{
    /** True-peak detector (ITU-R BS.1770-4 Annex 2 method): polyphase interpolation, then the
        largest absolute interpolated value. 4x below 88.2 kHz, 2x below 176.4 kHz, 1x above
        (the effective rate is always at least 176.4 kHz). Measurement only: no audio latency. */
    class TruePeak
    {
    public:
        static constexpr int tapsPerPhase = 16;

        void prepare (double sampleRate);
        void reset() noexcept;
        int getFactor() const noexcept { return factor; }

        /** Pushes one sample and returns the largest |interpolated| value for it (linear). */
        inline float push (int channel, float sample) noexcept
        {
            auto& st = state[(size_t) channel];
            st.pos = (st.pos == 0 ? tapsPerPhase : st.pos) - 1;
            st.ring[(size_t) st.pos] = sample;
            st.ring[(size_t) (st.pos + tapsPerPhase)] = sample;
            const float* x = st.ring.data() + st.pos;

            float peak = 0.0f;
            for (int p = 0; p < factor; ++p)
            {
                const float* h = phases.data() + p * tapsPerPhase;
                float acc = 0.0f;
                for (int k = 0; k < tapsPerPhase; ++k)
                    acc += h[k] * x[k];
                peak = std::max (peak, std::abs (acc));
            }
            return peak;
        }

    private:
        struct State
        {
            std::array<float, 2 * tapsPerPhase> ring {};
            int pos = 0;
        };

        int factor = 4;
        std::vector<float> phases;   // factor x tapsPerPhase
        std::array<State, 2> state {};
    };
}
