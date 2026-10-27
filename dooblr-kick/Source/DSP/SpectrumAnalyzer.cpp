#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    SpectrumAnalyzer::SpectrumAnalyzer()
        : window ((size_t) fftSize), history ((size_t) fftSize, 0.0f), fftData ((size_t) fftSize * 2, 0.0f),
          live ((size_t) numBins, floorDb), peak ((size_t) numBins, floorDb)
    {
        double sum = 0.0;

        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
            sum += window[(size_t) i];
        }

        scale = (float) (2.0 / sum); // a full-scale sine reads 0 dBFS
    }

    void SpectrumAnalyzer::reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);
        std::fill (live.begin(), live.end(), floorDb);
        writePosition = samplesSinceFrame = filled = 0;
        maxLiveDb = floorDb;
    }

    void SpectrumAnalyzer::clearPeakHold() noexcept
    {
        std::fill (peak.begin(), peak.end(), floorDb);
    }

    int SpectrumAnalyzer::push (const float* samples, int numSamples) noexcept
    {
        auto frames = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            history[(size_t) writePosition] = samples[i];
            writePosition = (writePosition + 1) % fftSize;
            filled = std::min (fftSize, filled + 1);

            if (++samplesSinceFrame >= hopSize && filled == fftSize)
            {
                samplesSinceFrame = 0;
                analyse();
                ++frames;
            }
        }

        return frames;
    }

    void SpectrumAnalyzer::analyse() noexcept
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = history[(size_t) ((writePosition + i) % fftSize)] * window[(size_t) i];

        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        auto maxDb = floorDb;

        for (int b = 0; b < numBins; ++b)
        {
            const auto magnitude = fftData[(size_t) b] * scale;
            const auto db = magnitude > 1.0e-6f ? 20.0f * std::log10 (magnitude) : floorDb;
            auto& l = live[(size_t) b];
            l = std::max (l, db);
            peak[(size_t) b] = std::max (peak[(size_t) b], db);
            maxDb = std::max (maxDb, l);
        }

        maxLiveDb = maxDb;
        ++framesAnalysed;
    }

    void SpectrumAnalyzer::decay (double seconds) noexcept
    {
        if (isSilent())
            return;

        const auto drop = (float) (releaseDbPerSecond * std::max (0.0, seconds));
        auto maxDb = floorDb;

        for (auto& l : live)
        {
            l = std::max (floorDb, l - drop);
            maxDb = std::max (maxDb, l);
        }

        maxLiveDb = maxDb;
    }

    float SpectrumAnalyzer::levelAt (const std::vector<float>& binsDb, float frequency, float octaveSpan) const noexcept
    {
        const auto binHz = (float) (sampleRate / fftSize);
        const auto half = std::exp2 (0.5f * octaveSpan);
        const auto lo = frequency / half / binHz, hi = frequency * half / binHz;

        if (hi - lo < 1.0f)
        {
            const auto position = std::clamp (frequency / binHz, 0.0f, (float) (numBins - 2));
            const auto index = (int) position;
            const auto t = position - (float) index;
            return binsDb[(size_t) index] + t * (binsDb[(size_t) index + 1] - binsDb[(size_t) index]);
        }

        const auto first = std::clamp ((int) std::ceil (lo), 0, numBins - 1);
        const auto last = std::clamp ((int) std::floor (hi), first, numBins - 1);
        auto level = floorDb;

        for (int b = first; b <= last; ++b)
            level = std::max (level, binsDb[(size_t) b]);

        return level;
    }
}
