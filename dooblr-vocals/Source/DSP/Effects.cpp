#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
        float gainToDb (float gain) noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }

        float timeCoefficient (float ms, double sampleRate) noexcept
        {
            return std::exp (-1.0f / std::max (1.0e-6f, ms * 0.001f * (float) sampleRate));
        }
    }

    //==============================================================================
    void Biquad::setParameters (Type type, double frequency, double q, double gainDb, double sampleRate) noexcept
    {
        const auto f = std::clamp (frequency, 10.0, 0.45 * sampleRate);
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * 3.14159265358979323846 * f / sampleRate;
        const auto cosw = std::cos (w0), sinw = std::sin (w0);
        const auto alpha = sinw / (2.0 * std::max (0.05, q));
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;

        if (type == Type::highPass)
        {
            nb0 = 0.5 * (1 + cosw); nb1 = -(1 + cosw); nb2 = 0.5 * (1 + cosw);
            na0 = 1 + alpha; na1 = -2 * cosw; na2 = 1 - alpha;
        }
        else if (type == Type::peak)
        {
            nb0 = 1 + alpha * A; nb1 = -2 * cosw; nb2 = 1 - alpha * A;
            na0 = 1 + alpha / A; na1 = -2 * cosw; na2 = 1 - alpha / A;
        }
        else
        {
            const auto sqrtA2alpha = 2.0 * std::sqrt (A) * alpha;

            if (type == Type::lowShelf)
            {
                nb0 = A * ((A + 1) - (A - 1) * cosw + sqrtA2alpha);
                nb1 = 2 * A * ((A - 1) - (A + 1) * cosw);
                nb2 = A * ((A + 1) - (A - 1) * cosw - sqrtA2alpha);
                na0 = (A + 1) + (A - 1) * cosw + sqrtA2alpha;
                na1 = -2 * ((A - 1) + (A + 1) * cosw);
                na2 = (A + 1) + (A - 1) * cosw - sqrtA2alpha;
            }
            else
            {
                nb0 = A * ((A + 1) + (A - 1) * cosw + sqrtA2alpha);
                nb1 = -2 * A * ((A - 1) + (A + 1) * cosw);
                nb2 = A * ((A + 1) + (A - 1) * cosw - sqrtA2alpha);
                na0 = (A + 1) - (A - 1) * cosw + sqrtA2alpha;
                na1 = 2 * ((A - 1) - (A + 1) * cosw);
                na2 = (A + 1) - (A - 1) * cosw - sqrtA2alpha;
            }
        }

        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    void Biquad::reset() noexcept
    {
        for (auto& s : state)
            s.fill (0.0);
    }

    float Biquad::process (int channel, float x) noexcept
    {
        auto& s = state[(size_t) channel];
        const auto y = b0 * x + b1 * s[0] + b2 * s[1] - a1 * s[2] - a2 * s[3];
        s[1] = s[0]; s[0] = x;
        s[3] = s[2]; s[2] = y;
        return (float) y;
    }

    double Biquad::magnitudeAt (double frequency, double sampleRate) const noexcept
    {
        const auto w = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }

    //==============================================================================
    void HighPass::setParameters (double frequency, int slopeIndex, double sampleRate) noexcept
    {
        const auto n = sectionsForSlope (slopeIndex);

        if (n != numSections)
        {
            numSections = n;
            reset();
        }

        // Butterworth of order 2n: section k has Q = 1 / (2 cos ((2k + 1) pi / (4n))).
        for (int k = 0; k < numSections; ++k)
        {
            const auto q = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * 3.14159265358979323846 / (4.0 * numSections)));
            sections[(size_t) k].setParameters (Biquad::Type::highPass, frequency, q, 0.0, sampleRate);
        }
    }

    void HighPass::reset() noexcept
    {
        for (auto& s : sections)
            s.reset();
    }

    double HighPass::magnitudeAt (double frequency, double sampleRate) const noexcept
    {
        auto m = 1.0;
        for (int k = 0; k < numSections; ++k)
            m *= sections[(size_t) k].magnitudeAt (frequency, sampleRate);
        return m;
    }

    //==============================================================================
    // Copied from dooblr-q (Source/DSP/DynamicEQBand.cpp computeDynamicGainDb and
    // Source/DSP/SvfFilter.cpp prewarp / finalise / makeDetectorCoefficients); dooblr-q is unchanged.
    namespace eqdynamics
    {
        float computeDynamicGainDb (float levelDb, float thresholdDb, float rangeDb) noexcept
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

        namespace
        {
            double prewarp (double frequency, double sampleRate) noexcept
            {
                const auto f = std::clamp (frequency, 10.0, 0.49 * sampleRate);
                return std::tan (juce::MathConstants<double>::pi * f / sampleRate);
            }

            SvfCoefficients finalise (double g, double k, double m0, double m1, double m2) noexcept
            {
                SvfCoefficients c;
                c.a1 = 1.0 / (1.0 + g * (g + k));
                c.a2 = g * c.a1;
                c.a3 = g * c.a2;
                c.m0 = m0;
                c.m1 = m1;
                c.m2 = m2;
                return c;
            }
        }

        SvfCoefficients makeDetectorCoefficients (DetectorShape shape, double frequency, double q, double sampleRate) noexcept
        {
            const auto w = prewarp (frequency, sampleRate);
            constexpr auto butterworthK = 1.4142135623730951;

            switch (shape)
            {
                case DetectorShape::lowShelf:  return finalise (w, butterworthK, 0.0, 0.0, 1.0);
                case DetectorShape::highShelf: return finalise (w, butterworthK, 1.0, -butterworthK, -1.0);
                case DetectorShape::bell:      break;
            }

            // k * band-pass output has unity gain at the centre frequency.
            const auto k = 1.0 / std::max (q, 0.025);
            return finalise (w, k, 0.0, k, 0.0);
        }
    }

    //==============================================================================
    void ThreeBandEq::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        hasCoefficients = false;
        reset();
    }

    void ThreeBandEq::reset()
    {
        low.reset();
        mid.reset();
        high.reset();
        hpf.reset();
        for (auto& band : detectorState)
            for (auto& state : band)
                state.reset();
        envelope.fill (0.0f);
        dynamicGainDb.fill (0.0f);
        hasCoefficients = false; // also drops any dynamic gain left in the coefficients
        dynamicApplied.fill (false);
    }

    void ThreeBandEq::setBandCoefficients (int band, const EqSettings& s, float extraGainDb) noexcept
    {
        const auto total = [extraGainDb] (float gainDb)
        {
            return extraGainDb == 0.0f ? (double) gainDb
                                       : (double) std::clamp (gainDb + extraGainDb, -eqdynamics::maxTotalGainDb, eqdynamics::maxTotalGainDb);
        };

        if (band == 0)      low.setParameters (Biquad::Type::lowShelf, s.lowFreq, 0.707, total (s.lowGainDb), sampleRate);
        else if (band == 1) mid.setParameters (Biquad::Type::peak, s.midFreq, s.midQ, total (s.midGainDb), sampleRate);
        else                high.setParameters (Biquad::Type::highShelf, s.highFreq, 0.707, total (s.highGainDb), sampleRate);
    }

    void ThreeBandEq::processDynamic (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings& s) noexcept
    {
        using namespace eqdynamics;
        const auto onePole = [this] (float ms)
        {
            const auto samples = (double) ms * 0.001 * sampleRate;
            return samples > 0.0 ? (float) std::exp (-1.0 / samples) : 0.0f;
        };

        std::array<bool, 3> dyn {};
        std::array<float, 3> attack {}, release {};
        std::array<SvfCoefficients, 3> detector {};
        const DetectorShape shapes[] { DetectorShape::lowShelf, DetectorShape::bell, DetectorShape::highShelf };
        const float freqs[] { s.lowFreq, s.midFreq, s.highFreq };

        for (size_t b = 0; b < 3; ++b)
        {
            dyn[b] = isDynamic (s.dynamics[b]);

            if (dyn[b])
            {
                attack[b] = onePole (s.dynamics[b].attackMs);
                release[b] = onePole (s.dynamics[b].releaseMs);
                detector[b] = makeDetectorCoefficients (shapes[b], freqs[b], b == 1 ? s.midQ : 0.707, sampleRate);
            }
            else if (dynamicApplied[b])
            {
                setBandCoefficients ((int) b, s, 0.0f); // back to the static curve
                dynamicApplied[b] = false;
                envelope[b] = dynamicGainDb[b] = 0.0f;
                for (auto& state : detectorState[b]) state.reset();
            }
        }

        const auto numChannels = std::min (2, buffer.getNumChannels());
        std::array<float*, 2> data {};
        for (int ch = 0; ch < numChannels; ++ch)
            data[(size_t) ch] = buffer.getWritePointer (ch, startSample);

        for (int pos = 0; pos < numSamples; pos += subBlock)
        {
            const auto n = std::min (subBlock, numSamples - pos);

            // Coefficients for this sub-block, from the dynamic gain measured up to its start.
            for (size_t b = 0; b < 3; ++b)
                if (dyn[b])
                {
                    setBandCoefficients ((int) b, s, dynamicGainDb[b]);
                    dynamicApplied[b] = true;
                }

            for (int i = pos; i < pos + n; ++i)
            {
                std::array<float, 3> level {}; // stereo linked: the louder channel drives each band

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto x = data[(size_t) ch][i];

                    if (s.hpfOn)
                        x = hpf.process (ch, x);

                    // Each band's detector listens to that band's own input.
                    if (dyn[0]) level[0] = std::max (level[0], std::abs (processSvf (x, detectorState[0][(size_t) ch], detector[0])));
                    x = low.process (ch, x);
                    if (dyn[1]) level[1] = std::max (level[1], std::abs (processSvf (x, detectorState[1][(size_t) ch], detector[1])));
                    x = mid.process (ch, x);
                    if (dyn[2]) level[2] = std::max (level[2], std::abs (processSvf (x, detectorState[2][(size_t) ch], detector[2])));
                    data[(size_t) ch][i] = high.process (ch, x);
                }

                for (size_t b = 0; b < 3; ++b)
                    if (dyn[b])
                    {
                        auto& env = envelope[b];
                        env = level[b] + (level[b] > env ? attack[b] : release[b]) * (env - level[b]);
                    }
            }

            for (size_t b = 0; b < 3; ++b)
                if (dyn[b])
                    dynamicGainDb[b] = computeDynamicGainDb (juce::Decibels::gainToDecibels (envelope[b], -120.0f),
                                                             s.dynamics[b].thresholdDb, s.dynamics[b].rangeDb);
        }
    }

    void ThreeBandEq::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings& s) noexcept
    {
        if (! s.enabled)
        {
            dynamicGainDb.fill (0.0f);
            return;
        }

        const auto changed = ! hasCoefficients
                          || std::abs (s.lowFreq - last.lowFreq) > 1.0e-4f || std::abs (s.lowGainDb - last.lowGainDb) > 1.0e-4f
                          || std::abs (s.midFreq - last.midFreq) > 1.0e-4f || std::abs (s.midGainDb - last.midGainDb) > 1.0e-4f
                          || std::abs (s.midQ - last.midQ) > 1.0e-4f
                          || std::abs (s.highFreq - last.highFreq) > 1.0e-4f || std::abs (s.highGainDb - last.highGainDb) > 1.0e-4f;
        const auto hpfChanged = s.hpfOn && (! hpfActive || std::abs (s.hpfFreq - lastHpfFreq) > 1.0e-4f || s.hpfSlope != lastHpfSlope);

        if (hpfChanged)
        {
            if (! hpfActive)
                hpf.reset(); // switched on: start from silence, not from stale state
            hpf.setParameters (s.hpfFreq, s.hpfSlope, sampleRate);
            lastHpfFreq = s.hpfFreq;
            lastHpfSlope = s.hpfSlope;
        }

        hpfActive = s.hpfOn;

        if (changed)
        {
            low.setParameters (Biquad::Type::lowShelf, s.lowFreq, 0.707, s.lowGainDb, sampleRate);
            mid.setParameters (Biquad::Type::peak, s.midFreq, s.midQ, s.midGainDb, sampleRate);
            high.setParameters (Biquad::Type::highShelf, s.highFreq, 0.707, s.highGainDb, sampleRate);
            last = s;
            hasCoefficients = true;
            dynamicApplied.fill (false);
        }

        if (isDynamic (s.dynamics[0]) || isDynamic (s.dynamics[1]) || isDynamic (s.dynamics[2]))
        {
            processDynamic (buffer, startSample, numSamples, s);
            return;
        }

        for (size_t b = 0; b < 3; ++b) // dynamics just switched off: restore the static curve
            if (dynamicApplied[b])
            {
                setBandCoefficients ((int) b, s, 0.0f);
                dynamicApplied[b] = false;
            }

        envelope.fill (0.0f);
        dynamicGainDb.fill (0.0f);
        for (auto& band : detectorState)
            for (auto& state : band)
                state.reset();

        for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);

            if (s.hpfOn) // the high-pass runs first, ahead of the low shelf
                for (int i = 0; i < numSamples; ++i)
                    data[i] = high.process (ch, mid.process (ch, low.process (ch, hpf.process (ch, data[i]))));
            else
                for (int i = 0; i < numSamples; ++i)
                    data[i] = high.process (ch, mid.process (ch, low.process (ch, data[i])));
        }
    }

    //==============================================================================
    void Compressor::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    float Compressor::staticReductionDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
    {
        const auto slope = 1.0f / std::max (1.0f, ratio) - 1.0f;
        const auto over = inputDb - thresholdDb;
        kneeDb = std::min (kneeDb, maxKneeDb);

        if (! (kneeDb > 0.0f)) // hard knee
            return over > 0.0f ? slope * over : 0.0f;

        if (2.0f * over > kneeDb)
            return slope * over;

        if (2.0f * over > -kneeDb)
            return slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);

        return 0.0f;
    }

    void Compressor::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                              const CompressorSettings& s) noexcept
    {
        lastMinReductionDb = 0.0f;

        if (! s.enabled)
            return;

        const auto numChannels = std::min (2, buffer.getNumChannels());
        const auto attack = timeCoefficient (s.attackMs, sampleRate);
        const auto release = timeCoefficient (s.releaseMs, sampleRate);
        auto minReduction = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (buffer.getSample (ch, startSample + i)));

            const auto target = staticReductionDb (gainToDb (peak), s.thresholdDb, s.ratio, s.kneeDb);
            const auto coefficient = target < envelopeDb ? attack : release;
            envelopeDb = coefficient * envelopeDb + (1.0f - coefficient) * target;
            minReduction = std::min (minReduction, envelopeDb);
            const auto g = dbToGain (envelopeDb + s.makeupDb);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[startSample + i] *= g;
        }

        lastMinReductionDb = minReduction;
    }

    //==============================================================================
    void OutputStage::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    float OutputStage::softClip (float x, float ceiling) noexcept
    {
        const auto threshold = 0.75f * ceiling;
        const auto a = std::abs (x);

        if (a <= threshold)
            return x;

        const auto range = ceiling - threshold;
        const auto y = threshold + range * std::tanh ((a - threshold) / range);
        return x < 0.0f ? -y : y;
    }

    void OutputStage::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                               const LimiterSettings& s) noexcept
    {
        const auto numChannels = std::min (2, buffer.getNumChannels());
        const auto ceiling = dbToGain (std::min (0.0f, s.ceilingDb));
        const auto release = timeCoefficient (s.releaseMs, sampleRate);
        const auto outputTarget = dbToGain (s.outputDb);
        const auto outputSmoothing = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        auto minGain = 1.0f, peakOut = 0.0f;

        if (outputGain < 0.0f)
            outputGain = outputTarget;

        for (int i = 0; i < numSamples; ++i)
        {
            outputGain += (outputTarget - outputGain) * outputSmoothing;
            const auto output = outputGain;

            if (s.mode == LimiterSettings::limiter)
            {
                auto peak = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                    peak = std::max (peak, std::abs (buffer.getSample (ch, startSample + i)));

                const auto target = peak > ceiling ? ceiling / peak : 1.0f;
                limiterGain = target < limiterGain ? target : target + (limiterGain - target) * release;
                minGain = std::min (minGain, limiterGain);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& sample = buffer.getWritePointer (ch)[startSample + i];
                    sample = std::clamp (sample * limiterGain, -ceiling, ceiling);
                    peakOut = std::max (peakOut, std::abs (sample));
                    sample *= output;
                }
            }
            else
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& sample = buffer.getWritePointer (ch)[startSample + i];

                    if (s.mode == LimiterSettings::clipper)
                    {
                        const auto in = std::abs (sample);
                        sample = softClip (sample, ceiling);

                        if (in > 1.0e-6f)
                            minGain = std::min (minGain, std::abs (sample) / in);
                    }

                    peakOut = std::max (peakOut, std::abs (sample));
                    sample *= output;
                }
            }
        }

        lastReductionDb = std::min (0.0f, gainToDb (minGain));
        lastPeakOut = peakOut;
    }

    //==============================================================================
    void Gate::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void Gate::reset() noexcept
    {
        gain = 1.0f;
        holdSamplesLeft = 0;
        lastMinReductionDb = 0.0f;
        lastGain = 1.0f;
        open = false;
    }

    float Gate::staticGain (float inputDb, float thresholdDb, float rangeDb) noexcept
    {
        if (inputDb >= thresholdDb)
            return 1.0f;

        return dbToGain (-std::max (0.0f, rangeDb));
    }

    void Gate::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const GateSettings& s) noexcept
    {
        lastMinReductionDb = 0.0f;
        lastGain = 1.0f;

        if (! s.enabled)
            return;

        const auto numChannels = std::min (2, buffer.getNumChannels());
        const auto closedGain = dbToGain (-std::max (0.0f, s.rangeDb));
        const auto attack = 1.0f - timeCoefficient (std::max (0.05f, s.attackMs), sampleRate);
        const auto release = 1.0f - timeCoefficient (std::max (1.0f, s.releaseMs), sampleRate);
        const auto holdSamples = std::max (0, (int) std::lround (s.holdMs * 0.001 * sampleRate));
        auto minReduction = 0.0f;
        auto endGain = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (buffer.getSample (ch, startSample + i)));

            const auto peakDb = gainToDb (peak);
            const auto wantOpen = peakDb >= s.thresholdDb;

            if (wantOpen)
            {
                open = true;
                holdSamplesLeft = holdSamples;
            }
            else if (holdSamplesLeft > 0)
            {
                --holdSamplesLeft;
            }
            else
            {
                open = false;
            }

            const auto target = open ? 1.0f : closedGain;
            const auto coeff = target > gain ? attack : release;
            gain += (target - gain) * coeff;
            minReduction = std::min (minReduction, gainToDb (gain));
            endGain = gain;

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[startSample + i] *= gain;
        }

        lastMinReductionDb = minReduction;
        lastGain = endGain;
    }

    //==============================================================================
    void OutputGain::process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb) noexcept
    {
        const auto target = dbToGain (gainDb);
        const auto smoothing = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));

        if (gain < 0.0f)
            gain = target;

        const auto channels = std::min (2, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            gain += (target - gain) * smoothing;

            for (int ch = 0; ch < channels; ++ch)
                buffer.getWritePointer (ch)[i] *= gain;
        }
    }
}
