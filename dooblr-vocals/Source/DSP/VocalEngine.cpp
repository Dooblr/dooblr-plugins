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
        reset();
    }

    void VocalEngine::reset()
    {
        pitch.reset();
        chains[0].reset();
        chains[1].reset();
        outputGain.reset();
        fx.reset();
        activeChain = 0;
        activeOrder = 0;
        fadeRemaining = 0;
        lastGrDb = 0.0f;
        haveSettings = false;
        smoothCounter = 0;
    }

    void VocalEngine::lerpSettings (VocalSettings& current, const VocalSettings& to, float a) noexcept
    {
        auto& p = current.pitch;
        const auto& q = to.pitch;
        p.retuneMs = lerp (p.retuneMs, q.retuneMs, a);
        p.humanize = lerp (p.humanize, q.humanize, a);
        p.amount = lerp (p.amount, q.amount, a);
        p.a4Hz = lerp (p.a4Hz, q.a4Hz, a);

        auto& g = current.gate;
        const auto& h = to.gate;
        g.thresholdDb = lerp (g.thresholdDb, h.thresholdDb, a);
        g.attackMs = lerp (g.attackMs, h.attackMs, a);
        g.holdMs = lerp (g.holdMs, h.holdMs, a);
        g.releaseMs = lerp (g.releaseMs, h.releaseMs, a);
        g.rangeDb = lerp (g.rangeDb, h.rangeDb, a);

        auto& e = current.eq;
        const auto& f = to.eq;
        e.lowFreq = lerp (e.lowFreq, f.lowFreq, a);
        e.lowGainDb = lerp (e.lowGainDb, f.lowGainDb, a);
        e.midFreq = lerp (e.midFreq, f.midFreq, a);
        e.midGainDb = lerp (e.midGainDb, f.midGainDb, a);
        e.midQ = lerp (e.midQ, f.midQ, a);
        e.highFreq = lerp (e.highFreq, f.highFreq, a);
        e.highGainDb = lerp (e.highGainDb, f.highGainDb, a);

        auto& c = current.compressor;
        const auto& d = to.compressor;
        c.thresholdDb = lerp (c.thresholdDb, d.thresholdDb, a);
        c.ratio = lerp (c.ratio, d.ratio, a);
        c.attackMs = lerp (c.attackMs, d.attackMs, a);
        c.releaseMs = lerp (c.releaseMs, d.releaseMs, a);
        c.makeupDb = lerp (c.makeupDb, d.makeupDb, a);

        auto& l = current.limiter;
        const auto& m = to.limiter;
        l.ceilingDb = lerp (l.ceilingDb, m.ceilingDb, a);
        l.releaseMs = lerp (l.releaseMs, m.releaseMs, a);
        l.outputDb = lerp (l.outputDb, m.outputDb, a);
    }

    void VocalEngine::smoothTowardsTarget (int numSamples) noexcept
    {
        if (! haveSettings)
        {
            settings = target;
            haveSettings = true;
            return;
        }

        // Discrete / mode parameters snap; continuous ones glide.
        settings.pitch.enabled = target.pitch.enabled;
        settings.pitch.mode = target.pitch.mode;
        settings.pitch.key = target.pitch.key;
        settings.pitch.scale = target.pitch.scale;
        settings.pitch.customMask = target.pitch.customMask;
        settings.pitch.formant = target.pitch.formant;
        settings.pitch.input = target.pitch.input;
        settings.pitch.quality = target.pitch.quality;
        settings.pitch.latency = target.pitch.latency;
        settings.harmony = target.harmony; // the harmonizer de-zippers its own gains, pans and delays
        settings.gate.enabled = target.gate.enabled;
        settings.eq.enabled = target.eq.enabled;
        settings.compressor.enabled = target.compressor.enabled;
        settings.limiter.mode = target.limiter.mode;
        settings.chainOrder = target.chainOrder;
        settings.nonRealtime = target.nonRealtime;
        // The FX block glides its own delay / pre-delay times and ramps its mixes and on / off,
        // so its settings snap (a mix of 0 must reach exactly 0 for a bit-transparent path).
        settings.reverb = target.reverb;
        settings.delay = target.delay;
        settings.fxRouting = target.fxRouting;
        settings.bpm = target.bpm;

        const auto coeff = 1.0f - std::exp (-(float) std::max (1, numSamples)
                                            / (smoothMs * 0.001f * (float) sampleRate));
        lerpSettings (settings, target, std::clamp (coeff, 0.0f, 1.0f));
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

            const auto wanted = std::clamp (settings.chainOrder, 0, numChainOrders - 1);
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

            lastGrDb = chains[(size_t) (fadeRemaining > 0 ? activeChain ^ 1 : activeChain)].getLastGainReductionDb();

            // Time-based FX (delay + reverb) after the chain: adds no latency.
            fx.process (slice, chunk, settings.reverb, settings.delay, settings.fxRouting, settings.bpm);

            outputGain.process (slice, chunk, settings.limiter.outputDb);

            if (meterTap != nullptr)
            {
                meter.output = std::max (slice.getMagnitude (0, 0, chunk),
                                         slice.getNumChannels() > 1 ? slice.getMagnitude (1, 0, chunk) : 0.0f);
                meterTap->addBlock (meter, chunk);
            }

            offset += chunk;
        }
    }
}
