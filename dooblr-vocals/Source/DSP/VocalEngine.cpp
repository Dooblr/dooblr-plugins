#include "VocalEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr int smoothBlock = 32;
        constexpr float smoothMs = 10.0f;

        float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    }

    void VocalEngine::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (1, maxBlockSize);
        fadeLength = std::max (1, (int) std::lround (0.015 * sampleRate));
        fadeBuffer.setSize (2, maxBlock);
        pitch.prepare (sampleRate, maxBlock);
        chains[0].prepare (sampleRate, maxBlock);
        chains[1].prepare (sampleRate, maxBlock);
        outputGain.prepare (sampleRate);
        fx.prepare (sampleRate, maxBlock);
        limiter.prepare (sampleRate);
        reset();
    }

    void VocalEngine::reset()
    {
        pitch.reset();
        chains[0].reset();
        chains[1].reset();
        outputGain.reset();
        fx.reset();
        limiter.reset();
        activeChain = 0;
        activeOrder = 0;
        fadeRemaining = 0;
        lastGrDb = 0.0f;
        haveSettings = false;
        smoothCounter = 0;
    }

    void VocalEngine::lerpSettings (VocalSettings& current, const VocalSettings& from, const VocalSettings& to, float a) noexcept
    {
        auto& p = current.pitch;
        const auto& q = to.pitch;
        p.retuneMs = lerp (from.pitch.retuneMs, q.retuneMs, a);
        p.humanize = lerp (from.pitch.humanize, q.humanize, a);
        p.amount = lerp (from.pitch.amount, q.amount, a);
        p.a4Hz = lerp (from.pitch.a4Hz, q.a4Hz, a);
        p.vibratoRespect = lerp (from.pitch.vibratoRespect, q.vibratoRespect, a);
        if (std::abs (p.vibratoRespect - q.vibratoRespect) < 1.0e-4f)
            p.vibratoRespect = q.vibratoRespect; // lands exactly (0 = the flatten-vibrato path)

        auto& g = current.gate;
        const auto& h = to.gate;
        g.thresholdDb = lerp (from.gate.thresholdDb, h.thresholdDb, a);
        g.attackMs = lerp (from.gate.attackMs, h.attackMs, a);
        g.holdMs = lerp (from.gate.holdMs, h.holdMs, a);
        g.releaseMs = lerp (from.gate.releaseMs, h.releaseMs, a);
        g.rangeDb = lerp (from.gate.rangeDb, h.rangeDb, a);

        auto& e = current.eq;
        const auto& f = to.eq;
        e.lowFreq = lerp (from.eq.lowFreq, f.lowFreq, a);
        e.lowGainDb = lerp (from.eq.lowGainDb, f.lowGainDb, a);
        e.midFreq = lerp (from.eq.midFreq, f.midFreq, a);
        e.midGainDb = lerp (from.eq.midGainDb, f.midGainDb, a);
        e.midQ = lerp (from.eq.midQ, f.midQ, a);
        e.highFreq = lerp (from.eq.highFreq, f.highFreq, a);
        e.highGainDb = lerp (from.eq.highGainDb, f.highGainDb, a);

        auto& c = current.compressor;
        const auto& d = to.compressor;
        c.thresholdDb = lerp (from.compressor.thresholdDb, d.thresholdDb, a);
        c.ratio = lerp (from.compressor.ratio, d.ratio, a);
        c.attackMs = lerp (from.compressor.attackMs, d.attackMs, a);
        c.releaseMs = lerp (from.compressor.releaseMs, d.releaseMs, a);
        c.makeupDb = lerp (from.compressor.makeupDb, d.makeupDb, a);
        c.kneeDb = lerp (from.compressor.kneeDb, d.kneeDb, a);

        auto& l = current.limiter;
        const auto& m = to.limiter;
        l.ceilingDb = lerp (from.limiter.ceilingDb, m.ceilingDb, a);
        l.releaseMs = lerp (from.limiter.releaseMs, m.releaseMs, a);
        l.outputDb = lerp (from.limiter.outputDb, m.outputDb, a);
    }

    void VocalEngine::smoothTowardsTarget (int numSamples) noexcept
    {
        if (! haveSettings)
        {
            settings = target;
            haveSettings = true;
            return;
        }

        // Everything snaps to the target (discrete / mode settings, Learn Range, the learned
        // range, and any field added later), then the continuous ones glide from where they were.
        // Listing the snapped fields one by one once left Learn Range, the learned detector range
        // and Vibrato Respect stuck at their first-block values.
        // The FX block glides its own delay / pre-delay times and ramps its mixes and on / off,
        // so its settings snap (a mix of 0 must reach exactly 0 for a bit-transparent path); the
        // harmonizer de-zippers its own gains, pans and delays.
        const auto from = settings;
        settings = target;

        const auto coeff = 1.0f - std::exp (-(float) std::max (1, numSamples)
                                            / (smoothMs * 0.001f * (float) sampleRate));
        lerpSettings (settings, from, target, std::clamp (coeff, 0.0f, 1.0f));
    }

    void VocalEngine::process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        const auto n = numSamples;
        if (n <= 0)
            return;

        for (int offset = 0; offset < n;)
        {
            const auto chunk = std::min (std::min (smoothBlock, n - offset), maxBlock);
            smoothTowardsTarget (chunk);

            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), offset, chunk);

            pitch.process (slice, chunk, settings.pitch, settings.harmony, settings.nonRealtime, pitchTap);

            // The limiter's slot in the order no longer matters (it is always last), so orders
            // that differ only there are the same chain: no crossfade between them.
            const auto wanted = chainOrderToIndex (withLimiterLast (chainOrderFromIndex (std::clamp (settings.chainOrder, 0, numChainOrders - 1))));
            MeterBlock meter;

            if (activeOrder < 0)
                activeOrder = wanted;

            if (wanted != activeOrder && fadeRemaining == 0)
            {
                fadeOrder = wanted;
                fadeRemaining = fadeLength;
                chains[(size_t) (activeChain ^ 1)].reset();
            }

            auto* meterBlock = meterTap != nullptr ? &meter : nullptr;

            if (fadeRemaining > 0)
            {
                auto& incoming = chains[(size_t) (activeChain ^ 1)];
                fadeBuffer.copyFrom (0, 0, slice, 0, 0, chunk);
                if (slice.getNumChannels() > 1)
                    fadeBuffer.copyFrom (1, 0, slice, 1, 0, chunk);
                else
                    fadeBuffer.copyFrom (1, 0, slice, 0, 0, chunk);

                chains[(size_t) activeChain].process (slice, chunk, settings, chainOrderFromIndex (activeOrder),
                                                      nullptr, nullptr);
                incoming.process (fadeBuffer, chunk, settings, chainOrderFromIndex (fadeOrder),
                                  meterBlock, meterTap);

                const auto done = fadeLength - fadeRemaining;

                for (int ch = 0; ch < slice.getNumChannels(); ++ch)
                {
                    auto* out = slice.getWritePointer (ch);
                    const auto* in = fadeBuffer.getReadPointer (std::min (ch, 1));

                    for (int i = 0; i < chunk; ++i)
                    {
                        const auto x = std::min (1.0f, (float) (done + i + 1) / (float) fadeLength);
                        out[i] += (in[i] - out[i]) * x;
                    }
                }

                fadeRemaining = std::max (0, fadeRemaining - chunk);

                if (fadeRemaining == 0)
                {
                    activeChain ^= 1;
                    activeOrder = fadeOrder;
                }
            }
            else
            {
                chains[(size_t) activeChain].process (slice, chunk, settings, chainOrderFromIndex (activeOrder),
                                                      meterBlock, meterTap);
            }

            // Time-based FX (delay + reverb) after the chain: adds no latency.
            fx.process (slice, chunk, settings.reverb, settings.delay, settings.fxRouting, settings.bpm);

            outputGain.process (slice, chunk, settings.limiter.outputDb);

            // The limiter is the last stage, after the wet signal and the output trim, so the
            // final output never exceeds the ceiling (the trim drives into it rather than past it).
            const auto peakOfSlice = [&slice, chunk]
            {
                return std::max (slice.getMagnitude (0, 0, chunk),
                                 slice.getNumChannels() > 1 ? slice.getMagnitude (1, 0, chunk) : 0.0f);
            };

            if (meterTap != nullptr)
                meter.limiterIn = peakOfSlice();

            auto limiterSettings = settings.limiter;
            limiterSettings.outputDb = 0.0f; // the trim is applied above, in front of the limiter
            limiter.process (slice, 0, chunk, limiterSettings);
            lastGrDb = limiter.getLastGainReductionDb();

            if (meterTap != nullptr)
            {
                meter.limiterOut = peakOfSlice();
                meter.limiterGrDb = lastGrDb;
                meter.output = meter.limiterOut;
                meterTap->addBlock (meter, chunk);
            }

            offset += chunk;
        }
    }
}
