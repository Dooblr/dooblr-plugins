#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrreverb::dsp
{
    /**
        Mono samples from the audio thread to the analyzer (single producer, single consumer,
        lock free, no allocation after construction). The audio thread pushes what it has room
        for and drops the rest; the editor drains on VBlank, or discards when it is not showing.
    */
    class SpectrumTap
    {
    public:
        static constexpr int capacity = 1 << 15;

        SpectrumTap() : fifo (capacity), buffer ((size_t) capacity, 0.0f) {}

        void push (const float* samples, int numSamples) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
            if (size1 > 0) std::copy (samples, samples + size1, buffer.begin() + start1);
            if (size2 > 0) std::copy (samples + size1, samples + size1 + size2, buffer.begin() + start2);
            fifo.finishedWrite (size1 + size2);
        }

        /** Calls fn (const float*, int) with the waiting samples, in order; returns the count. */
        template <typename Fn>
        int drain (Fn&& fn)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
            if (size1 > 0) fn (buffer.data() + start1, size1);
            if (size2 > 0) fn (buffer.data() + start2, size2);
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

        void discardAll() { fifo.finishedRead (fifo.getNumReady()); }

    private:
        juce::AbstractFifo fifo;
        std::vector<float> buffer;
    };
}
