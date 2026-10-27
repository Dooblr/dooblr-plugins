#include "PitchEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr float pi = 3.14159265358979323846f;

        int wrap (int i, int n) noexcept { return ((i % n) + n) % n; }


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
        rawDiff.assign (diff.size(), 0.0f);
    }

    void PitchDetector::reset() noexcept
    {
        std::fill (diff.begin(), diff.end(), 0.0f);
        std::fill (rawDiff.begin(), rawDiff.end(), 0.0f);
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
        auto* raw = rawDiff.data();
        d[0] = raw[0] = 0.0f;

        // Same integration length for every lag: with a shrinking (window - tau) sum the
        // difference function tilts towards longer lags and the interpolated period is biased
        // by several cents.
        const auto length = window - tauMax;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < length; ++j)
            {
                const auto delta = (double) x[j] - (double) x[j + tau];
                sum += delta * delta;
            }
            d[tau] = raw[tau] = (float) sum;
        }

        double running = 0.0;
        d[0] = 1.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[tau];
            d[tau] = d[tau] * (float) tau / (float) std::max (1.0e-12, running);
        }

        // Classic YIN: smallest tau below the threshold, then the local minimum.
        // A later global min is usually 2× / 3× the period of a clean sine.
        auto bestTau = tauMin;
        auto bestCmnd = 1.0f;
        auto found = false;

        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (d[tau] < bestCmnd)
            {
                bestCmnd = d[tau];
                bestTau = tau;
            }

            if (! found && d[tau] < yinThreshold)
            {
                while (tau + 1 <= tauMax && d[tau + 1] < d[tau])
                    ++tau;
                bestTau = tau;
                bestCmnd = d[tau];
                found = true;
                break;
            }
        }

        if (! found && bestCmnd > 0.35f)
            return out;

        // Parabolic interpolation on the raw difference function (YIN step 5): the cumulative
        // mean normalisation skews the dip, the raw function is symmetric around the period.
        for (int step = 0; step < 2 && bestTau + 1 < tauMax && raw[bestTau + 1] < raw[bestTau]; ++step)
            ++bestTau;
        for (int step = 0; step < 2 && bestTau - 1 > 1 && raw[bestTau - 1] < raw[bestTau]; ++step)
            --bestTau;

        auto tau = (float) bestTau;
        if (bestTau > 1 && bestTau < tauMax)
        {
            const auto s0 = (double) raw[bestTau - 1], s1 = (double) raw[bestTau], s2 = (double) raw[bestTau + 1];
            // Vertex of the parabola through (-1, s0), (0, s1), (1, s2).
            const auto curvature = s0 - 2.0 * s1 + s2;
            if (curvature > 1.0e-12)
                tau += (float) std::clamp (0.5 * (s0 - s2) / curvature, -1.0, 1.0);
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
    void PitchShifter::prepare (double newSampleRate, int maxGrainHalf)
    {
        sampleRate = newSampleRate;
        olaSize = 1024;
        while (olaSize < 2 * std::max (64, maxGrainHalf) + 8)
            olaSize <<= 1;
        olaMask = olaSize - 1;
        maxHalf = olaSize / 2 - 4;
        olaL.assign ((size_t) olaSize, 0.0f);
        olaR.assign ((size_t) olaSize, 0.0f);
        olaW.assign ((size_t) olaSize, 0.0f);
        reset();
    }

    void PitchShifter::reset() noexcept
    {
        std::fill (olaL.begin(), olaL.end(), 0.0f);
        std::fill (olaR.begin(), olaR.end(), 0.0f);
        std::fill (olaW.begin(), olaW.end(), 0.0f);
        lastOut = std::numeric_limits<std::int64_t>::min();
        newestInput = 0;
        marking = false;
        synthMark = analysisEpoch = 0.0;
    }

    void PitchShifter::setDelayBuffers (const float* left, const float* right, int size) noexcept
    {
        delayL = left;
        delayR = right;
        delaySize = size;
    }

    float PitchShifter::readAbs (const float* buf, double t) const noexcept
    {
        if (buf == nullptr || delaySize <= 2)
            return 0.0f;

        t = std::clamp (t, (double) (newestInput - delaySize + 2), (double) newestInput);
        const auto i0 = (std::int64_t) std::floor (t);
        const auto frac = (float) (t - (double) i0);
        const auto a = (int) (((i0 % delaySize) + delaySize) % delaySize);
        const auto b = a + 1 == delaySize ? 0 : a + 1;
        return buf[a] + frac * (buf[b] - buf[a]);
    }

    namespace
    {
        /** Grain half-length (output samples) for period T at the ratio, limited so the grain's
            input never reaches past the newest sample (lookahead = latency). */
        float grainHalf (float period, float ratio, bool formant, float lookahead, int maxHalf) noexcept
        {
            const auto hopOut = period / ratio;
            const auto stretch = formant ? 1.0f : ratio;
            const auto wanted = formant ? std::max (period, hopOut) : hopOut;
            const auto fit = (lookahead - 4.0f - 0.5f * period) / (1.0f + stretch);
            return std::clamp (std::min (wanted, fit), 8.0f, (float) maxHalf);
        }
    }

    void PitchShifter::addGrain (float period, float ratio, bool formant) noexcept
    {
        const auto lookahead = (float) (newestInput - lastOut - 1);
        const auto half = grainHalf (period, ratio, formant, lookahead, maxHalf);
        const auto stretch = (double) (formant ? 1.0f : ratio);
        const auto T = (double) period;

        // Pitch-synchronous analysis epoch nearest this mark: epochs step by whole periods, so
        // every grain starts on the same phase of the waveform.
        while (analysisEpoch + 0.5 * T <= synthMark) analysisEpoch += T;
        while (analysisEpoch - 0.5 * T > synthMark) analysisEpoch -= T;
        while (analysisEpoch + (double) half * stretch > (double) newestInput && analysisEpoch - T > (double) (newestInput - delaySize + 4))
            analysisEpoch -= T;

        const auto now = lastOut + 1; // the output sample being produced
        const auto first = std::max (now, (std::int64_t) std::ceil (synthMark - (double) half));
        const auto last = (std::int64_t) std::floor (synthMark + (double) half);
        const auto invHalf = 1.0 / (double) half;

        for (auto j = first; j <= last; ++j)
        {
            const auto d = (double) j - synthMark;
            const auto w = (float) (0.5 + 0.5 * std::cos ((double) pi * d * invHalf));
            if (w <= 0.0f)
                continue;
            const auto x = analysisEpoch + d * stretch;
            const auto idx = (size_t) (j & olaMask);
            olaL[idx] += w * readAbs (delayL, x);
            olaR[idx] += w * readAbs (delayR, x);
            olaW[idx] += w;
        }

        synthMark += T / (double) ratio;
    }

    void PitchShifter::processSample (std::int64_t tOut, std::int64_t newest, float ratio, float period,
                                      bool voiced, bool formant, float& outL, float& outR) noexcept
    {
        newestInput = newest;

        if (tOut != lastOut + 1 || lastOut == std::numeric_limits<std::int64_t>::min())
        {
            // First call, or the latency changed (Live <-> HQ): start a fresh time line.
            std::fill (olaL.begin(), olaL.end(), 0.0f);
            std::fill (olaR.begin(), olaR.end(), 0.0f);
            std::fill (olaW.begin(), olaW.end(), 0.0f);
            marking = false;
        }

        lastOut = tOut - 1; // addGrain() writes from tOut on
        ratio = std::clamp (ratio, 0.5f, 2.0f);
        period = std::clamp (period, 16.0f, (float) maxHalf);

        if (voiced)
        {
            const auto lookahead = (float) (newest - tOut);

            if (! marking)
            {
                marking = true;
                synthMark = (double) tOut + (double) grainHalf (period, ratio, formant, lookahead, maxHalf);
                analysisEpoch = synthMark;
            }

            for (int guard = 0; guard < 32; ++guard)
            {
                if (synthMark - (double) grainHalf (period, ratio, formant, lookahead, maxHalf) > (double) tOut)
                    break;
                addGrain (period, ratio, formant);
            }
        }
        else
        {
            marking = false;
        }

        const auto idx = (size_t) (tOut & olaMask);
        const auto w = olaW[idx];

        if (w >= 1.0f || (marking && w > 1.0e-3f))
        {
            outL = olaL[idx] / w;
            outR = olaR[idx] / w;
        }
        else
        {
            // Voicing on / off edges: what the grains do not cover is the dry input.
            const auto rest = 1.0f - std::max (0.0f, w);
            outL = olaL[idx] + rest * readAbs (delayL, (double) tOut);
            outR = olaR[idx] + rest * readAbs (delayR, (double) tOut);
        }

        olaL[idx] = olaR[idx] = olaW[idx] = 0.0f;
        lastOut = tOut;
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
        detector.prepare (sampleRate, 8192);
        corrector.prepare (sampleRate);
        shifter.prepare (sampleRate, hq + 64);
        shifter.setDelayBuffers (delayL.data(), delayR.data(), delaySize);
        reset();
    }

    void PitchEngine::reset()
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        writePos = 0;
        inputCount = 0;
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
        auto window = std::min (detector.recommendedWindow (minHz) * (hq ? 2 : 1), delaySize - latency - 4);
        if (window < 64)
            return;

        // Centre the window where the next grains are taken: one period after the playback
        // point (latency samples back). A window ending at the playback point lags the audio
        // being shifted by half a window, which smears vibrato and glides.
        window = std::min (window, 2 * latency);
        const auto centre = window / 2;
        const auto ahead = lastEstimate.hz > 20.0f
                               ? std::clamp ((int) std::lround (sampleRate / lastEstimate.hz), 0, std::max (0, latency - centre))
                               : 0;

        for (int i = 0; i < window; ++i)
        {
            const auto d = std::max (0, latency - ahead + centre - 1 - i);
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

        const auto period = [this] { return lastEstimate.hz > 20.0f ? (float) sampleRate / lastEstimate.hz
                                                                    : (float) sampleRate / 200.0f; };

        for (int i = 0; i < n; ++i)
        {
            delayL[(size_t) writePos] = left[i];
            delayR[(size_t) writePos] = right[i];
            writePos = wrap (writePos + 1, delaySize);
            ++inputCount;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyseIfDue (s, hq);

                if (tap != nullptr)
                    tap->push ({ lastEstimate.hz, lastVoiced ? lastCorrectedHz : 0.0f, lastEstimate.confidence, lastVoiced });
            }

            mixSmoothed += (mixTarget - mixSmoothed) * mixCoeff;
            const auto voicedNow = lastVoiced && s.enabled;

            // Output time: the input sample written latency samples ago.
            const auto newest = inputCount - 1;
            float wetL = 0.0f, wetR = 0.0f;
            shifter.processSample (newest - latency, newest, lastRatio, period(), voicedNow, s.formant, wetL, wetR);

            const auto dryL = readDelay (delayL, latency);
            const auto dryR = readDelay (delayR, latency);
            const auto want = (voicedNow && mixSmoothed > 1.0e-4f) ? 1.0f : 0.0f;
            voiceEnv += (want - voiceEnv) * voiceCoeff;
            const auto wetAmt = voiceEnv * mixSmoothed;
            left[i] = dryL + wetAmt * (wetL - dryL);
            if (channels > 1)
                right[i] = dryR + wetAmt * (wetR - dryR);
        }
    }
}
