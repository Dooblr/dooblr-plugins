#pragma once

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrl::dsp
{
    /** One history column: peaks over about 5 ms, plus the deepest GR in that window.
        min/max are the input waveform extrema (linear); peaks are max-abs linear. GR is
        positive dB. */
    struct HistoryFrame
    {
        float inMin = 0, inMax = 0, inPeak = 0, outPeak = 0, grDb = 0;
    };

    /** Single-producer / single-consumer lock-free FIFO of HistoryFrames.

        Storage is allocated in the constructor. The audio thread never blocks or allocates;
        when the FIFO is full (no editor is reading) new frames are dropped.
    */
    class HistoryTap
    {
    public:
        static constexpr int capacity = 1 << 11;

        HistoryTap();

        /** Not concurrent with the audio thread (prepareToPlay). */
        void prepare (double sampleRate) noexcept;

        // Audio thread ---------------------------------------------------------
        void addBlock (const HistoryFrame& block, int numSamples) noexcept;

        // Consumer (editor / tests) --------------------------------------------
        int popFrames (HistoryFrame* destination, int maxFrames) noexcept;
        void discardAll() noexcept;

        int getSamplesPerFrame() const noexcept { return samplesPerFrame.load (std::memory_order_relaxed); }
        juce::int64 getFramesWritten() const noexcept { return framesWritten.load (std::memory_order_relaxed); }

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<HistoryFrame> frames;

        HistoryFrame accumulated;
        int accumulatedSamples = 0;
        std::atomic<int> samplesPerFrame { 256 };
        std::atomic<juce::int64> framesWritten { 0 };
    };
}
