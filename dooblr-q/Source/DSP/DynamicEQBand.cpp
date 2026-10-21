#include "DynamicEQBand.h"

#include <algorithm>
#include <cmath>

namespace dooblrq::dsp
{
    namespace
    {
        constexpr double smoothingSeconds = 0.03;
        constexpr double enableFadeSeconds = 0.01;
        constexpr float silenceDb = -120.0f;

        float onePoleCoefficient (float milliseconds, double sampleRate) noexcept
        {
            const auto samples = (double) milliseconds * 0.001 * sampleRate;
            return samples > 0.0 ? (float) std::exp (-1.0 / samples) : 0.0f;
        }
    }

    float DynamicEQBand::computeDynamicGainDb (float levelDb, float thresholdDb, float rangeDb) noexcept
    {
        const auto over = levelDb - thresholdDb;
        const auto halfKnee = 0.5f * kneeWidthDb;

        float overWithKnee = 0.0f;

        if (over >= halfKnee)
            overWithKnee = over;
        else if (over > -halfKnee)
            overWithKnee = (over + halfKnee) * (over + halfKnee) / (2.0f * kneeWidthDb);

        const auto magnitude = std::min (std::abs (rangeDb), dynamicSlope * overWithKnee);
        return rangeDb < 0.0f ? -magnitude : magnitude;
    }

    void DynamicEQBand::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        frequency.reset (sampleRate, smoothingSeconds);
        q.reset (sampleRate, smoothingSeconds);
        gainDb.reset (sampleRate, smoothingSeconds);
        rangeDb.reset (sampleRate, smoothingSeconds);
        thresholdDb.reset (sampleRate, smoothingSeconds);
        wet.reset (sampleRate, enableFadeSeconds);
        width.reset (sampleRate, smoothingSeconds);

