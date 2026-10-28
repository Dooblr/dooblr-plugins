#include "ProcessingChain.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    ChainOrder chainOrderFromIndex (int index) noexcept
    {
        ChainOrder order { gateStage, eqStage, dynamicsStage, limiterStage };

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
            case gateStage:      return "Gate";
            case eqStage:        return "EQ";
            case dynamicsStage:  return "Comp";
            case limiterStage:   return "Limiter";
            default:             return "";
        }
    }

    void ProcessingChain::prepare (double sampleRate, int)
    {
        gate.prepare (sampleRate);
        eq.prepare (sampleRate);
        compressor.prepare (sampleRate);
        limiter.prepare (sampleRate);
        reset();
    }

    void ProcessingChain::reset()
    {
        gate.reset();
        eq.reset();
        compressor.reset();
        limiter.reset();
    }

    void ProcessingChain::process (juce::AudioBuffer<float>& buffer, int n, const VocalSettings& s,
                                   const ChainOrder& order, MeterBlock* meter, MeterTap* spectrumTap) noexcept
    {
        auto limiterSettings = s.limiter;
        limiterSettings.outputDb = 0.0f;

        auto peakOf = [&] () -> float
        {
            auto peak = 0.0f;
            for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
                peak = std::max (peak, buffer.getMagnitude (ch, 0, n));
            return peak;
        };

        for (const auto stage : order)
        {
            switch (stage)
            {
                case gateStage:
                {
                    if (meter != nullptr)
                        meter->gateIn = peakOf();

                    gate.process (buffer, 0, n, s.gate);

                    if (meter != nullptr)
                    {
                        meter->gateOut = peakOf();
                        meter->gateGrDb = gate.getLastMinReductionDb();
                    }
                    break;
                }

                case eqStage:
                {
                    eq.process (buffer, 0, n, s.eq);

                    if (spectrumTap != nullptr && buffer.getNumChannels() >= 2)
                        spectrumTap->pushSpectrum (buffer.getReadPointer (0), buffer.getReadPointer (1), n);
                    else if (spectrumTap != nullptr)
                        spectrumTap->pushSpectrum (buffer.getReadPointer (0), buffer.getReadPointer (0), n);
                    break;
                }

                case dynamicsStage:
                {
                    if (meter != nullptr)
                    {
                        meter->dynamicsIn = peakOf();
                        meter->compressorIn = meter->dynamicsIn;
                    }

                    compressor.process (buffer, 0, n, s.compressor);

                    if (meter != nullptr)
                    {
                        meter->dynamicsOut = peakOf();
                        meter->compressorGrDb = compressor.getLastMinReductionDb();
                    }
                    break;
                }

                case limiterStage:
                default:
                {
                    if (meter != nullptr)
                        meter->limiterIn = peakOf();

                    limiter.process (buffer, 0, n, limiterSettings);

                    if (meter != nullptr)
                    {
                        meter->limiterOut = peakOf();
                        meter->limiterGrDb = limiter.getLastGainReductionDb();
                    }
                    break;
                }
            }
        }
    }
}
