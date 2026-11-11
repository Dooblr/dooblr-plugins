#include "SubEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrsub::dsp
{
    void SubEngine::prepare (double sampleRateToUse, int)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        toneLeft.prepare (sampleRate);
        toneRight.prepare (sampleRate);
        sub.prepare (sampleRate);
        check.prepare (sampleRate);
        toneL.assign ((size_t) maxChunk, 0.0f);
        toneR.assign ((size_t) maxChunk, 0.0f);
        inL.assign ((size_t) maxChunk, 0.0f);
        inR.assign ((size_t) maxChunk, 0.0f);
        delayedL.assign ((size_t) maxChunk, 0.0f);
        delayedR.assign ((size_t) maxChunk, 0.0f);
        dryDelayL.assign ((size_t) ToneShift::latencySamples, 0.0f);
        dryDelayR.assign ((size_t) ToneShift::latencySamples, 0.0f);
        reset();
    }

    void SubEngine::reset()
    {
        toneLeft.reset();
        toneRight.reset();
        sub.reset();
        check.reset();
        std::fill (dryDelayL.begin(), dryDelayL.end(), 0.0f);
        std::fill (dryDelayR.begin(), dryDelayR.end(), 0.0f);
        dryWrite = 0;
        infraZL = infraZR = monoZL = monoZR = 0.0f;
        limitEnv = 0.0f;
        bypassMix = settings.bypass ? 1.0f : 0.0f;
        outGain = juce::Decibels::decibelsToGain (settings.outputDb);
    }

    void SubEngine::processSafety (float& l, float& r) noexcept
    {
        const auto th = thresholdsFor (settings.target);

        if (settings.infraHpf)
        {
            const auto hz = juce::jlimit (12.0f, 50.0f, settings.infraHz + th.infraHzBias);
            const auto a = 1.0f - std::exp (-6.28318530718f * hz / (float) sampleRate);
            infraZL += a * (l - infraZL);
            infraZR += a * (r - infraZR);
            l -= infraZL;
            r -= infraZR;
        }

        if (settings.monoBelow)
        {
            const auto a = 1.0f - std::exp (-6.28318530718f * settings.monoHz / (float) sampleRate);
            monoZL += a * (l - monoZL);
            monoZR += a * (r - monoZR);
            const auto midLow = 0.5f * (monoZL + monoZR);
            l = (l - monoZL) + midLow;
            r = (r - monoZR) + midLow;
        }

        if (settings.subLimit)
        {
            const auto ceiling = th.limiterCeiling;
            const auto peak = std::max (std::abs (l), std::abs (r));
            const auto atk = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
            const auto rel = 1.0f - std::exp (-1.0f / (0.080f * (float) sampleRate));
            if (peak > limitEnv) limitEnv += atk * (peak - limitEnv);
            else                 limitEnv += rel * (peak - limitEnv);
            if (peak > 1.0e-8f)
            {
                const auto soft = ceiling * std::tanh (peak / juce::jmax (1.0e-6f, ceiling)) / peak;
                const auto hard = limitEnv > ceiling ? ceiling / limitEnv : 1.0f;
                const auto gain = 0.5f * hard + 0.5f * soft;
                l *= gain;
                r *= gain;
            }
        }
    }

    void SubEngine::process (float* left, float* right, int numSamples)
    {
        if (left == nullptr || numSamples <= 0)
            return;

        toneLeft.setSettings (settings.tone);
        toneRight.setSettings (settings.tone);
        sub.setSettings (settings.sub);
        check.setTarget (settings.target);

        const auto mixT = juce::jlimit (0.0f, 1.0f, settings.mix);
        const auto bypassT = settings.bypass ? 1.0f : 0.0f;
        const auto outT = juce::Decibels::decibelsToGain (settings.outputDb);
        auto* rch = right != nullptr ? right : left;
        const auto stereo = right != nullptr && right != left;
        const auto delaySize = (int) dryDelayL.size();

        float peaks[2] { 0.0f, 0.0f };

        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const auto n = std::min (maxChunk, numSamples - start);
            for (int i = 0; i < n; ++i)
            {
                inL[(size_t) i] = left[start + i];
                inR[(size_t) i] = rch[start + i];
                delayedL[(size_t) i] = dryDelayL[(size_t) dryWrite];
                delayedR[(size_t) i] = dryDelayR[(size_t) dryWrite];
                dryDelayL[(size_t) dryWrite] = inL[(size_t) i];
                dryDelayR[(size_t) dryWrite] = inR[(size_t) i];
                dryWrite = (dryWrite + 1) % delaySize;
            }

            toneLeft.process (inL.data(), toneL.data(), n);
            if (stereo)
                toneRight.process (inR.data(), toneR.data(), n);
            else
                std::copy (toneL.begin(), toneL.begin() + n, toneR.begin());

            float lastSub = 0.0f, lastFund = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                auto l = toneL[(size_t) i];
                auto r = stereo ? toneR[(size_t) i] : l;
                const auto mid = 0.5f * (l + r);

                float generated = 0.0f;
                const auto add = sub.process (mid, generated);
                l += add;
                r += add;
                lastSub = generated;
                lastFund = mid;

                processSafety (l, r);

                bypassMix += 0.002f * (bypassT - bypassMix);
                outGain += 0.002f * (outT - outGain);

                const auto dryL = delayedL[(size_t) i];
                const auto dryR = delayedR[(size_t) i];
                l = (1.0f - mixT) * dryL + mixT * l;
                r = (1.0f - mixT) * dryR + mixT * r;
                l = (1.0f - bypassMix) * l * outGain + bypassMix * dryL;
                r = (1.0f - bypassMix) * r * outGain + bypassMix * dryR;

                left[start + i] = l;
                if (stereo)
                    right[start + i] = r;
                peaks[0] = std::max (peaks[0], std::abs (l));
                peaks[1] = std::max (peaks[1], std::abs (r));
            }

            check.process (left + start, stereo ? right + start : left + start, n, stereo ? 2 : 1,
                           lastSub, lastFund, sub.getTrackedHz());
        }

        peakTap.push (peaks[0], stereo ? peaks[1] : peaks[0]);
    }

    DisplaySnapshot SubEngine::display() const
    {
        DisplaySnapshot d;
        d.analysis = check.snapshot();
        d.verdict = evaluate (d.analysis, settings.target, d.reasons);
        d.trackedHz = sub.getTrackedHz();
        d.envelope = sub.getEnvelope();
        check.copySpectrum (d.spectrum.data(), (int) d.spectrum.size());
        d.bands = d.analysis.bandRms;
        return d;
    }
}
