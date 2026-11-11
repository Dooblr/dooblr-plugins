#include "PitchTracker.h"

#include <algorithm>

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    void PitchTracker::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        buffer.assign ((size_t) windowSize, 0.0f);
        reset();
    }

    void PitchTracker::reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        write = hopCount = 0;
        lp = hp = lpState = hpState = 0.0f;
        smoothedHz = rawHz = confidence = 0.0f;
    }

    float PitchTracker::process (float sample, float crossoverHz, float trackingSpeed01) noexcept
    {
        if (buffer.empty())
            return smoothedHz;

        const auto lpHz = juce::jlimit (40.0f, 180.0f, crossoverHz * 1.6f);
        const auto lpCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * lpHz / (float) sampleRate);
        const auto hpCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 18.0f / (float) sampleRate);

        lpState += lpCoeff * (sample - lpState);
        hpState += hpCoeff * (lpState - hpState);
        const auto band = lpState - hpState;

        buffer[(size_t) write] = band;
        write = (write + 1) % windowSize;
        if (++hopCount >= hopSize)
        {
            hopCount = 0;
            const auto minHz = 28.0f;
            const auto maxHz = juce::jlimit (50.0f, 140.0f, crossoverHz * 1.8f);
            analyse (minHz, maxHz);
        }

        const auto speed = juce::jlimit (0.0f, 1.0f, trackingSpeed01);
        // Faster tracking: shorter smoothing time (about 8 ms .. 180 ms).
        const auto smoothHz = juce::jmap (speed, 8.0f, 180.0f);
        const auto a = 1.0f - std::exp (-(float) hopSize / ((float) sampleRate * (smoothHz * 0.001f)));
        if (rawHz > 1.0f)
            smoothedHz += a * (rawHz - smoothedHz);
        else
            smoothedHz += 0.08f * (0.0f - smoothedHz);

        return smoothedHz;
    }

    void PitchTracker::analyse (float minHz, float maxHz) noexcept
    {
        const auto minLag = juce::jlimit (8, windowSize / 4, (int) std::floor (sampleRate / (double) maxHz));
        const auto maxLag = juce::jlimit (minLag + 4, windowSize / 2 - 2, (int) std::ceil (sampleRate / (double) minHz));

        // buffer[write] is the next slot to overwrite, i.e. the oldest sample.
        auto sampleAt = [this] (int ageFromOldest) -> float
        {
            return buffer[(size_t) ((write + ageFromOldest) % windowSize)];
        };

        const auto n = windowSize - maxLag;
        if (n < 64)
        {
            rawHz = 0.0f;
            confidence = 0.0f;
            return;
        }

        // Energy; silence is unvoiced.
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
            energy += (double) sampleAt (i) * (double) sampleAt (i);
        if (energy < 1.0e-10)
        {
            rawHz = 0.0f;
            confidence *= 0.8f;
            return;
        }

        int bestLag = minLag;
        double best = -1.0e9;
        double acf0 = energy;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i < n; ++i)
                acc += (double) sampleAt (i) * (double) sampleAt (i + lag);
            const auto norm = acc / (acf0 + 1.0e-20);
            if (norm > best)
            {
                best = norm;
                bestLag = lag;
            }
        }

        // Parabolic interpolation around the peak lag.
        auto acfAt = [&] (int lag) -> double
        {
            lag = juce::jlimit (minLag, maxLag, lag);
            double acc = 0.0;
            for (int i = 0; i < n; ++i)
                acc += (double) sampleAt (i) * (double) sampleAt (i + lag);
            return acc / (acf0 + 1.0e-20);
        };

        const auto ym1 = acfAt (bestLag - 1);
        const auto y0 = acfAt (bestLag);
        const auto yp1 = acfAt (bestLag + 1);
        auto peakLag = (double) bestLag;
        const auto denom = ym1 - 2.0 * y0 + yp1;
        if (std::abs (denom) > 1.0e-12)
            peakLag += 0.5 * (ym1 - yp1) / denom;

        const auto hz = (float) (sampleRate / juce::jmax (2.0, peakLag));
        confidence = juce::jlimit (0.0f, 1.0f, (float) y0);
        rawHz = (y0 > 0.25 && hz >= minHz * 0.85f && hz <= maxHz * 1.15f) ? hz : 0.0f;
    }
}
