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
        attenDb.reset (sampleRate, smoothingSeconds);
        colour.prepare (sampleRate);
        crossfadeLength = std::max (1, (int) std::lround (topologyFadeSeconds * sampleRate));

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
        attenDb.setCurrentAndTargetValue (attenDb.getTargetValue());
        colour.reset();
    }

    void DynamicEQBand::resetFilterState() noexcept
    {
        for (auto& cascade : filterState) for (auto& state : cascade) state.reset();
        for (auto& cascade : fadingState) for (auto& state : cascade) state.reset();
        crossfadeRemaining = 0;
        appliedType = settings.type;
        appliedSlope = settings.slope;
        appliedModel = settings.model;
        for (auto& state : detectorState) state.reset();
        envelope.fill (0.0f);
        dynamicGainDb.fill (0.0f);
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
            attenDb.setCurrentAndTargetValue (settings.attenDb);
            appliedType = settings.type;
            appliedSlope = settings.slope;
            appliedModel = settings.model;
            crossfadeRemaining = 0;
        }
        else
        {
            frequency.setTargetValue (settings.frequency);
            q.setTargetValue (settings.q);
            gainDb.setTargetValue (settings.gainDb);
            rangeDb.setTargetValue (settings.rangeDb);
            thresholdDb.setTargetValue (settings.thresholdDb);
            wet.setTargetValue (targetWet);
            attenDb.setTargetValue (settings.attenDb);
        }

        colour.setTarget (colourFor (settings.enabled ? settings.model : Model::digital), settings.driveDb, snap);

        updateBallistics();
    }

    void DynamicEQBand::updateBallistics() noexcept
    {
        attackCoefficient  = onePoleCoefficient (settings.attackMs, sampleRate);
        releaseCoefficient = onePoleCoefficient (settings.releaseMs, sampleRate);
    }

    void DynamicEQBand::process (float* const* channels, int numChannels, int startSample, int numSamples,
                                 bool stereoLinked, bool applyColour) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        if (numSamples <= 0 || numChannels <= 0)
            return;

        const auto currentFrequency = (double) frequency.skip (numSamples);
        const auto currentQ         = (double) q.skip (numSamples);
        const auto currentGainDb    = gainDb.skip (numSamples);
        const auto currentRangeDb   = rangeDb.skip (numSamples);
        const auto currentThreshold = thresholdDb.skip (numSamples);
        const auto currentAtten     = (double) attenDb.skip (numSamples);

        const auto fading = wet.isSmoothing();

        if (! fading && wet.getTargetValue() < 0.5f)
        {
            // Fully disabled: cost nothing, and start from clean state when re-enabled.
            if (active)
            {
                resetFilterState();
                active = false;
            }
            appliedType = settings.type; // nothing to crossfade from while silent
            appliedSlope = settings.slope;
            appliedModel = settings.model;
            if (applyColour)
                colour.process (channels, numChannels, startSample, numSamples); // fading out
            return;
        }

        active = true;

        // A new type or slope starts a crossfade from the running cascade (one at a time;
        // a further change waits for the current crossfade to finish).
        if (crossfadeRemaining <= 0
            && (settings.type != appliedType
                || settings.slope != appliedSlope
                || settings.model != appliedModel))
        {
            fadingType = appliedType;
            fadingSlope = appliedSlope;
            fadingModel = appliedModel;
            fadingState = filterState;
            for (auto& cascade : filterState) for (auto& state : cascade) state.reset();
            appliedType = settings.type;
            appliedSlope = settings.slope;
            appliedModel = settings.model;
            crossfadeRemaining = crossfadeLength;
        }

        const auto type = appliedType;
        const auto crossfading = crossfadeRemaining > 0;
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
        std::array<BandCoefficients, maxChannels> coefficients;
        std::array<BandCoefficients, maxChannels> fadingCoefficients;

        for (int i = 0; i < numCoefficientSets; ++i)
        {
            const auto totalGain = std::clamp (currentGainDb + dynamicGainDb[(size_t) i],
                                               -maxTotalGainDb, maxTotalGainDb);
            makeModelCoefficients (appliedModel, type, appliedSlope, currentFrequency, currentQ,
                                   (double) totalGain, currentAtten, sampleRate, coefficients[(size_t) i]);
            if (crossfading)
                makeModelCoefficients (fadingModel, fadingType, fadingSlope, currentFrequency, currentQ,
                                       (double) (hasGain (fadingType) ? totalGain : currentGainDb),
                                       currentAtten, sampleRate, fadingCoefficients[(size_t) i]);
        }

        const auto runCascade = [] (float input, CascadeState& state, const BandCoefficients& c) noexcept
        {
            auto x = input;
            for (int s = 0; s < c.numSections; ++s)
                x = processSvf (x, state[(size_t) s], c.sections[(size_t) s]);
            return x;
        };

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
            float newWeight = 1.0f;

            if (crossfadeRemaining > 0)
            {
                // Equal-gain (linear) crossfade: the two cascades are highly correlated.
                newWeight = 1.0f - (float) crossfadeRemaining / (float) crossfadeLength;
                --crossfadeRemaining;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = channels[ch][n];
                const auto set = (size_t) (perChannelGain ? ch : 0);
                auto filtered = runCascade (sample, filterState[(size_t) ch], coefficients[set]);

                if (crossfading)
                {
                    const auto old = runCascade (sample, fadingState[(size_t) ch], fadingCoefficients[set]);
                    filtered = old + newWeight * (filtered - old);
                }

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

        // Output stage colour of the model (transformers / amplifiers), after the filter.
        if (applyColour)
            colour.process (channels, numChannels, startSample, numSamples);
    }
}
