#include "LimiterEngine.h"

#include <algorithm>

namespace dooblrl::dsp
{
    namespace
    {
        float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

        float coeffForMs (double sr, float ms) noexcept
        {
            const auto t = std::max (1.0e-5, (double) ms * 0.001);
            return (float) std::exp (-1.0 / (t * sr));
        }

        void raise (std::atomic<float>& slot, float value) noexcept
        {
            if (! std::isfinite (value))
                return;
            auto cur = slot.load (std::memory_order_relaxed);
            while (value > cur && ! slot.compare_exchange_weak (cur, value, std::memory_order_relaxed)) {}
        }
    }

    int LimiterEngine::factorForIndex (int index) noexcept
    {
        static constexpr int f[] { 1, 2, 4, 8, 16, 32 };
        return f[std::clamp (index, 0, 5)];
    }

    LimiterEngine::StyleAlgo LimiterEngine::algoFor (int style) noexcept
    {
        StyleAlgo a;
        switch ((Style) std::clamp (style, 0, (int) Style::safe))
        {
            case Style::transparent:
                a.attackScale = 0.35f; a.releaseScale = 1.15f; a.kneeBiasDb = 2.5f; a.ratio = 80;
                a.rmsMix = 0.0f; a.overshootDb = 1.8f; a.evenHarm = 0.05f; a.oddHarm = 0.15f;
                break;
            case Style::punchy:
                a.attackScale = 2.6f; a.releaseScale = 0.38f; a.kneeBiasDb = 1.0f; a.ratio = 40;
                a.rmsMix = 0.05f; a.overshootDb = 6.5f; a.evenHarm = 0.2f; a.oddHarm = 0.7f;
                break;
            case Style::dynamicStyle:
                a.attackScale = 1.9f; a.releaseScale = 0.7f; a.kneeBiasDb = 1.5f; a.ratio = 50;
                a.rmsMix = 0.1f; a.overshootDb = 5.5f; a.enhanceBeforeLimit = true;
                a.evenHarm = 0.45f; a.oddHarm = 0.55f;
                break;
            case Style::allround:
                a.attackScale = 1.0f; a.releaseScale = 1.0f; a.kneeBiasDb = 0.0f; a.ratio = 60;
                a.rmsMix = 0.35f; a.overshootDb = 4.0f; a.evenHarm = 0.25f; a.oddHarm = 0.5f;
                break;
            case Style::aggressive:
                a.attackScale = 0.18f; a.releaseScale = 0.22f; a.kneeBiasDb = -1.5f; a.ratio = 200;
                a.rmsMix = 0.0f; a.overshootDb = 1.2f; a.softClip = true; a.clipDrive = 2.4f;
                a.evenHarm = 0.15f; a.oddHarm = 0.9f;
                break;
            case Style::modern:
                a.attackScale = 0.45f; a.releaseScale = 0.85f; a.kneeBiasDb = 0.5f; a.ratio = 120;
                a.rmsMix = 0.15f; a.overshootDb = 2.2f; a.dcBlock = true;
                a.evenHarm = 0.08f; a.oddHarm = 0.28f;
                break;
            case Style::bus:
                a.attackScale = 1.4f; a.releaseScale = 2.8f; a.kneeBiasDb = 1.0f; a.ratio = 12;
                a.rmsMix = 0.72f; a.overshootDb = 3.5f; a.programRelease = true;
                a.evenHarm = 0.35f; a.oddHarm = 0.4f;
                break;
            case Style::safe:
                a.attackScale = 0.25f; a.releaseScale = 1.4f; a.kneeBiasDb = 5.0f; a.ratio = 40;
                a.rmsMix = 0.2f; a.overshootDb = 0.6f; a.extraCeilingMarginDb = 0.35f;
                a.minLookaheadMs = 5.0f; a.noHarmonics = true; a.dcBlock = true;
                a.evenHarm = 0; a.oddHarm = 0;
                break;
            case Style::numStyles:
                break;
        }
        return a;
    }

    void LimiterEngine::ensureOversampler (int index, int block)
    {
        const auto clamped = std::clamp (index, 0, 5);
        if (clamped == 0)
        {
            activeOs = nullptr;
            osFactor = 1;
            osLatency = 0;
            return;
        }

        const auto slot = clamped - 1; // 0..4 for 2x..32x
        activeOs = oversamplers[(size_t) slot].get();
        osFactor = factorForIndex (clamped);
        osLatency = activeOs != nullptr ? (int) std::llround (activeOs->getLatencyInSamples()) : 0;
        (void) block;
    }