        setSettings (settings, true);
        reset();
    }

    void DynamicEQBand::reset() noexcept
    {
        resetFilterState();
        frequency.setCurrentAndTargetValue (frequency.getTargetValue());
        q.setCurrentAndTargetValue (q.getTargetValue());
        gainDb.setCurrentAndTargetValue (gainDb.getTargetValue());
        rangeDb.setCurrentAndTargetValue (rangeDb.getTargetValue());
        thresholdDb.setCurrentAndTargetValue (thresholdDb.getTargetValue());
        wet.setCurrentAndTargetValue (wet.getTargetValue());
        width.setCurrentAndTargetValue (width.getTargetValue());
    }

    void DynamicEQBand::resetFilterState() noexcept
    {
        for (auto& state : filterState)   state.reset();
        for (auto& state : detectorState) state.reset();
        envelope.fill (0.0f);
        dynamicGainDb.fill (0.0f);
        widthState.reset();
        widthActive = false;
        dynamicGainForDisplay.store (0.0f, std::memory_order_relaxed);
    }

    void DynamicEQBand::setSettings (const BandSettings& newSettings, bool snap) noexcept
    {
        settings = newSettings;

        const auto targetWet = settings.enabled ? 1.0f : 0.0f;

        if (snap)
        {
            frequency.setCurrentAndTargetValue (settings.frequency);
            q.setCurrentAndTargetValue (settings.q);
            gainDb.setCurrentAndTargetValue (settings.gainDb);
            rangeDb.setCurrentAndTargetValue (settings.rangeDb);
            thresholdDb.setCurrentAndTargetValue (settings.thresholdDb);
            wet.setCurrentAndTargetValue (targetWet);
            width.setCurrentAndTargetValue (settings.widthPercent * 0.01f);
        }
        else
        {
            frequency.setTargetValue (settings.frequency);
            q.setTargetValue (settings.q);
            gainDb.setTargetValue (settings.gainDb);
            rangeDb.setTargetValue (settings.rangeDb);
            thresholdDb.setTargetValue (settings.thresholdDb);
            wet.setTargetValue (targetWet);
            width.setTargetValue (settings.widthPercent * 0.01f);
        }

        updateBallistics();
    }

    void DynamicEQBand::updateBallistics() noexcept
    {
        attackCoefficient  = onePoleCoefficient (settings.attackMs, sampleRate);
        releaseCoefficient = onePoleCoefficient (settings.releaseMs, sampleRate);
    }

    void DynamicEQBand::process (float* const* channels, int numChannels, int startSample, int numSamples,
                                 bool stereoLinked) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        if (numSamples <= 0 || numChannels <= 0)
            return;

        const auto currentFrequency = (double) frequency.skip (numSamples);
        const auto currentQ         = (double) q.skip (numSamples);
        const auto currentGainDb    = gainDb.skip (numSamples);
        const auto currentRangeDb   = rangeDb.skip (numSamples);
        const auto currentThreshold = thresholdDb.skip (numSamples);
        const auto widthSmoothing   = width.isSmoothing();
        const auto currentWidth     = width.skip (numSamples);

        const auto fading = wet.isSmoothing();

        if (! fading && wet.getTargetValue() < 0.5f)
        {
            // Fully disabled: cost nothing, and start from clean state when re-enabled.
            if (active)
            {
                resetFilterState();
                active = false;
            }
            return;
        }

        active = true;

        const auto type = settings.type;
        const auto dynamic = settings.dynamicsEnabled && supportsDynamics (type)
                             && (std::abs (currentRangeDb) > 1.0e-3f || rangeDb.isSmoothing());

        const auto linked = stereoLinked || numChannels == 1;
        const auto numDetectors = linked ? 1 : numChannels;
        const auto perChannelGain = dynamic && ! linked;
        const auto numCoefficientSets = perChannelGain ? numChannels : 1;

        if (! dynamic)
        {
            envelope.fill (0.0f);
            dynamicGainDb.fill (0.0f);
        }

        // Coefficients for this sub-block, using the dynamic gain measured up to its start.
        std::array<SvfCoefficients, maxChannels> coefficients;

        for (int i = 0; i < numCoefficientSets; ++i)
        {
            const auto totalGain = std::clamp (currentGainDb + dynamicGainDb[(size_t) i],
                                               -maxTotalGainDb, maxTotalGainDb);
            coefficients[(size_t) i] = makeEqCoefficients (type, currentFrequency, currentQ,
                                                           (double) totalGain, sampleRate);
        }

        // Stereo width: mid/side inside the band's region. Skipped entirely at exactly 100%,
        // so a neutral width leaves the signal path bit-identical.
        const auto useWidth = numChannels == 2 && supportsWidth (type)
                              && (widthSmoothing || ! juce::exactlyEqual (currentWidth, 1.0f));

        if (! useWidth && widthActive)
        {
            widthState.reset();
            widthActive = false;
        }

        widthActive = useWidth;
        const auto region = useWidth ? makeWidthRegionCoefficients (type, currentFrequency, currentQ, sampleRate)
                                     : SvfCoefficients {};
        const auto widthAmount = currentWidth - 1.0f;

        const auto detector = dynamic ? makeDetectorCoefficients (type, currentFrequency, currentQ, sampleRate)
                                      : SvfCoefficients {};

        for (int n = startSample; n < startSample + numSamples; ++n)
        {
            if (dynamic)
            {
                float linkedLevel = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto level = std::abs (processSvf (channels[ch][n], detectorState[(size_t) ch], detector));

                    if (linked)
                    {
                        linkedLevel = std::max (linkedLevel, level);
                    }
                    else
                    {
                        auto& env = envelope[(size_t) ch];
                        env = level + (level > env ? attackCoefficient : releaseCoefficient) * (env - level);
                    }
                }

                if (linked)
                {
                    auto& env = envelope[0];
                    env = linkedLevel + (linkedLevel > env ? attackCoefficient : releaseCoefficient) * (env - linkedLevel);
                }
            }

            const auto mix = fading ? wet.getNextValue() : 1.0f;

            if (useWidth)
            {
                auto& left = channels[0][n];
                auto& right = channels[1][n];
                const auto mid = 0.5f * (left + right);
                const auto side = 0.5f * (left - right);
                const auto newSide = side + mix * widthAmount * processSvf (side, widthState, region);
                left = mid + newSide;
                right = mid - newSide;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = channels[ch][n];
                const auto filtered = processSvf (sample, filterState[(size_t) ch],
                                                  coefficients[(size_t) (perChannelGain ? ch : 0)]);
                sample = fading ? sample + mix * (filtered - sample) : filtered;
            }
        }

        if (dynamic)
        {
            float displayGain = 0.0f;

            for (int i = 0; i < numDetectors; ++i)
            {
                const auto levelDb = juce::Decibels::gainToDecibels (envelope[(size_t) i], silenceDb);
                dynamicGainDb[(size_t) i] = computeDynamicGainDb (levelDb, currentThreshold, currentRangeDb);

                if (std::abs (dynamicGainDb[(size_t) i]) > std::abs (displayGain))
                    displayGain = dynamicGainDb[(size_t) i];
            }

            dynamicGainForDisplay.store (displayGain, std::memory_order_relaxed);
        }
        else
        {
            dynamicGainForDisplay.store (0.0f, std::memory_order_relaxed);
        }
    }
}
