#include "PitchEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr float pi = 3.14159265358979323846f;

        int wrap (int i, int n) noexcept { return ((i % n) + n) % n; }

        float hann (int i, int n) noexcept
        {
            if (n <= 1)
                return 1.0f;
            return 0.5f - 0.5f * std::cos (2.0f * pi * (float) i / (float) (n - 1));
        }

        float rmsOf (const float* x, int n) noexcept
        {
            double s = 0.0;
            for (int i = 0; i < n; ++i)
                s += (double) x[i] * (double) x[i];
            return (float) std::sqrt (s / std::max (1, n));
        }
    }

    //==============================================================================
    void PitchDetector::prepare (double newSampleRate, int maxWindow)
    {
        sampleRate = newSampleRate;
        diff.assign ((size_t) std::max (4, maxWindow / 2 + 2), 0.0f);
    }

    void PitchDetector::reset() noexcept
    {
        std::fill (diff.begin(), diff.end(), 0.0f);
    }

    int PitchDetector::recommendedWindow (float minHz) const noexcept
    {
        const auto period = sampleRate / std::max (40.0f, minHz);
        const auto w = (int) std::ceil (2.5 * period);
        int n = 512;
        while (n < w)
            n <<= 1;
        return std::min (n, (int) diff.size() * 2);
    }

    PitchEstimate PitchDetector::analyse (const float* x, int window, float minHz, float maxHz,
                                          float yinThreshold, bool hq) const noexcept
    {
        PitchEstimate out;
        if (x == nullptr || window < 64 || sampleRate <= 0.0)
            return out;

        const auto rms = rmsOf (x, window);
        if (rms < 0.003f) // ~ -50 dBFS: breaths and gaps stay unvoiced
            return out;

        const auto tauMin = std::max (2, (int) std::floor (sampleRate / std::max (maxHz, minHz + 1.0f)));
        const auto tauMax = std::min ((int) diff.size() - 2,
                                      std::min (window / 2 - 2, (int) std::ceil (sampleRate / std::max (40.0f, minHz))));
        if (tauMax <= tauMin + 2)
            return out;

        auto* d = diff.data();
        d[0] = 0.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            const auto last = window - tau;
            for (int j = 0; j < last; ++j)
            {
                const auto delta = (double) x[j] - (double) x[j + tau];
                sum += delta * delta;
            }
            d[tau] = (float) sum;
        }

        double running = 0.0;
        d[0] = 1.0f;
        auto bestTau = tauMin;
        auto bestCmnd = 1.0f;
        auto found = false;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[tau];
            const auto cmnd = d[tau] * (float) tau / (float) std::max (1.0e-12, running);
            d[tau] = cmnd;

            if (tau >= tauMin && cmnd < bestCmnd)
            {
                bestCmnd = cmnd;
                bestTau = tau;
            }

            if (! found && tau >= tauMin && cmnd < yinThreshold)
            {
                // Local minimum: walk forward while it is still falling.
                while (tau + 1 <= tauMax && d[tau + 1] < d[tau])
                    ++tau;
                bestTau = tau;
                bestCmnd = d[tau];
                found = true;
                if (! hq)
                    break;
            }
        }

        if (! found && bestCmnd > 0.35f)
            return out;

        // Parabolic interpolation around the chosen tau.
        auto tau = (float) bestTau;
        if (bestTau > 1 && bestTau < tauMax)
        {
            const auto s0 = d[bestTau - 1], s1 = d[bestTau], s2 = d[bestTau + 1];
            const auto denom = 2.0f * s1 - s0 - s2;
            if (std::abs (denom) > 1.0e-12f)
                tau += 0.5f * (s0 - s2) / denom;
        }

        const auto hz = (float) sampleRate / std::max (2.0f, tau);
        if (hz < minHz * 0.9f || hz > maxHz * 1.1f)
            return out;

        out.hz = hz;
        out.confidence = std::clamp (1.0f - bestCmnd, 0.0f, 1.0f);
        out.voiced = out.confidence > (hq ? 0.35f : 0.40f);
        return out;
    }

    //==============================================================================
    void PitchCorrector::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void PitchCorrector::reset() noexcept
    {
        smoothedHz = 0.0f;
        holdHz = 0.0f;
        holdSamples = 0;
    }

    float PitchCorrector::hzToMidi (float hz, float a4) noexcept
    {
        return 69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / std::max (1.0f, a4));
    }

    float PitchCorrector::midiToHz (float midi, float a4) noexcept
    {
        return std::max (1.0f, a4) * std::exp2 ((midi - 69.0f) / 12.0f);
    }

    float PitchCorrector::snapMidi (float midi, int mask) noexcept
    {
        mask &= 0x0fff;
        if (mask == 0)
            mask = 0x0fff;

        const auto rounded = std::round (midi);
        auto best = rounded;
        auto bestErr = 1.0e9f;

        // Search ±12 semitones so a note just off the scale still finds a neighbour.
        for (int delta = -12; delta <= 12; ++delta)
        {
            const auto candidate = rounded + (float) delta;
            const auto pc = wrap ((int) std::lround (candidate), 12);
            if (((mask >> pc) & 1) == 0)
                continue;

            const auto err = std::abs (midi - candidate);
            if (err < bestErr)
            {
                bestErr = err;
                best = candidate;
            }
        }

        return best;
    }

    float PitchCorrector::process (float detectedHz, bool voiced, const PitchSettings& s, int hopSamples) noexcept
    {
        if (! voiced || detectedHz < 20.0f)
        {
            holdSamples = 0;
            return 0.0f;
        }

        const auto a4 = std::clamp (s.a4Hz, 415.0f, 466.0f);
        const auto midi = hzToMidi (detectedHz, a4);
        const auto mask = effectiveMask (s.mode, s.scale, s.key, s.customMask);
        const auto targetMidi = snapMidi (midi, mask);
        auto targetHz = midiToHz (targetMidi, a4);

        const auto errorSt = std::abs (midi - targetMidi);
        const auto holdZone = 0.06f + 0.40f * std::clamp (s.humanize, 0.0f, 1.0f);

        // Humanize / hold: near the target, leave more of the sung pitch (sustained notes).
        if (s.humanize > 0.001f && errorSt < holdZone)
        {
            const auto t = errorSt / std::max (1.0e-6f, holdZone);
            const auto towardSnap = t * t;
            const auto keep = std::clamp (s.humanize, 0.0f, 1.0f) * (1.0f - towardSnap);
            targetHz = detectedHz + (targetHz - detectedHz) * (1.0f - keep);
        }

        if (smoothedHz < 20.0f)
        {
            smoothedHz = targetHz;
            return smoothedHz;
        }

        const auto retuneMs = std::max (0.0f, s.retuneMs);
        if (retuneMs <= 0.05f)
        {
            smoothedHz = targetHz;
            return smoothedHz;
        }

        const auto hop = (float) std::max (1, hopSamples);
        const auto coeff = 1.0f - std::exp (-hop / std::max (1.0e-4f, retuneMs * 0.001f * (float) sampleRate));
        smoothedHz += (targetHz - smoothedHz) * std::clamp (coeff, 0.0f, 1.0f);
        return smoothedHz;
    }

    //==============================================================================
    void PitchShifter::prepare (double newSampleRate, int delaySizeToUse)
    {
        sampleRate = newSampleRate;
        delaySize = std::max (64, delaySizeToUse);
        olaSize = std::max (4096, delaySize);
        olaL.assign ((size_t) olaSize, 0.0f);
        olaR.assign ((size_t) olaSize, 0.0f);
        reset();
    }

    void PitchShifter::reset() noexcept
    {
        std::fill (olaL.begin(), olaL.end(), 0.0f);
        std::fill (olaR.begin(), olaR.end(), 0.0f);
        olaWrite = 0;
        synthPhase = 0.0f;
        lastPeriod = 100.0f;
    }

    void PitchShifter::setDelayBuffers (const float* left, const float* right, int size, int newWritePos) noexcept
    {
        delayL = left;
        delayR = right;
        delaySize = size;
        writePos = newWritePos;
    }

    float PitchShifter::readAt (const float* buf, float delay) const noexcept
    {
        if (buf == nullptr || delaySize <= 0)
            return 0.0f;

        delay = std::clamp (delay, 0.0f, (float) delaySize - 2.0f);
        const auto newest = writePos - 1;
        const auto pos = (float) newest - delay;
        auto i0 = (int) std::floor (pos);
        const auto frac = pos - (float) i0;
        i0 = wrap (i0, delaySize);
        const auto i1 = wrap (i0 + 1, delaySize);
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    void PitchShifter::addGrain (int latency, float period, float ratio, bool formant) noexcept
    {
        period = std::clamp (period, 16.0f, (float) delaySize * 0.25f);
        ratio = std::clamp (ratio, 0.5f, 2.0f);

        const auto inLen = 2.0f * period;
        const auto outLen = formant ? inLen : std::max (32.0f, inLen / ratio);
        const auto grain = std::clamp ((int) std::lround (outLen), 32, olaSize / 2);
        const auto scale = 0.5f; // 50 % overlap Hann is ~unity for period hops

        for (int k = 0; k < grain; ++k)
        {
            const auto t = (float) k / (float) std::max (1, grain - 1);
            // Formant on: walk the original 2·T0 grain 1:1. Formant off: resample it by ratio
            // so the spectral envelope moves with the pitch.
            const auto inIndex = formant ? t * inLen : t * inLen;
            const auto delay = (float) latency + inLen - 1.0f - inIndex;
            const auto w = hann (k, grain) * scale;
            const auto idx = wrap (olaWrite + k, olaSize);
            olaL[(size_t) idx] += w * readAt (delayL, delay);
            olaR[(size_t) idx] += w * readAt (delayR, delay);
        }

        lastPeriod = period;
    }

    void PitchShifter::process (float* outL, float* outR, int n, int latency, const float* ratios,
                                const char* voiced, const float* periods, bool formant) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto voicedI = voiced[i] != 0;
            const auto ratio = voicedI ? ratios[i] : 1.0f;
            const auto period = std::max (16.0f, periods[i]);
            const auto outPeriod = std::max (8.0f, period / std::max (0.5f, ratio));

            if (voicedI)
            {
                synthPhase += 1.0f;
                if (synthPhase >= outPeriod)
                {
                    synthPhase -= outPeriod;
                    addGrain (latency, period, ratio, formant);
                }
            }
            else
            {
                synthPhase = 0.0f;
            }

            outL[i] = olaL[(size_t) olaWrite];
            outR[i] = olaR[(size_t) olaWrite];
            olaL[(size_t) olaWrite] = 0.0f;
            olaR[(size_t) olaWrite] = 0.0f;
            olaWrite = wrap (olaWrite + 1, olaSize);
        }
    }

    //==============================================================================
    void PitchEngine::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (1, maxBlockSize);
        const auto hq = hqLatencySamples (sampleRate);
        delaySize = hq + 8192; // extra history for the YIN window
        delayL.assign ((size_t) delaySize, 0.0f);
        delayR.assign ((size_t) delaySize, 0.0f);
        monoScratch.assign ((size_t) delaySize, 0.0f);
        ratioScratch.assign ((size_t) maxBlock, 1.0f);
        voicedScratch.assign ((size_t) maxBlock, 0);
        periodScratch.assign ((size_t) maxBlock, 100.0f);
        wetL.assign ((size_t) maxBlock, 0.0f);
        wetR.assign ((size_t) maxBlock, 0.0f);
        detector.prepare (sampleRate, 8192);
        corrector.prepare (sampleRate);
        shifter.prepare (sampleRate, delaySize);
        reset();
    }

    void PitchEngine::reset()
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        writePos = 0;
        hopCounter = 0;
        lastEstimate = {};
        lastCorrectedHz = 0.0f;
        lastRatio = 1.0f;
        lastVoiced = false;
        mixSmoothed = 1.0f;
        voiceEnv = 0.0f;
        latency = liveLatencySamples (sampleRate);
        detector.reset();
        corrector.reset();
        shifter.reset();
    }

    int PitchEngine::latencySamples (const PitchSettings& s, bool nonRealtime) const noexcept
    {
        const auto hq = nonRealtime || s.quality == QualityMode::hq;
        return hq ? hqLatencySamples (sampleRate) : liveLatencySamples (sampleRate);
    }

    float PitchEngine::readDelay (const std::vector<float>& buf, int delay) const noexcept
    {
        delay = std::clamp (delay, 0, delaySize - 1);
        return buf[(size_t) wrap (writePos - 1 - delay, delaySize)];
    }

    void PitchEngine::analyseIfDue (const PitchSettings& s, bool hq) noexcept
    {
        const auto range = rangeFor (s.input);
        const auto minHz = range.minHz;
        const auto maxHz = range.maxHz;
        const auto window = std::min (detector.recommendedWindow (minHz) * (hq ? 2 : 1), delaySize - latency - 4);
        if (window < 64)
            return;

        for (int i = 0; i < window; ++i)
        {
            const auto d = latency + window - 1 - i;
            monoScratch[(size_t) i] = 0.5f * (readDelay (delayL, d) + readDelay (delayR, d));
        }

        const auto threshold = hq ? 0.10f : 0.15f;
        lastEstimate = detector.analyse (monoScratch.data(), window, minHz, maxHz, threshold, hq);

        // Hop-scaled corrector: treat one hop as the time step.
        // Temporarily scale by calling process once per hop (the 64x factor inside matches live hop).
        lastCorrectedHz = corrector.process (lastEstimate.hz, lastEstimate.voiced, s, hop);
        lastVoiced = lastEstimate.voiced && lastCorrectedHz > 20.0f && lastEstimate.hz > 20.0f;

        if (lastVoiced)
            lastRatio = std::clamp (lastCorrectedHz / lastEstimate.hz, 0.5f, 2.0f);
        else
            lastRatio = 1.0f;
    }

    void PitchEngine::process (juce::AudioBuffer<float>& buffer, int numSamples, const PitchSettings& s,
                               bool nonRealtime, PitchTap* tap) noexcept
    {
        const auto n = std::min (numSamples, maxBlock);
        const auto channels = std::min (2, buffer.getNumChannels());
        if (n <= 0 || channels <= 0)
            return;

        auto* left = buffer.getWritePointer (0);
        auto* right = channels > 1 ? buffer.getWritePointer (1) : left;

        const auto hq = nonRealtime || s.quality == QualityMode::hq;
        latency = latencySamples (s, nonRealtime);
        hop = hq ? 32 : 64;

        const auto mixTarget = s.enabled ? std::clamp (s.amount, 0.0f, 1.0f) : 0.0f;
        const auto mixCoeff = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        const auto voiceCoeff = 1.0f - std::exp (-1.0f / (0.008f * (float) sampleRate));

        for (int i = 0; i < n; ++i)
        {
            delayL[(size_t) writePos] = left[i];
            delayR[(size_t) writePos] = right[i];
            writePos = wrap (writePos + 1, delaySize);

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyseIfDue (s, hq);

                if (tap != nullptr)
                    tap->push ({ lastEstimate.hz, lastVoiced ? lastCorrectedHz : 0.0f, lastEstimate.confidence, lastVoiced });
            }

            mixSmoothed += (mixTarget - mixSmoothed) * mixCoeff;
            ratioScratch[(size_t) i] = lastRatio;
            voicedScratch[(size_t) i] = (char) (lastVoiced && s.enabled);
            periodScratch[(size_t) i] = lastEstimate.hz > 20.0f ? (float) sampleRate / lastEstimate.hz
                                                                : (float) sampleRate / 200.0f;
        }

        shifter.setDelayBuffers (delayL.data(), delayR.data(), delaySize, writePos);
        shifter.process (wetL.data(), wetR.data(), n, latency, ratioScratch.data(),
                         voicedScratch.data(), periodScratch.data(), s.formant);

        for (int i = 0; i < n; ++i)
        {
            const auto dryL = readDelay (delayL, latency + (n - 1 - i));
            const auto dryR = readDelay (delayR, latency + (n - 1 - i));
            const auto want = (voicedScratch[(size_t) i] && mixSmoothed > 1.0e-4f) ? 1.0f : 0.0f;
            voiceEnv += (want - voiceEnv) * voiceCoeff;
            const auto wetAmt = voiceEnv * mixSmoothed;
            left[i] = dryL + wetAmt * (wetL[(size_t) i] - dryL);
            if (channels > 1)
                right[i] = dryR + wetAmt * (wetR[(size_t) i] - dryR);
        }
    }
}