    void LimiterEngine::prepare (double sr, int maxBlk, int oversamplingIndex)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        maxBlock = std::max (16, maxBlk);
        osIndex = std::clamp (oversamplingIndex, 0, 5);

        for (auto& slot : oversamplers)
            slot.reset();

        auto makeOs = [&] (int log2Factor)
        {
            const auto slot = log2Factor - 1;
            oversamplers[(size_t) slot] = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) maxChannels,
                (size_t) log2Factor,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                true,
                true);
            oversamplers[(size_t) slot]->initProcessing ((size_t) maxBlock);
        };

        // 2x / 4x / 8x are always live so the control can switch during playback.
        for (int log2 = 1; log2 <= 3; ++log2)
            makeOs (log2);
        if (osIndex >= 4)
            makeOs (osIndex);

        ensureOversampler (osIndex, maxBlock);

        const auto maxLook = (int) std::llround (maxLookaheadMs * 0.001 * sampleRate);
        hostLookahead = std::max (0, (int) std::llround (std::min (maxLookaheadMs, settings.lookaheadMs) * 0.001 * sampleRate));
        osLookahead = hostLookahead * osFactor;
        reportedLatency = hostLookahead + osLatency + autoCompLatency();

        delaySize = std::max (maxLook * 32 + 64, 256); // enough for 32x at max lookahead
        delayL.assign ((size_t) delaySize, 0.0f);
        delayR.assign ((size_t) delaySize, 0.0f);
        dryL.assign ((size_t) maxBlock, 0.0f);
        dryR.assign ((size_t) maxBlock, 0.0f);
        dryDelaySize = std::max (maxLook + 16384, 512); // host-rate dry delay covers lookahead + OS FIR
        dryDelayL.assign ((size_t) dryDelaySize, 0.0f);
        dryDelayR.assign ((size_t) dryDelaySize, 0.0f);
        acSize = TruePeak::tapsPerPhase + 4;
        acL.assign ((size_t) acSize, 0.0f);
        acR.assign ((size_t) acSize, 0.0f);
        delayWrite = dryDelayWrite = acWrite = 0;

        inTp.prepare (sampleRate);
        outTp.prepare (sampleRate);
        autoTp.prepare (sampleRate * (double) osFactor);
        loudness.prepare (sampleRate);
        reset();
    }

    void LimiterEngine::reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (dryDelayL.begin(), dryDelayL.end(), 0.0f);
        std::fill (dryDelayR.begin(), dryDelayR.end(), 0.0f);
        std::fill (acL.begin(), acL.end(), 0.0f);
        std::fill (acR.begin(), acR.end(), 0.0f);
        delayWrite = dryDelayWrite = acWrite = 0;
        envL = envR = 1.0f;
        rmsL = rmsR = 0.0f;
        autoCompGain = 1.0f;
        dcZL = dcZR = 0.0f;
        shapeZL = shapeZR = shapeZ2L = shapeZ2R = 0.0f;
        unitySmooth = 1.0f;
        targetTrim = 1.0f;
        inTp.reset();
        outTp.reset();
        autoTp.reset();
        if (activeOs != nullptr)
            activeOs->reset();
    }

    void LimiterEngine::resetMeters() noexcept
    {
        loudness.reset();
        inPeakL = inPeakR = outPeakL = outPeakR = 0;
        inTpL = inTpR = outTpL = outTpR = 0;
        grDb = 0;
        mLufs = sLufs = iLufs = -70;
        lraVal = 0;
        integratedFlag = 0;
    }

    Meters LimiterEngine::takeMeters() noexcept
    {
        Meters m;
        m.inputPeakL = inPeakL.exchange (0.0f);
        m.inputPeakR = inPeakR.exchange (0.0f);
        m.outputPeakL = outPeakL.exchange (0.0f);
        m.outputPeakR = outPeakR.exchange (0.0f);
        m.inputTruePeakL = inTpL.exchange (0.0f);
        m.inputTruePeakR = inTpR.exchange (0.0f);
        m.outputTruePeakL = outTpL.exchange (0.0f);
        m.outputTruePeakR = outTpR.exchange (0.0f);
        m.gainReductionDb = grDb.load();
        m.momentaryLufs = mLufs.load();
        m.shortTermLufs = sLufs.load();
        m.integratedLufs = iLufs.load();
        m.lra = lraVal.load();
        m.hasIntegrated = integratedFlag.load() != 0;
        return m;
    }

    float LimiterEngine::detectorGainDb (float peakLin, float rmsLin, const StyleAlgo& a) const noexcept
    {
        const auto mix = clamp01 (a.rmsMix);
        const auto det = peakLin * (1.0f - mix) + rmsLin * mix;
        if (det <= 1.0e-12f)
            return 0.0f;

        const auto knee = std::max (0.0f, settings.kneeDb + a.kneeBiasDb);
        const auto detDb = linearToDb (det);
        const auto threshDb = settings.thresholdDb;
        const auto halfKnee = 0.5f * knee;
        float over = detDb - threshDb;

        if (over <= -halfKnee)
            return 0.0f;

        float gr;
        if (knee <= 1.0e-4f || over >= halfKnee)
        {
            gr = over * (1.0f - 1.0f / std::max (1.0f, a.ratio));
        }
        else
        {
            const auto x = over + halfKnee;
            const auto t = x / std::max (1.0e-4f, knee);
            gr = over * (1.0f - 1.0f / std::max (1.0f, a.ratio)) * t * t;
        }

        return gr;
    }

    float LimiterEngine::enhanceResidual (float x, float amount, const StyleAlgo& a) const noexcept
    {
        if (amount <= 1.0e-8f)
            return x;

        const auto drive = 1.0f + amount * 3.5f;
        const auto odd = std::tanh (x * drive) / std::tanh (drive);
        const auto even = x * x * (x >= 0.0f ? 1.0f : -1.0f);
        const auto evenMix = a.noHarmonics ? 0.0f : a.evenHarm;
        const auto oddMix = a.noHarmonics ? 1.0f : a.oddHarm;
        const auto shaped = odd * oddMix + even * evenMix * amount;
        if (a.noHarmonics)
            return x * (1.0f - 0.15f * amount) + odd * 0.15f * amount; // extra control, no added colour
        return x * (1.0f - amount) + shaped * amount;
    }

    float LimiterEngine::nextRandom() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return ((rng >> 8) & 0xFFFFFFu) / 16777215.0f;
    }

    void LimiterEngine::applyDither (float& l, float& r) noexcept
    {
        if (settings.dither <= 0)
            return;

        const auto bits = settings.dither == 1 ? 16 : 24;
        const auto lsb = std::ldexp (1.0f, 1 - bits); // 2^(1-bits)

        auto tpdf = [this, lsb]()
        {
            return ((nextRandom() + nextRandom()) - 1.0f) * lsb;
        };

        auto shape = [&] (float& x, float& z1, float& z2)
        {
            const auto d = tpdf();
            switch (settings.shaping)
            {
                case 0: x += d; break;                         // flat TPDF
                case 1: x += d - 0.5f * z1; z1 = d; break;     // 1st-order
                case 2: x += d - z1 + 0.5f * z2; z2 = z1; z1 = d; break;
                default: // high: emphasise HF
                {
                    const auto y = d - 1.5f * z1 + 0.6f * z2;
                    z2 = z1; z1 = d;
                    x += y;
                    break;
                }
            }
        };

        shape (l, shapeZL, shapeZ2L);
        shape (r, shapeZR, shapeZ2R);

        const auto q = std::ldexp (1.0f, 1 - bits);
        l = std::round (l / q) * q;
        r = std::round (r / q) * q;
    }

    void LimiterEngine::processSample (float& l, float& r, bool skipEnhance) noexcept
    {
        const auto a = algoFor (settings.style);
        const auto osRate = sampleRate * (double) osFactor;

        if (a.dcBlock)
        {
            const auto dcC = coeffForMs (osRate, 15.0f);
            const auto nl = l - dcZL; dcZL = l + dcC * (dcZL - l); l = nl;
            const auto nr = r - dcZR; dcZR = r + dcC * (dcZR - r); r = nr;
        }

        // Write current (future relative to the delayed output) into the delay line.
        delayL[(size_t) delayWrite] = l;
        delayR[(size_t) delayWrite] = r;

        const auto read = (delayWrite - osLookahead + delaySize) % delaySize;
        auto xL = delayL[(size_t) read];
        auto xR = delayR[(size_t) read];
        delayWrite = (delayWrite + 1) % delaySize;

        // Peak of the upcoming window: current sample is the lookahead "future".
        const auto peakL = std::abs (l);
        const auto peakR = std::abs (r);

        const auto rmsC = coeffForMs (osRate, 50.0f);
        rmsL = peakL + (rmsL - peakL) * rmsC;
        rmsR = peakR + (rmsR - peakR) * rmsC;

        const auto tLink = clamp01 (settings.transientLink);
        const auto rLink = clamp01 (settings.releaseLink);
        const auto maxPeak = std::max (peakL, peakR);
        const auto maxRms  = std::max (rmsL, rmsR);

        const auto atkL = peakL + (maxPeak - peakL) * tLink;
        const auto atkR = peakR + (maxPeak - peakR) * tLink;
        const auto relDetL = rmsL + (maxRms - rmsL) * rLink;
        const auto relDetR = rmsR + (maxRms - rmsR) * rLink;

        const auto wantL = dbToLinear (-detectorGainDb (atkL, relDetL, a));
        const auto wantR = dbToLinear (-detectorGainDb (atkR, relDetR, a));

        auto follow = [&] (float& env, float want, float atkMs, float relMs)
        {
            // Overshoot is extra attack: transients leak into Enhance, sustained GR is full.
            const auto atk = coeffForMs (osRate, std::max (0.01f, atkMs * a.attackScale * (1.0f + a.overshootDb * 0.22f)));
            auto relMsAdj = std::max (1.0f, relMs * a.releaseScale);
            if (a.programRelease && env < 0.5f)
                relMsAdj *= 2.2f;
            const auto rel = coeffForMs (osRate, relMsAdj);
            if (want < env)
                env = want + (env - want) * atk;
            else
                env = want + (env - want) * rel;
        };

        follow (envL, wantL, settings.attackMs, settings.releaseMs);
        follow (envR, wantR, settings.attackMs, settings.releaseMs);

        // Dynamic style: reshape transients *before* the GR is applied.
        if (a.enhanceBeforeLimit)
        {
            const auto fastL = peakL, fastR = peakR;
            const auto transL = std::max (0.0f, fastL - rmsL);
            const auto transR = std::max (0.0f, fastR - rmsR);
            const auto boost = 0.55f;
            xL += xL * transL * boost;
            xR += xR * transR * boost;
        }

        xL *= envL;
        xR *= envR;

        const auto enhanceAmt = skipEnhance ? 0.0f : clamp01 (settings.enhance * 0.01f);
        if (enhanceAmt > 1.0e-8f)
        {
            const auto threshLin = dbToLinear (settings.thresholdDb);
            auto reshape = [&] (float y)
            {
                const auto mag = std::abs (y);
                if (mag <= threshLin)
                    return y;
                const auto sign = y < 0.0f ? -1.0f : 1.0f;
                const auto body = sign * threshLin;
                const auto residual = y - body;
                return body + enhanceResidual (residual, enhanceAmt, a);
            };
            xL = reshape (xL);
            xR = reshape (xR);

            // Fast residual-peak control so Enhance stays under the ceiling.
            const auto ceilLinWork = dbToLinear (settings.ceilingDb - a.extraCeilingMarginDb);
            const auto mag = std::max (std::abs (xL), std::abs (xR));
            if (mag > ceilLinWork && mag > 1.0e-12f)
            {
                const auto g = ceilLinWork / mag;
                xL *= g;
                xR *= g;
            }
        }

        if (a.softClip)
        {
            const auto ceilLin = dbToLinear (settings.ceilingDb);
            const auto d = std::max (1.05f, a.clipDrive);
            const auto n = std::tanh (d);
            xL = std::tanh (xL / std::max (1.0e-8f, ceilLin) * d) / n * ceilLin;
            xR = std::tanh (xR / std::max (1.0e-8f, ceilLin) * d) / n * ceilLin;
        }

        l = xL;
        r = xR;
    }

    void LimiterEngine::processOversampled (juce::dsp::AudioBlock<float> block, bool skipEnhance)
    {
        const auto n = (int) block.getNumSamples();
        const auto nCh = (int) block.getNumChannels();
        auto* l = block.getChannelPointer (0);
        auto* r = nCh > 1 ? block.getChannelPointer (1) : l;
        for (int i = 0; i < n; ++i)
        {
            auto sl = l[i];
            auto sr = r[i];
            processSample (sl, sr, skipEnhance);
            l[i] = sl;
            if (nCh > 1)
                r[i] = sr;
        }
    }

    void LimiterEngine::processHostRate (float* const* io, int numChannels, int numSamples, bool skipEnhance)
    {
        const auto nCh = std::min (maxChannels, std::max (1, numChannels));
        auto* left = io[0];
        auto* right = nCh > 1 ? io[1] : io[0];

        const auto inGain = dbToLinear (settings.inputGainDb);
        const auto a = algoFor (settings.style);
        const auto ceilLin = dbToLinear (settings.ceilingDb);
        const auto n = std::min (numSamples, (int) dryL.size());

        const auto neededIndex = std::clamp (settings.oversamplingIndex, 0, 5);
        if (neededIndex != osIndex)
        {
            // Switching to a factor that was not prepared stays on the current
            // oversampler until the host calls prepareToPlay (no audio-thread alloc).
            if (neededIndex == 0 || oversamplers[(size_t) (neededIndex - 1)] != nullptr)
            {
                osIndex = neededIndex;
                ensureOversampler (osIndex, maxBlock);
                std::fill (delayL.begin(), delayL.end(), 0.0f);
                std::fill (delayR.begin(), delayR.end(), 0.0f);
                delayWrite = 0;
                autoTp.prepare (sampleRate * (double) osFactor);
            }
        }

        const auto lookMs = std::max (settings.lookaheadMs, a.minLookaheadMs);
        hostLookahead = std::max (0, (int) std::llround (std::min (maxLookaheadMs, lookMs) * 0.001 * sampleRate));
        osLookahead = std::min (delaySize - 8, hostLookahead * osFactor);
        reportedLatency = hostLookahead + osLatency + autoCompLatency();

        for (int i = 0; i < n; ++i)
        {
            dryL[(size_t) i] = left[i];
            dryR[(size_t) i] = nCh > 1 ? right[i] : left[i];
            raise (inPeakL, std::abs (left[i]));
            raise (inPeakR, std::abs (nCh > 1 ? right[i] : left[i]));
            raise (inTpL, inTp.pushAndTruePeak (0, left[i]));
            raise (inTpR, inTp.pushAndTruePeak (1, nCh > 1 ? right[i] : left[i]));

            if (dryDelaySize > 0)
            {
                dryDelayL[(size_t) dryDelayWrite] = dryL[(size_t) i];
                dryDelayR[(size_t) dryDelayWrite] = dryR[(size_t) i];
                const auto dryRead = (dryDelayWrite - reportedLatency + dryDelaySize) % dryDelaySize;
                dryL[(size_t) i] = dryDelayL[(size_t) dryRead];
                dryR[(size_t) i] = dryDelayR[(size_t) dryRead];
                dryDelayWrite = (dryDelayWrite + 1) % dryDelaySize;
            }

            left[i] *= inGain;
            if (nCh > 1)
                right[i] *= inGain;
        }

        if (settings.bypass)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto ol = dryL[(size_t) i];
                const auto or_ = dryR[(size_t) i];
                left[i] = ol;
                if (nCh > 1)
                    right[i] = or_;
                raise (outPeakL, std::abs (ol));
                raise (outPeakR, std::abs (or_));
                raise (outTpL, outTp.pushAndTruePeak (0, ol));
                raise (outTpR, outTp.pushAndTruePeak (1, or_));
            }
            loudness.process (left, nCh > 1 ? right : left, n, nCh);
            mLufs.store (loudness.momentaryLufs());
            sLufs.store (loudness.shortTermLufs());
            iLufs.store (loudness.integratedLufs());
            lraVal.store (loudness.loudnessRange());
            integratedFlag.store (loudness.hasIntegrated() ? 1 : 0);
            grDb.store (0.0f);
            return;
        }
        else if (activeOs != nullptr)
        {
            juce::dsp::AudioBlock<float> block (io, (size_t) nCh, (size_t) n);
            auto up = activeOs->processSamplesUp (block);
            processOversampled (up, skipEnhance);
            activeOs->processSamplesDown (block);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                processSample (left[i], nCh > 1 ? right[i] : left[i], skipEnhance);
        }

        // LUFS target trim: slow ride of short-term LUFS once the 3 s window is full.
        const auto target = settings.lufsTarget;
        if (target > 0 && loudness.shortTermReady())
        {
            static constexpr float targets[] { 0.0f, -23.0f, -16.0f, -14.0f, -12.0f };
            const auto want = targets[std::clamp (target, 0, 4)];
            const auto st = loudness.shortTermLufs();
            if (st > -60.0f)
            {
                const auto err = want - st;
                const auto step = coeffForMs (sampleRate, 400.0f);
                const auto toward = dbToLinear (err);
                targetTrim = toward + (targetTrim - toward) * step;
                targetTrim = std::clamp (targetTrim, 0.25f, 4.0f);
            }
        }
        else
        {
            targetTrim = 1.0f + (targetTrim - 1.0f) * coeffForMs (sampleRate, 80.0f);
        }

        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            auto ol = left[i] * targetTrim;
            auto or_ = (nCh > 1 ? right[i] : left[i]) * targetTrim;

            if (settings.unityGain)
            {
                const auto inMag = std::max (std::abs (dryL[(size_t) i]), std::abs (dryR[(size_t) i]));
                const auto outMag = std::max (std::abs (ol), std::abs (or_));
                float want = 1.0f;
                if (outMag > 1.0e-6f && inMag > 1.0e-6f)
                    want = inMag / outMag;
                unitySmooth = want + (unitySmooth - want) * coeffForMs (sampleRate, 40.0f);
                ol *= unitySmooth;
                or_ *= unitySmooth;
            }

            if (settings.delta)
            {
                ol -= dryL[(size_t) i];
                or_ -= dryR[(size_t) i];
            }
            else
            {
                if (settings.autoComp && acSize >= TruePeak::tapsPerPhase)
                {
                    acL[(size_t) acWrite] = ol;
                    acR[(size_t) acWrite] = or_;
                    const auto newest = acWrite;
                    const auto acRead = (acWrite - TruePeak::latency() + acSize) % acSize;
                    const auto tp = std::max (autoTp.peakFromRing (acL.data(), acSize, newest),
                                              autoTp.peakFromRing (acR.data(), acSize, newest));
                    const auto acCeil = dbToLinear (settings.ceilingDb - 0.05f);
                    float want = 1.0f;
                    if (tp > acCeil && tp > 1.0e-12f)
                        want = acCeil / tp;
                    if (want < autoCompGain)
                        autoCompGain = want; // instant attack for the duration of the over
                    else
                        autoCompGain = want + (autoCompGain - want) * coeffForMs (sampleRate, 8.0f);
                    ol = acL[(size_t) acRead] * autoCompGain;
                    or_ = acR[(size_t) acRead] * autoCompGain;
                    acWrite = (acWrite + 1) % acSize;
                }

                // Sample-peak ceiling is always honoured on the program (not on delta).
                ol = std::clamp (ol, -ceilLin, ceilLin);
                or_ = std::clamp (or_, -ceilLin, ceilLin);
                applyDither (ol, or_);
                ol = std::clamp (ol, -ceilLin, ceilLin);
                or_ = std::clamp (or_, -ceilLin, ceilLin);
            }

            left[i] = ol;
            if (nCh > 1)
                right[i] = or_;

            raise (outPeakL, std::abs (ol));
            raise (outPeakR, std::abs (or_));
            raise (outTpL, outTp.pushAndTruePeak (0, ol));
            raise (outTpR, outTp.pushAndTruePeak (1, or_));

            const auto g = std::min (envL, envR);
            maxGr = std::max (maxGr, -linearToDb (std::max (1.0e-6f, g)));
        }

        loudness.process (left, nCh > 1 ? right : left, n, nCh);
        mLufs.store (loudness.momentaryLufs());
        sLufs.store (loudness.shortTermLufs());
        iLufs.store (loudness.integratedLufs());
        lraVal.store (loudness.loudnessRange());
        integratedFlag.store (loudness.hasIntegrated() ? 1 : 0);
        grDb.store (maxGr);
        (void) a;
        (void) skipEnhance;
    }

    void LimiterEngine::process (float* const* io, int numChannels, int numSamples)
    {
        if (io == nullptr || numSamples <= 0)
            return;
        processHostRate (io, numChannels, numSamples, false);
    }

    void LimiterEngine::processPreProcessOnly (float* const* io, int numChannels, int numSamples)
    {
        if (io == nullptr || numSamples <= 0)
            return;
        processHostRate (io, numChannels, numSamples, true);
    }
}
