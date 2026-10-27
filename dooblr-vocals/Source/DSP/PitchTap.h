#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrvocals::dsp
{
    /** One pitch reading for the editor (about one detector hop). */
    struct PitchFrame
    {
        float detectedHz = 0.0f;
        float correctedHz = 0.0f;
        float confidence = 0.0f;
        bool voiced = false;
    };

    /**
        Audio thread -> editor feed for the pitch display.
        Single-producer / single-consumer lock-free FIFO; the audio thread never blocks.
    */
    class PitchTap
    {
    public:
        static constexpr int capacity = 1 << 11;

        PitchTap() : frames ((size_t) capacity) {}

        void push (const PitchFrame& frame) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (1, start1, size1, start2, size2);

            if (size1 > 0)
            {
                frames[(size_t) start1] = frame;
                fifo.finishedWrite (1);
                written.fetch_add (1, std::memory_order_relaxed);
            }
        }

        int pop (PitchFrame* destination, int maxFrames) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (maxFrames, start1, size1, start2, size2);
            std::copy_n (frames.data() + start1, size1, destination);
            std::copy_n (frames.data() + start2, size2, destination + size1);
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

        void discardAll() noexcept { fifo.finishedRead (fifo.getNumReady()); }
        juce::int64 getFramesWritten() const noexcept { return written.load (std::memory_order_relaxed); }

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<PitchFrame> frames;
        std::atomic<juce::int64> written { 0 };
    };
}
