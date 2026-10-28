#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    namespace
    {
        constexpr float pi = 3.14159265358979323846f;

        float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

        float gainToDb (float gain) noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }

        float timeCoefficient (float ms, double sampleRate) noexcept
        {
            return std::exp (-1.0f / std::max (1.0e-6f, ms * 0.001f * (float) sampleRate));
        }
    }

    //==============================================================================
    void SvfFilter::setParameters (Mode newMode, float cutoffHz, float resonance01, double sampleRate) noexcept
    {
        mode = newMode;
        const auto fc = std::clamp (cutoffHz, 10.0f, 0.45f * (float) sampleRate);
        g = std::tan (pi * fc / (float) sampleRate);
        k = 2.0f - 1.92f * std::clamp (resonance01, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float SvfFilter::process (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        switch (mode)
        {
            case Mode::lowPass:  return v2;
            case Mode::bandPass: return v1;
            case Mode::highPass: return x - k * v1 - v2;
        }

        return v2;
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
    void Distortion::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (1, maxBlockSize);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) maxBlock);
        dry.setSize (2, maxBlock);
        reset();
    }

    void Distortion::reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();

        toneState.fill (0.0f);
        holdValue.fill (0.0f);
        dcX.fill (0.0f);
        dcY.fill (0.0f);
        holdPhase = 1.0f;
    }

    float Distortion::shape (int type, float x, float gain) noexcept
    {
        const auto v = x * gain;

        switch (type)
        {
            case DistortionSettings::softClip: return std::tanh (v);
            case DistortionSettings::hardClip: return std::clamp (v, -1.0f, 1.0f);
            case DistortionSettings::tube:
            {
                constexpr float bias = 0.35f;
                static const float offset = std::tanh (bias);
                return (std::tanh (v + bias) - offset) * 1.25f;
            }
            case DistortionSettings::fold:     return std::sin (v * pi * 0.5f);
            default:       return std::clamp (v, -1.0f, 1.0f);
        }
    }

    void Distortion::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                              const DistortionSettings& s) noexcept
    {
        if (! s.enabled || oversampling == nullptr)
            return;

        const auto numChannels = std::min (2, buffer.getNumChannels());
        const auto gain = dbToGain (s.driveDb);
        const auto type = std::clamp (s.type, 0, (int) DistortionSettings::numTypes - 1);

        for (int offset = 0; offset < numSamples; offset += maxBlock)
        {
            const auto n = std::min (maxBlock, numSamples - offset);
            const auto start = startSample + offset;

            for (int ch = 0; ch < numChannels; ++ch)
                dry.copyFrom (ch, 0, buffer, ch, start, n);

            if (type == DistortionSettings::bitcrush)
            {
                const auto levels = std::pow (2.0f, std::clamp (s.bits, 1.0f, 16.0f) - 1.0f);
                const auto step = std::clamp (s.rateHz, 100.0f, (float) sampleRate) / (float) sampleRate;

                for (int i = 0; i < n; ++i)
                {
                    holdPhase += step;
                    const auto update = holdPhase >= 1.0f;

                    if (update)
                        holdPhase -= std::floor (holdPhase);

                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        auto* data = buffer.getWritePointer (ch, start);

                        if (update)
                        {
                            const auto clipped = std::clamp (data[i] * gain, -1.0f, 1.0f);
                            holdValue[(size_t) ch] = std::round (clipped * levels) / levels;
                        }

                        data[i] = holdValue[(size_t) ch];
                    }
                }
            }
            else
            {
                float* channels[2] = { buffer.getWritePointer (0, start),
                                       buffer.getWritePointer (numChannels > 1 ? 1 : 0, start) };
                juce::dsp::AudioBlock<float> block (channels, (size_t) numChannels, (size_t) n);
                auto up = oversampling->processSamplesUp (block);

                for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                {
                    auto* data = up.getChannelPointer (ch);

                    for (size_t i = 0; i < up.getNumSamples(); ++i)
                        data[i] = shape (type, data[i], gain);
                }

                oversampling->processSamplesDown (block);
            }

            const auto toneHz = 1000.0f * std::pow (20.0f, std::clamp (s.tone, 0.0f, 1.0f));
            const auto toneOn = s.tone < 0.999f;
            const auto toneA = 1.0f - std::exp (-2.0f * pi * toneHz / (float) sampleRate);
            const auto dcR = 1.0f - 2.0f * pi * 10.0f / (float) sampleRate;
            const auto out = dbToGain (s.outputDb);
            const auto mix = std::clamp (s.mix, 0.0f, 1.0f);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch, start);
                const auto* dryData = dry.getReadPointer (ch);
                auto& tone = toneState[(size_t) ch];
                auto& x1 = dcX[(size_t) ch];
                auto& y1 = dcY[(size_t) ch];

                for (int i = 0; i < n; ++i)
                {
                    auto y = data[i];

                    // DC blocker: the asymmetric tube curve adds an offset.
                    const auto blocked = y - x1 + dcR * y1;
                    x1 = y;
                    y1 = blocked;
                    y = blocked;

                    if (toneOn)
                    {
                        tone += toneA * (y - tone);
                        y = tone;
                    }

                    data[i] = dryData[i] * (1.0f - mix) + y * out * mix;
                }
            }
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
    void TransientShaper::prepare (double sampleRate)
    {
        fastAttack  = timeCoefficient (0.3f, sampleRate);
        fastRelease = timeCoefficient (15.0f, sampleRate);
        slowAttack  = timeCoefficient (15.0f, sampleRate);
        slowRelease = timeCoefficient (150.0f, sampleRate);
        reset();
    }

    void TransientShaper::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                   const ShaperSettings& s) noexcept
    {
        if (! s.enabled)
            return;

        const auto numChannels = std::min (2, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (buffer.getSample (ch, startSample + i)));

            fast = peak > fast ? fastAttack * fast + (1.0f - fastAttack) * peak
                               : fastRelease * fast + (1.0f - fastRelease) * peak;
            slow = peak > slow ? slowAttack * slow + (1.0f - slowAttack) * peak
                               : slowRelease * slow + (1.0f - slowRelease) * peak;

            const auto diffDb = gainToDb (fast + 1.0e-6f) - gainToDb (slow + 1.0e-6f);
            const auto gainDb = std::clamp (s.attack * std::clamp (diffDb, 0.0f, 24.0f) * 0.75f
                                              + s.sustain * std::clamp (-diffDb, 0.0f, 24.0f) * 0.75f,
                                            -18.0f, 18.0f);
            const auto gain = dbToGain (gainDb);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[startSample + i] *= gain;
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
            const auto gain = dbToGain (envelopeDb + s.makeupDb);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[startSample + i] *= gain;
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
            // Per-sample ramp so output gain changes never step the waveform.
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
}
