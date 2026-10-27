#include "MeterTap.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        float toDb (float gain) noexcept { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; }
    }

    MeterTap::MeterTap()
        : spectrum ((size_t) spectrumCapacity, 0.0f), frames ((size_t) frameCapacity)
    {
    }

    void MeterTap::prepare (double newSampleRate) noexcept
    {
        sampleRate.store (newSampleRate, std::memory_order_relaxed);
        const auto perFrame = (int) std::lround (newSampleRate * 0.005 / 32.0) * 32;
        samplesPerFrame.store (std::max (32, perFrame), std::memory_order_relaxed);
        accumulated = {};
        accumulatedSamples = 0;
    }

    void MeterTap::pushSpectrum (const float* left, const float* right, int numSamples) noexcept
    {
        int start1, size1, start2, size2;
        spectrumFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        auto write = [&] (int start, int size, int offset)
        {
            for (int i = 0; i < size; ++i)
                spectrum[(size_t) (start + i)] = 0.5f * (left[offset + i] + right[offset + i]);
        };

        write (start1, size1, 0);
        write (start2, size2, size1);
        spectrumFifo.finishedWrite (size1 + size2);
        spectrumWritten.fetch_add (size1 + size2, std::memory_order_relaxed);
    }

    void MeterTap::addBlock (const MeterBlock& b, int numSamples) noexcept
    {
        auto& a = accumulated;
        a.dynamicsIn = std::max (a.dynamicsIn, b.dynamicsIn);
        a.compressorIn = std::max (a.compressorIn, b.compressorIn);
        a.dynamicsOut = std::max (a.dynamicsOut, b.dynamicsOut);
        a.compressorGrDb = std::min (a.compressorGrDb, b.compressorGrDb);
        a.limiterIn = std::max (a.limiterIn, b.limiterIn);
        a.limiterOut = std::max (a.limiterOut, b.limiterOut);
        a.limiterGrDb = std::min (a.limiterGrDb, b.limiterGrDb);
        a.gateIn = std::max (a.gateIn, b.gateIn);
        a.gateOut = std::max (a.gateOut, b.gateOut);
        a.gateGrDb = std::min (a.gateGrDb, b.gateGrDb);
        a.output = std::max (a.output, b.output);
        accumulatedSamples += numSamples;

        if (accumulatedSamples < samplesPerFrame.load (std::memory_order_relaxed))
            return;

        MeterFrame frame;
        frame.dynamicsInDb = toDb (a.dynamicsIn);
        frame.compressorInDb = toDb (a.compressorIn);
        frame.dynamicsOutDb = toDb (a.dynamicsOut);
        frame.compressorGrDb = a.compressorGrDb;
        frame.limiterInDb = toDb (a.limiterIn);
        frame.limiterOutDb = toDb (a.limiterOut);
        frame.limiterGrDb = a.limiterGrDb;
        frame.gateInDb = toDb (a.gateIn);
        frame.gateOutDb = toDb (a.gateOut);
        frame.gateGrDb = a.gateGrDb;
        frame.outputDb = toDb (a.output);
        accumulated = {};
        accumulatedSamples = 0;

        int start1, size1, start2, size2;
        frameFifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            frames[(size_t) start1] = frame;
            frameFifo.finishedWrite (1);
            framesWritten.fetch_add (1, std::memory_order_relaxed);
        }
    }

    int MeterTap::popSpectrum (float* destination, int maxSamples) noexcept
    {
        int start1, size1, start2, size2;
        spectrumFifo.prepareToRead (maxSamples, start1, size1, start2, size2);
        std::copy_n (spectrum.data() + start1, size1, destination);
        std::copy_n (spectrum.data() + start2, size2, destination + size1);
        spectrumFifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    int MeterTap::popFrames (MeterFrame* destination, int maxFrames) noexcept
    {
        int start1, size1, start2, size2;
        frameFifo.prepareToRead (maxFrames, start1, size1, start2, size2);
        std::copy_n (frames.data() + start1, size1, destination);
        std::copy_n (frames.data() + start2, size2, destination + size1);
        frameFifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    void MeterTap::discardAll() noexcept
    {
        spectrumFifo.finishedRead (spectrumFifo.getNumReady());
        frameFifo.finishedRead (frameFifo.getNumReady());
    }
}
