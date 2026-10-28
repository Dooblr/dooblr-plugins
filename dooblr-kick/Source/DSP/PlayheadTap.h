#pragma once

#include <atomic>
#include <cstdint>

namespace dooblrkick::dsp
{
    /**
        v0.3.4: the live hit's position, from the audio thread to the editor's envelope graphs.

        One 64-bit atomic carries a consistent snapshot: the hit number (low 16 bits of a counter
        that goes up on every note-on, so a retrigger is visible even at the same position),
        whether that hit is still sounding, and the samples elapsed since it started. The audio
        thread publishes once per block; the UI reads on VBlank. Lock free, no allocation.
    */
    class PlayheadTap
    {
    public:
        struct Snapshot
        {
            std::uint32_t hit = 0;    // 0 .. 65535 (wraps)
            bool active = false;
            std::int64_t samples = 0; // since the hit started
        };

        static constexpr int sampleBits = 47;
        static constexpr std::int64_t maxSamples = (std::int64_t (1) << sampleBits) - 1;

        void setSampleRate (double newRate) noexcept { sampleRate.store (newRate > 0.0 ? newRate : 48000.0); }
        double getSampleRate() const noexcept { return sampleRate.load(); }

        void publish (std::uint32_t hit, bool active, std::int64_t samples) noexcept { packed.store (pack (hit, active, samples)); }
        Snapshot read() const noexcept { return unpack (packed.load()); }

        static std::uint64_t pack (std::uint32_t hit, bool active, std::int64_t samples) noexcept
        {
            const auto s = (std::uint64_t) (samples < 0 ? 0 : (samples > maxSamples ? maxSamples : samples));
            return ((std::uint64_t) (hit & 0xffffu) << 48) | ((active ? 1ull : 0ull) << sampleBits) | s;
        }

        static Snapshot unpack (std::uint64_t v) noexcept
        {
            return { (std::uint32_t) (v >> 48), ((v >> sampleBits) & 1u) != 0, (std::int64_t) (v & (std::uint64_t) maxSamples) };
        }

    private:
        std::atomic<std::uint64_t> packed { 0 };
        std::atomic<double> sampleRate { 48000.0 };

        static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "the playhead hand-off must be lock free");
        static_assert (std::atomic<double>::is_always_lock_free, "the playhead hand-off must be lock free");
    };
}
