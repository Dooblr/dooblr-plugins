#include "ProcessingChain.h"

#include <algorithm>
#include <cmath>

namespace dooblrkick::dsp
{
    ChainOrder chainOrderFromIndex (int index) noexcept
    {
        ChainOrder order { driveStage, eqStage, dynamicsStage, limiterStage };

        if (index <= 0 || index >= numChainOrders)
            return order;

        for (int i = 0; i < index; ++i)
            std::next_permutation (order.begin(), order.end());

        return order;
    }

    int chainOrderToIndex (const ChainOrder& order) noexcept
    {
        for (int i = 0; i < numChainOrders; ++i)
            if (chainOrderFromIndex (i) == order)
                return i;

        return -1;
    }

    const char* chainStageName (int stage) noexcept
    {
        switch (stage)
        {
            case driveStage:    return "Drive";
            case eqStage:       return "EQ";
            case dynamicsStage: return "Dynamics";
            case limiterStage:  return "Limiter";
            default:            return "";
        }
    }

    //==============================================================================
    void ProcessingChain::prepare (double sampleRate, int maxBlockSize)
    {
        const auto block = std::max (1, maxBlockSize);
        distortion.prepare (sampleRate, block);
        eq.prepare (sampleRate);
        sendEq.prepare (sampleRate);
        shaper.prepare (sampleRate);
        compressor.prepare (sampleRate);
        limiter.prepare (sampleRate);
        sum.setSize (2, block);
        sumIn.setSize (2, block);
        reset();
    }

    void ProcessingChain::reset()
    {
        distortion.reset();
        eq.reset();
        sendEq.reset();
        shaper.reset();
        compressor.reset();
        limiter.reset();
        lastRatio = { 1.0f, 1.0f };
    }

    float ProcessingChain::peakOf (const juce::AudioBuffer<float>& direct, const juce::AudioBuffer<float>& send,
                                   int n, bool split) noexcept
    {
        if (! split)
            return std::max (direct.getMagnitude (0, 0, n), direct.getMagnitude (1, 0, n));

        auto peak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = direct.getReadPointer (ch);
            const auto* s = send.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
                peak = std::max (peak, std::abs (d[i] + s[i]));
        }

        return peak;
    }

    template <typename Process>
    void ProcessingChain::gainStage (juce::AudioBuffer<float>& direct, juce::AudioBuffer<float>& send, int n,
                                     bool split, Process&& process) noexcept
    {
        if (! split)
        {
            process (direct);
            return;
        }

        // Before Drive: detect on (and process) the sum, then give each bus the same gain.
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = direct.getReadPointer (ch);
            const auto* s = send.getReadPointer (ch);
            auto* a = sum.getWritePointer (ch);
            auto* b = sumIn.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
                a[i] = b[i] = d[i] + s[i];
        }

        process (sum);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* in = sumIn.getReadPointer (ch);
            const auto* out = sum.getReadPointer (ch);
            auto* d = direct.getWritePointer (ch);
            auto* s = send.getWritePointer (ch);
            auto& ratio = lastRatio[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                // Gain stages are y = g * x (the clipper's g is y / x), so the ratio is the gain;
                // at a zero crossing keep the previous gain.
                if (std::abs (in[i]) > 1.0e-9f)
                    ratio = std::clamp (out[i] / in[i], 0.0f, 16.0f);

                d[i] *= ratio;
                s[i] *= ratio;
            }
        }
    }

    void ProcessingChain::process (juce::AudioBuffer<float>& direct, juce::AudioBuffer<float>& send, int n,
                                   const KickSettings& s, const ChainOrder& order,
                                   MeterBlock* meter, MeterTap* spectrumTap) noexcept
    {
        auto split = true; // the buses are separate until the Drive stage has run
        auto limiterSettings = s.output;
        limiterSettings.outputDb = 0.0f;

        for (const auto stage : order)
        {
            switch (stage)
            {
                case driveStage:
                {
                    if (meter != nullptr)
                        meter->driveIn = std::max (send.getMagnitude (0, 0, n), send.getMagnitude (1, 0, n));

                    distortion.process (send, 0, n, s.drive);

                    if (meter != nullptr)
                        meter->driveOut = std::max (send.getMagnitude (0, 0, n), send.getMagnitude (1, 0, n));

                    for (int ch = 0; ch < 2; ++ch)
                        direct.addFrom (ch, 0, send, ch, 0, n);

                    split = false;
                    break;
                }

                case eqStage:
                {
                    eq.process (direct, 0, n, s.eq);

                    if (split)
                        sendEq.process (send, 0, n, s.eq);

                    if (spectrumTap != nullptr)
                    {
                        if (split)
                        {
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                sum.copyFrom (ch, 0, direct, ch, 0, n);
                                sum.addFrom (ch, 0, send, ch, 0, n);
                            }

                            spectrumTap->pushSpectrum (sum.getReadPointer (0), sum.getReadPointer (1), n);
                        }
                        else
                        {
                            spectrumTap->pushSpectrum (direct.getReadPointer (0), direct.getReadPointer (1), n);
                        }
                    }
                    break;
                }

                case dynamicsStage:
                {
                    if (meter != nullptr)
                        meter->dynamicsIn = peakOf (direct, send, n, split);

                    gainStage (direct, send, n, split, [&] (juce::AudioBuffer<float>& b) { shaper.process (b, 0, n, s.shaper); });

                    if (meter != nullptr)
                        meter->compressorIn = peakOf (direct, send, n, split);

                    gainStage (direct, send, n, split, [&] (juce::AudioBuffer<float>& b) { compressor.process (b, 0, n, s.compressor); });

                    if (meter != nullptr)
                    {
                        meter->dynamicsOut = peakOf (direct, send, n, split);
                        meter->compressorGrDb = compressor.getLastMinReductionDb();
                    }
                    break;
                }

                case limiterStage:
                default:
                {
                    if (meter != nullptr)
                        meter->limiterIn = peakOf (direct, send, n, split);

                    gainStage (direct, send, n, split, [&] (juce::AudioBuffer<float>& b) { limiter.process (b, 0, n, limiterSettings); });

                    if (meter != nullptr)
                    {
                        meter->limiterOut = peakOf (direct, send, n, split);
                        meter->limiterGrDb = limiter.getLastGainReductionDb();
                    }
                    break;
                }
            }
        }

        if (split) // not reachable with a valid order (Drive is always in it)
            for (int ch = 0; ch < 2; ++ch)
                direct.addFrom (ch, 0, send, ch, 0, n);
    }

    //==============================================================================
    void OutputGain::process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb) noexcept
    {
        const auto target = std::pow (10.0f, gainDb * 0.05f);
        const auto smoothing = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));

        if (gain < 0.0f)
            gain = target;

        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            gain += (target - gain) * smoothing;
            left[i] *= gain;
            right[i] *= gain;
        }
    }
}
