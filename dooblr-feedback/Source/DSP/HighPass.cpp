#include "HighPass.h"

#include <algorithm>

namespace dooblrfeedback::dsp
{
    void HighPass::prepare (double sr, int) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        mixStep = 1.0f / std::max (1.0f, fadeSeconds * (float) sampleRate);
        freqSmooth.reset (sampleRate, smoothSeconds);
        freqSmooth.setCurrentAndTargetValue (defaultHz);
        design (defaultHz);
        mix = 0.0f;
        controlCountdown = 0;
        reset();
    }

    void HighPass::reset() noexcept
    {
        ic1 = {};
        ic2 = {};
    }

    float HighPass::analogResponseDb (float freqHz, float cutoffHz) noexcept
    {
        if (! (freqHz > 0.0f) || ! (cutoffHz > 0.0f))
            return -90.0f;
        const auto r = freqHz / cutoffHz;
        const auto r2 = r * r;
        const auto mag = r2 / std::sqrt (1.0f + r2 * r2);
        return juce::Decibels::gainToDecibels (mag, -90.0f);
    }

    void HighPass::design (float cutoffHz) noexcept
    {
        const auto nyquistLimit = (float) (sampleRate * 0.45);
        const auto freq = juce::jlimit (minHz * 0.5f, nyquistLimit, cutoffHz);
        const auto g = std::tan (juce::MathConstants<float>::pi * freq / (float) sampleRate);
        k = std::sqrt (2.0f); // Butterworth Q = 1/sqrt(2)
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void HighPass::apply (int channel, float* x, int n) noexcept
    {
        auto c1 = ic1[(size_t) channel], c2 = ic2[(size_t) channel];
        const auto kk = k;
        for (int i = 0; i < n; ++i)
        {
            const auto v0 = x[i];
            const auto v3 = v0 - c2;
            const auto v1 = a1 * c1 + a2 * v3;
            const auto v2 = c2 + a2 * c1 + a3 * v3;
            c1 = 2.0f * v1 - c1;
            c2 = 2.0f * v2 - c2;
            x[i] = v0 - kk * v1 - v2;
        }
        ic1[(size_t) channel] = c1;
        ic2[(size_t) channel] = c2;
    }

    void HighPass::process (float* const* io, const float* const* dry, int numChannels, int numSamples,
                            bool on, float freqHz) noexcept
    {
        const auto channels = juce::jlimit (0, 2, numChannels);
        if (channels == 0 || numSamples <= 0 || io == nullptr)
            return;

        freqHz = juce::jlimit (minHz, maxHz, freqHz);
        freqSmooth.setTargetValue (freqHz);
        const auto targetMix = on ? 1.0f : 0.0f;

        if (mix <= 0.0f && targetMix <= 0.0f)
        {
            freqSmooth.setCurrentAndTargetValue (freqHz);
            return;
        }

        if (mix <= 0.0f && targetMix > 0.0f)
            reset();

        int pos = 0;
        while (pos < numSamples)
        {
            if (controlCountdown <= 0)
            {
                design (freqSmooth.getCurrentValue());
                controlCountdown = controlInterval;
            }

            const auto n = std::min (numSamples - pos, controlCountdown);
            freqSmooth.skip (n);
            for (int ch = 0; ch < channels; ++ch)
                apply (ch, io[ch] + pos, n);

            const auto fullyOn = mix >= 1.0f && targetMix >= 1.0f;
            if (! fullyOn && dry != nullptr)
            {
                for (int i = 0; i < n; ++i)
                {
                    if (std::abs (mix - targetMix) > 1.0e-8f)
                        mix = mix < targetMix ? std::min (targetMix, mix + mixStep)
                                              : std::max (targetMix, mix - mixStep);
                    const auto wetMix = mix, dryMix = 1.0f - mix;
                    for (int ch = 0; ch < channels; ++ch)
                        io[ch][pos + i] = wetMix * io[ch][pos + i] + dryMix * dry[ch][pos + i];
                }
            }
            else if (! fullyOn)
            {
                for (int i = 0; i < n; ++i)
                    if (std::abs (mix - targetMix) > 1.0e-8f)
                        mix = mix < targetMix ? std::min (targetMix, mix + mixStep)
                                              : std::max (targetMix, mix - mixStep);
            }
            else
            {
                mix = 1.0f;
            }

            controlCountdown -= n;
            pos += n;
        }

        if (mix <= 0.0f && targetMix <= 0.0f)
            reset();
    }
}
