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

        if (type == Type::peak)
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
    }

    void ThreeBandEq::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings& s) noexcept
    {
        if (! s.enabled)
            return;

        const auto changed = ! hasCoefficients
                          || std::abs (s.lowFreq - last.lowFreq) > 1.0e-4f || std::abs (s.lowGainDb - last.lowGainDb) > 1.0e-4f
                          || std::abs (s.midFreq - last.midFreq) > 1.0e-4f || std::abs (s.midGainDb - last.midGainDb) > 1.0e-4f
                          || std::abs (s.midQ - last.midQ) > 1.0e-4f
                          || std::abs (s.highFreq - last.highFreq) > 1.0e-4f || std::abs (s.highGainDb - last.highGainDb) > 1.0e-4f;

        if (changed)
        {
            low.setParameters (Biquad::Type::lowShelf, s.lowFreq, 0.707, s.lowGainDb, sampleRate);
            mid.setParameters (Biquad::Type::peak, s.midFreq, s.midQ, s.midGainDb, sampleRate);
            high.setParameters (Biquad::Type::highShelf, s.highFreq, 0.707, s.highGainDb, sampleRate);
            last = s;
            hasCoefficients = true;
        }

        for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);

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

    float Compressor::staticReductionDb (float inputDb, float thresholdDb, float ratio) noexcept
    {
        const auto slope = 1.0f / std::max (1.0f, ratio) - 1.0f;
        const auto over = inputDb - thresholdDb;

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

            const auto target = staticReductionDb (gainToDb (peak), s.thresholdDb, s.ratio);
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
