#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dooblrq::analysis
{
    //==============================================================================
    AnalyzerFifo::AnalyzerFifo()
        : storage ((size_t) capacity, 0.0f)
    {
    }

    void AnalyzerFifo::push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numSamples <= 0 || numChannels <= 0)
            return;

        const auto toWrite = std::min (numSamples, fifo.getFreeSpace());

        if (toWrite < numSamples)
            dropped.fetch_add (numSamples - toWrite, std::memory_order_relaxed);

        if (toWrite <= 0)
            return;

        const auto scale = 1.0f / (float) numChannels;
        const auto scope = fifo.write (toWrite);

        const auto copyRange = [&] (int destinationStart, int count, int sourceOffset)
        {
            auto* destination = storage.data() + destinationStart;

            for (int i = 0; i < count; ++i)
            {
                auto sum = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                    sum += channels[ch][sourceOffset + i];

                destination[i] = sum * scale;
            }
        };

        copyRange (scope.startIndex1, scope.blockSize1, 0);
        copyRange (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    int AnalyzerFifo::pop (float* dest, int maxSamples) noexcept
    {
        const auto toRead = std::min (maxSamples, fifo.getNumReady());

        if (toRead <= 0)
            return 0;

        const auto scope = fifo.read (toRead);
        std::memcpy (dest, storage.data() + scope.startIndex1, sizeof (float) * (size_t) scope.blockSize1);
        std::memcpy (dest + scope.blockSize1, storage.data() + scope.startIndex2, sizeof (float) * (size_t) scope.blockSize2);
        return toRead;
    }

    void AnalyzerFifo::discardAll() noexcept
    {
        const auto ready = fifo.getNumReady();

        if (ready > 0)
            fifo.finishedRead (ready);
    }

    //==============================================================================
    SpectrumAnalyzer::SpectrumAnalyzer()
        : window ((size_t) fftSize),
          input ((size_t) fftSize, 0.0f),
          fftData ((size_t) fftSize * 2, 0.0f),
          scratch ((size_t) hopSize, 0.0f),
          latestDb ((size_t) numBins, floorDb),
          liveDb ((size_t) numBins, floorDb),
          averageDb ((size_t) numBins, floorDb),
          peakDb ((size_t) numBins, floorDb),
          powerSum ((size_t) numBins, 0.0)
    {
        // Periodic Hann window: its coherent gain (sum) is exactly fftSize / 2.
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    }

    void SpectrumAnalyzer::setSampleRate (double newSampleRate)
    {
        if (newSampleRate > 0.0 && std::abs (newSampleRate - sampleRate) > 0.5)
        {
            sampleRate = newSampleRate;
            resetAll();
        }
    }

    void SpectrumAnalyzer::resetAverage() noexcept
    {
        std::fill (powerSum.begin(), powerSum.end(), 0.0);
        std::fill (averageDb.begin(), averageDb.end(), floorDb);
        framesAccumulated = 0;
        samplesSinceAverageStart = 0;
    }

    void SpectrumAnalyzer::resetPeak() noexcept
    {
        std::fill (peakDb.begin(), peakDb.end(), floorDb);
        peakFrames = 0;
    }

    void SpectrumAnalyzer::freeze() noexcept
    {
        if (frozen)
            return;

        resetAverage(); // a fresh average from the moment Freeze is engaged
        frozen = true;
    }

    void SpectrumAnalyzer::unfreeze() noexcept
    {
        frozen = false;
        resetAverage(); // averaging stops; the next freeze() starts over
    }

    void SpectrumAnalyzer::resetAll()
    {
        std::fill (input.begin(), input.end(), 0.0f);
        inputFill = 0;
        std::fill (latestDb.begin(), latestDb.end(), floorDb);
        std::fill (liveDb.begin(), liveDb.end(), floorDb);
        resetAverage();
        resetPeak();
    }

    int SpectrumAnalyzer::pull (AnalyzerFifo& fifo)
    {
        auto frames = 0;

        for (;;)
        {
            const auto read = fifo.pop (scratch.data(), (int) scratch.size());

            if (read <= 0)
                break;

            frames += pushSamples (scratch.data(), read);
        }

        return frames;
    }

    int SpectrumAnalyzer::pushSamples (const float* samples, int numSamples)
    {
        auto frames = 0;

        while (numSamples > 0)
        {
            const auto count = std::min (numSamples, fftSize - inputFill);
            std::memcpy (input.data() + inputFill, samples, sizeof (float) * (size_t) count);
            inputFill += count;
            samplesSinceAverageStart += count;
            samples += count;
            numSamples -= count;

            if (inputFill == fftSize)
            {
                processFrame();
                ++frames;

                // Keep the newest (fftSize - hopSize) samples for the next, overlapping frame.
                std::memmove (input.data(), input.data() + hopSize, sizeof (float) * (size_t) (fftSize - hopSize));
                inputFill = fftSize - hopSize;
            }
        }

        return frames;
    }

    void SpectrumAnalyzer::processFrame()
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = input[(size_t) i] * window[(size_t) i];

        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // |X[k]| = A * coherentGain / 2 = A * fftSize / 4 for a sine of amplitude A.
        constexpr auto amplitudeScale = 4.0f / (float) fftSize;
        const auto frameSeconds = (float) hopSize / (float) sampleRate;
        const auto release = liveReleaseDbPerSecond * frameSeconds;
        const auto floorPower = std::pow (10.0, (double) floorDb / 10.0);
        // Average only frames played entirely after Freeze was engaged (or Clear while frozen).
        const auto addToAverage = frozen && samplesSinceAverageStart >= fftSize;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto amplitude = (double) (fftData[(size_t) bin] * amplitudeScale);
            const auto power = amplitude * amplitude;
            const auto db = power > floorPower ? (float) (10.0 * std::log10 (power)) : floorDb;
            const auto b = (size_t) bin;

            latestDb[b] = db;
            liveDb[b] = db > liveDb[b] ? db : std::max (db, liveDb[b] - release);

            if (addToAverage)
                powerSum[b] += power;

            peakDb[b] = std::max (peakDb[b], db);
        }

        ++peakFrames;

        if (addToAverage)
            ++framesAccumulated;
    }

    const std::vector<float>& SpectrumAnalyzer::getAverageDb()
    {
        if (framesAccumulated == 0)
        {
            std::fill (averageDb.begin(), averageDb.end(), floorDb);
            return averageDb;
        }

        const auto floorPower = std::pow (10.0, (double) floorDb / 10.0);

        for (size_t b = 0; b < averageDb.size(); ++b)
        {
            const auto mean = powerSum[b] / (double) framesAccumulated;
            averageDb[b] = mean > floorPower ? (float) (10.0 * std::log10 (mean)) : floorDb;
        }

        return averageDb;
    }

    double SpectrumAnalyzer::getAccumulatedSeconds() const noexcept
    {
        return (double) framesAccumulated * hopSize / sampleRate;
    }

    void SpectrumAnalyzer::clear()
    {
        resetAverage(); // empty anyway when not frozen
        resetPeak();
    }

    float SpectrumAnalyzer::levelAt (const std::vector<float>& binDb, double frequency, double rate,
                                     double octaveSpan) noexcept
    {
        const auto lastBin = (int) binDb.size() - 1;

        if (lastBin < 1 || frequency <= 0.0 || rate <= 0.0)
            return floorDb;

        const auto binWidth = rate / (double) ((lastBin) * 2);
        const auto halfSpan = std::pow (2.0, 0.5 * octaveSpan);
        const auto low  = std::max (1, (int) std::ceil (frequency / halfSpan / binWidth));
        const auto high = std::min (lastBin, (int) std::floor (frequency * halfSpan / binWidth));

        if (low <= high)
        {
            auto level = binDb[(size_t) low];

            for (int bin = low + 1; bin <= high; ++bin)
                level = std::max (level, binDb[(size_t) bin]);

            return level;
        }

        const auto position = std::clamp (frequency / binWidth, 1.0, (double) lastBin);
        const auto below = std::min ((int) position, lastBin - 1);
        const auto fraction = (float) (position - below);
        return binDb[(size_t) below] + fraction * (binDb[(size_t) below + 1] - binDb[(size_t) below]);
    }
}
