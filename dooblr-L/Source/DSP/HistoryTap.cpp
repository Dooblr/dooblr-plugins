#include "HistoryTap.h"

#include <algorithm>
#include <cmath>

namespace dooblrl::dsp
{
    HistoryTap::HistoryTap()
        : frames ((size_t) capacity)
    {
        accumulated.inMin = 1.0e6f;
        accumulated.inMax = -1.0e6f;
    }

    void HistoryTap::prepare (double sampleRate) noexcept
    {
        const auto perFrame = (int) std::lround (sampleRate * 0.005 / 32.0) * 32;
        samplesPerFrame.store (std::max (32, perFrame), std::memory_order_relaxed);
        accumulated = {};
        accumulated.inMin = 1.0e6f;
        accumulated.inMax = -1.0e6f;
        accumulatedSamples = 0;
        discardAll();
    }

    void HistoryTap::addBlock (const HistoryFrame& b, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        const auto perFrame = samplesPerFrame.load (std::memory_order_relaxed);
        auto remaining = numSamples;
        while (remaining > 0)
        {
            auto& a = accumulated;
            a.inMin = std::min (a.inMin, b.inMin);
            a.inMax = std::max (a.inMax, b.inMax);
            a.inPeak = std::max (a.inPeak, b.inPeak);
            a.outPeak = std::max (a.outPeak, b.outPeak);
            a.grDb = std::max (a.grDb, b.grDb);

            const auto take = std::min (remaining, perFrame - accumulatedSamples);
            accumulatedSamples += take;
            remaining -= take;

            if (accumulatedSamples < perFrame)
                return;

            const HistoryFrame frame = a;
            accumulated = {};
            accumulated.inMin = 1.0e6f;
            accumulated.inMax = -1.0e6f;
            accumulatedSamples = 0;

            int start1, size1, start2, size2;
            fifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                frames[(size_t) start1] = frame;
                fifo.finishedWrite (1);
                framesWritten.fetch_add (1, std::memory_order_relaxed);
            }
        }
    }

    int HistoryTap::popFrames (HistoryFrame* destination, int maxFrames) noexcept
    {
        if (destination == nullptr || maxFrames <= 0)
            return 0;

        int start1, size1, start2, size2;
        fifo.prepareToRead (maxFrames, start1, size1, start2, size2);
        std::copy_n (frames.data() + start1, size1, destination);
        std::copy_n (frames.data() + start2, size2, destination + size1);
        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    void HistoryTap::discardAll() noexcept
    {
        fifo.finishedRead (fifo.getNumReady());
    }
}
