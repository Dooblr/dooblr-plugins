#include "SoloStage.h"

#include <algorithm>
#include <cmath>

namespace dooblrq::dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // Two identical band-pass sections whose cascade is -3 dB where a single section of
        // Q would be: each section's Q is Q * sqrt (sqrt 2 - 1).
        const double cascadeQScale = std::sqrt (std::sqrt (2.0) - 1.0);

        // 4th-order Butterworth section Qs.
        constexpr double butterworthQ[2] { 0.54119610014619701, 1.3065629648763766 };

        SvfCoefficients makeSection (SoloShape shape, double frequency, double q, double sampleRate) noexcept
        {
            const auto clamped = std::clamp (frequency, 5.0, sampleRate * 0.49);
            const auto g = std::tan (pi * clamped / sampleRate);
            const auto k = 1.0 / std::max (q, 0.05);
            const auto a1 = 1.0 / (1.0 + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;

            SvfCoefficients c;
            c.g = g;
            c.k = k;
            c.a1 = (float) a1;
            c.a2 = (float) a2;
            c.a3 = (float) a3;

            switch (shape)
            {
                case SoloShape::bandPass: c.m0 = 0.0f; c.m1 = (float) k;  c.m2 = 0.0f;  break; // unity peak
                case SoloShape::lowPass:  c.m0 = 0.0f; c.m1 = 0.0f;       c.m2 = 1.0f;  break;
                case SoloShape::highPass: c.m0 = 1.0f; c.m1 = (float) -k; c.m2 = -1.0f; break;
            }

            return c;
        }
    }

    SoloRegion soloRegionFor (const BandSettings& s, double dynamicGainDb) noexcept
    {
        SoloRegion r;
        r.frequency = (double) s.frequency;
        r.q = std::clamp ((double) s.q, 0.1, 40.0);

        switch (s.type)
        {
            case FilterType::bell:
            case FilterType::notch:
                r.shape = SoloShape::bandPass;
                break;

            case FilterType::lowShelf:  r.shape = SoloShape::lowPass;  break;
            case FilterType::highShelf: r.shape = SoloShape::highPass; break;
            case FilterType::tilt:      r.shape = s.gainDb >= 0.0f ? SoloShape::highPass : SoloShape::lowPass; break;

            case FilterType::lowCut:
            case FilterType::highCut:
                // What the cut takes away: below a Low Cut, above a High Cut, from the input.
                r.shape = s.type == FilterType::lowCut ? SoloShape::lowPass : SoloShape::highPass;
                r.fromInput = true;
                if (s.dynamicsEnabled && std::isfinite (dynamicGainDb))
                    r.frequency = DynamicEQBand::dynamicCutFrequency (s.type, r.frequency, dynamicGainDb);
                break;
        }

        return r;
    }

    SoloCoefficients makeSoloCoefficients (const SoloRegion& region, double sampleRate) noexcept
    {
        SoloCoefficients c;

        for (int i = 0; i < 2; ++i)
        {
            const auto q = region.shape == SoloShape::bandPass ? region.q * cascadeQScale : butterworthQ[i];
            c.sections[(size_t) i] = makeSection (region.shape, region.frequency, q, sampleRate);
        }

        return c;
    }

    double soloMagnitudeAt (const SoloRegion& region, double frequency, double sampleRate) noexcept
    {
        const auto c = makeSoloCoefficients (region, sampleRate);
        return magnitudeAt (c.sections[0], frequency, sampleRate) * magnitudeAt (c.sections[1], frequency, sampleRate);
    }

    //==============================================================================
    void SoloStage::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        step = (float) (1.0 / std::max (1.0, fadeSeconds * sampleRate));
        glide = 1.0 - std::exp (-(double) updateInterval / (glideSeconds * sampleRate));
        reset();
    }

    void SoloStage::reset() noexcept
    {
        mix = 0.0f;

        for (auto& band : bands)
        {
            band.running = false;
            band.weight = 0.0f;
            for (auto& channel : band.state)
                for (auto& section : channel)
                    section.reset();
        }

        // Targets stay: a reset while soloed snaps straight to solo.
        bool any = false;
        for (auto& band : bands)
        {
            if (band.target)
            {
                band.running = true;
                band.weight = 1.0f;
                updateCoefficients (band, true);
                any = true;
            }
        }

        mix = any ? 1.0f : 0.0f;
    }

    void SoloStage::setBand (int bandIndex, bool soloed, const SoloRegion& region) noexcept
    {
        if (bandIndex < 0 || bandIndex >= maxBands)
            return;

        auto& band = bands[(size_t) bandIndex];
        band.target = soloed;
        band.region = region;
    }

    bool SoloStage::isActive() const noexcept
    {
        if (mix > 0.0f)
            return true;

        for (const auto& band : bands)
            if (band.target || band.running)
                return true;

        return false;
    }

    void SoloStage::updateCoefficients (Band& band, bool snap) noexcept
    {
        const auto targetLog = std::log (std::max (1.0, band.region.frequency));

        if (snap || band.shape != band.region.shape || band.fromInput != band.region.fromInput)
        {
            band.fromInput = band.region.fromInput;
            band.logFrequency = targetLog;
            band.q = band.region.q;
            band.shape = band.region.shape;
        }
        else
        {
            band.logFrequency += (targetLog - band.logFrequency) * glide;
            band.q += (band.region.q - band.q) * glide;
        }

        SoloRegion current { band.shape, std::exp (band.logFrequency), band.q, band.fromInput };
        band.coefficients = makeSoloCoefficients (current, sampleRate);
    }

    void SoloStage::process (float* const* channels, const float* const* input, int numChannels, int numSamples) noexcept
    {
        if (! isActive())
            return; // exact pass-through

        numChannels = std::min (numChannels, maxChannels);
        bool anyTarget = false;

        for (auto& band : bands)
        {
            anyTarget = anyTarget || band.target;

            if (band.target && ! band.running)
            {
                // Starting from silence: fresh state, coefficients straight at the region.
                band.running = true;
                band.weight = 0.0f;
                for (auto& channel : band.state)
                    for (auto& section : channel)
                        section.reset();
                updateCoefficients (band, true);
            }
        }

        const auto mixTarget = anyTarget ? 1.0f : 0.0f;
        std::array<float, updateInterval> mixRamp {};
        std::array<std::array<float, updateInterval>, maxBands> weightRamp {};

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const auto length = std::min (updateInterval, numSamples - start);

            for (int n = 0; n < length; ++n)
            {
                mix = mix < mixTarget ? std::min (mixTarget, mix + step) : std::max (mixTarget, mix - step);
                mixRamp[(size_t) n] = mix;
            }

            for (size_t b = 0; b < bands.size(); ++b)
            {
                auto& band = bands[b];

                if (! band.running)
                    continue;

                updateCoefficients (band, false);
                const auto target = band.target ? 1.0f : 0.0f;

                for (int n = 0; n < length; ++n)
                {
                    band.weight = band.weight < target ? std::min (target, band.weight + step)
                                                       : std::max (target, band.weight - step);
                    weightRamp[b][(size_t) n] = band.weight;
                }
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = channels[ch] + start;
                const auto* source = input != nullptr && input[ch] != nullptr ? input[ch] + start : data;

                for (int n = 0; n < length; ++n)
                {
                    const auto x = data[n];
                    auto solo = 0.0f;

                    for (size_t b = 0; b < bands.size(); ++b)
                    {
                        auto& band = bands[b];

                        if (! band.running)
                            continue;

                        auto& state = band.state[(size_t) ch];
                        const auto in = band.fromInput ? source[n] : x;
                        const auto y = processSvf (processSvf (in, state[0], band.coefficients.sections[0]),
                                                   state[1], band.coefficients.sections[1]);
                        solo += weightRamp[b][(size_t) n] * y;
                    }

                    data[n] = (1.0f - mixRamp[(size_t) n]) * x + solo;
                }
            }

            for (auto& band : bands)
            {
                if (band.running && ! band.target && band.weight <= 0.0f)
                {
                    band.running = false;
                    for (auto& channel : band.state)
                        for (auto& section : channel)
                            section.reset();
                }
            }
        }
    }
}
