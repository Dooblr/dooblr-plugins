#pragma once

#include <atomic>
#include <utility>

namespace dooblrvocals::dsp
{
    /**
        v0.3.4: the final output's peak for the header meter. The audio thread raises a per-channel
        maximum once per block (after the Output gain: the samples the host gets); the editor takes
        and clears it on VBlank, so no peak (or clip) between two frames is missed. Lock free.
    */
    class OutputPeakTap
    {
    public:
        void push (float left, float right) noexcept
        {
            raise (peaks[0], left);
            raise (peaks[1], right);
        }

        /** The peaks (linear gain) since the last take; resets them. */
        std::pair<float, float> take() noexcept { return { peaks[0].exchange (0.0f), peaks[1].exchange (0.0f) }; }

    private:
        static void raise (std::atomic<float>& peak, float value) noexcept
        {
            if (! (value == value))
                return; // NaN

            auto current = peak.load (std::memory_order_relaxed);
            while (value > current && ! peak.compare_exchange_weak (current, value)) {}
        }

        std::atomic<float> peaks[2] { 0.0f, 0.0f };

        static_assert (std::atomic<float>::is_always_lock_free, "the output meter hand-off must be lock free");
    };
}
