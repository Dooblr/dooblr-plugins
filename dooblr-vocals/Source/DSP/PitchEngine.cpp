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
        float grainHalf (float period, float ratio, bool formant, float lookahead, int maxHalf, bool causal) noexcept
        {
            const auto hopOut = period / ratio;
            const auto stretch = formant ? 1.0f : ratio;
            const auto wanted = formant ? std::max (period, hopOut) : hopOut;

            if (causal)
                return std::clamp (wanted, 8.0f, (float) maxHalf);

            // Never shorter than the hop (the windows must still cover the output; the epoch
            // then steps back a period instead), else as long as the look-ahead allows.
            const auto fit = (lookahead - 4.0f - 0.5f * period) / (1.0f + stretch);
            return std::clamp (std::max (std::min (wanted, fit), std::min (wanted, hopOut)), 8.0f, (float) maxHalf);
        }
    }

    void PitchShifter::addGrain (float period, float ratio, bool formant, bool causal) noexcept
    {
        const auto lookahead = (float) (newestInput - lastOut - 1);
        const auto half = grainHalf (period, ratio, formant, lookahead, maxHalf, causal);
        const auto stretch = (double) (formant ? 1.0f : ratio);
        const auto T = (double) period;

        // Pitch-synchronous analysis epoch nearest this mark: epochs step by whole periods, so
        // every grain starts on the same phase of the waveform.
        while (analysisEpoch + 0.5 * T <= synthMark) analysisEpoch += T;
        while (analysisEpoch - 0.5 * T > synthMark) analysisEpoch -= T;
        while (analysisEpoch + (double) half * stretch > (double) newestInput && analysisEpoch - T > (double) (newestInput - delaySize + 4))
            analysisEpoch -= T;

        // Harmony voices: snap the epoch to the waveform's maximum within a quarter period (a
        // pitch mark). A unison voice then stays sample-aligned with the lead.
        auto shortHalf = 0.0;
        if (harmonyVoice)
        {
            const auto reach = std::max (1, (int) (0.25 * T));
            const auto centre = (std::int64_t) std::llround (analysisEpoch);
            auto best = analysisEpoch;
            auto bestValue = -1.0e30f;
            for (auto j = centre - reach; j <= centre + reach; ++j)
            {
                if ((double) j + (double) half * stretch > (double) newestInput)
                    break;
                // Signed maximum: always the same side of the waveform, so successive marks
                // never alternate between a positive and a negative peak.
                const auto v = readAbs (delayL, (double) j) + readAbs (delayR, (double) j);
                if (v > bestValue)
                {
                    bestValue = v;
                    best = (double) j;
                }
            }
            // The synthesis mark follows the snap (scaled to the output's time base), so the
            // output pulses keep the input's real spacing rather than the period estimate's.
            synthMark += (best - analysisEpoch) / (double) std::max (0.25f, ratio);
            analysisEpoch = best;

            // Formant-preserving downshift: grains are spaced more than a period apart, so
            // read a two-period window around the mark (one pulse per grain) and leave the
            // longer synthesis window for coverage.
            if (formant && (double) half > 1.05 * T)
                shortHalf = T;
        }

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
            auto a = w;
            if (shortHalf > 0.0)
                a = std::abs (d) >= shortHalf ? 0.0f : w * (float) (0.5 + 0.5 * std::cos ((double) pi * d / shortHalf));
            olaL[idx] += a * readAbs (delayL, x);
            olaR[idx] += a * readAbs (delayR, x);
            olaW[idx] += w;
        }

        synthMark += T / (double) ratio;
    }

    void PitchShifter::processSample (std::int64_t tOut, std::int64_t newest, float ratio, float period,
                                      bool voiced, bool formant, float& outL, float& outR, bool causal) noexcept
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
        ratio = std::clamp (ratio, 0.25f, 4.0f);
        period = std::clamp (period, 16.0f, (float) maxHalf);

        if (voiced)
        {
            const auto lookahead = (float) (newest - tOut);

            if (! marking)
            {
                marking = true;
                synthMark = (double) tOut + (double) grainHalf (period, ratio, formant, lookahead, maxHalf, causal);
                analysisEpoch = synthMark;
            }

            for (int guard = 0; guard < 32; ++guard)
            {
                if (synthMark - (double) grainHalf (period, ratio, formant, lookahead, maxHalf, causal) > (double) tOut)
                    break;
                addGrain (period, ratio, formant, causal);
            }
        }
        else
        {
            marking = false;
        }

        const auto idx = (size_t) (tOut & olaMask);
        const auto w = olaW[idx];

        if (harmonyVoice)
        {
            // No dry fill: a harmony voice is silent wherever its grains do not reach.
            const auto norm = std::max (1.0f, w);
            outL = olaL[idx] / norm;
            outR = olaR[idx] / norm;
        }
        else if (w >= 1.0f || (marking && w > 1.0e-3f))
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
    void Harmonizer::prepare (double newSampleRate, int maxGrainHalf, const float* left, const float* right, int size)
    {
        sampleRate = newSampleRate;
        for (auto& s : shifters)
        {
            s.prepare (sampleRate, maxGrainHalf);
            s.setHarmonyVoice (true);
            s.setDelayBuffers (left, right, size);
        }

        lineSize = 64;
        while (lineSize < (int) std::ceil (0.032 * sampleRate) + 4)
            lineSize <<= 1;
        for (auto& l : lines)
            l.assign ((size_t) lineSize, 0.0f);
        reset();
    }

    void Harmonizer::reset() noexcept
    {
        for (auto& s : shifters)
            s.reset();
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        ratios.fill (1.0f);
        gains.fill (0.0f);
        panL.fill (std::sqrt (0.5f));
        panR.fill (std::sqrt (0.5f));
        delays.fill (0.0f);
        linePos = 0;
        env = 0.0f;
        voiced = false;
    }

    int Harmonizer::intervalFor (const HarmonyVoice& v, int leadMidi, int mask) noexcept
    {
        if (v.fixed)
            return std::clamp (v.semitones, -24, 24);

        return std::clamp (scaleStepMidi (leadMidi, std::clamp (v.degrees, -14, 14), mask) - leadMidi, -24, 24);
    }

    void Harmonizer::setPitch (float detectedHz, float leadHz, bool isVoiced, const HarmonySettings& h, int mask, float a4,
                               float leadNoteHz) noexcept
    {
        voiced = isVoiced && detectedHz > 20.0f && leadHz > 20.0f;
        if (! voiced)
            return;

        const auto leadMidi = (int) std::lround (PitchCorrector::hzToMidi (leadNoteHz > 20.0f ? leadNoteHz : leadHz, a4));
        const auto leadRatio = leadHz / detectedHz;

        for (size_t v = 0; v < ratios.size(); ++v)
        {
            const auto& voice = h.voices[v];
            const auto semis = (float) intervalFor (voice, leadMidi, mask) + std::clamp (voice.detuneCents, -100.0f, 100.0f) * 0.01f;
            ratios[v] = std::clamp (leadRatio * std::exp2 (semis / 12.0f), 0.25f, 4.0f);
        }
    }

    void Harmonizer::processSample (std::int64_t tOut, std::int64_t newest, float period, bool causal,
                                    const HarmonySettings& h, float& outL, float& outR) noexcept
    {
        const auto smooth = 1.0f - std::exp (-1.0f / (0.010f * (float) sampleRate));
        const auto envCoeff = 1.0f - std::exp (-1.0f / (0.008f * (float) sampleRate));
        env += ((voiced ? 1.0f : 0.0f) - env) * envCoeff;
        const auto bus = h.enabled ? juce::Decibels::decibelsToGain (h.levelDb, -60.0f) : 0.0f;
        linePos = (linePos + 1) & (lineSize - 1);

        for (size_t v = 0; v < shifters.size(); ++v)
        {
            const auto& voice = h.voices[v];
            const auto want = voice.enabled ? bus * juce::Decibels::decibelsToGain (voice.levelDb, -60.0f) : 0.0f;
            gains[v] += (want - gains[v]) * smooth;

            if (gains[v] < 1.0e-5f && want == 0.0f)
            {
                gains[v] = 0.0f;
                lines[v][(size_t) linePos] = 0.0f;
                continue; // idle voice: its shifter restarts its time line when it comes back
            }

            const auto angle = (std::clamp (voice.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * pi;
            panL[v] += (std::cos (angle) - panL[v]) * smooth;
            panR[v] += (std::sin (angle) - panR[v]) * smooth;
            delays[v] += (std::clamp (voice.delayMs, 0.0f, 30.0f) * 0.001f * (float) sampleRate - delays[v]) * smooth;

            float l = 0.0f, r = 0.0f;
            shifters[v].processSample (tOut, newest, ratios[v], period, voiced, h.formant, l, r, causal);

            auto& line = lines[v];
            line[(size_t) linePos] = 0.5f * (l + r);
            const auto d = std::clamp (delays[v], 0.0f, (float) (lineSize - 2));
            const auto di = (int) d;
            const auto frac = d - (float) di;
            const auto a = line[(size_t) ((linePos - di) & (lineSize - 1))];
            const auto b = line[(size_t) ((linePos - di - 1) & (lineSize - 1))];
            const auto mono = (a + frac * (b - a)) * gains[v] * env;
            outL += mono * panL[v];
            outR += mono * panR[v];
        }
    }

    //==============================================================================
    void VibratoTracker::reset() noexcept
    {
        numTurns = midHead = midCount = segHead = segCount = 0;
        tracking = false;
        lastT = lastVoicedT = candT = 0;
        y = lastMidi = cand = lo = hi = 0.0f;
        dir = 0;
    }

    void VibratoTracker::startSegment (std::int64_t t, float midi) noexcept
    {
        segHead = (segHead + 1) % (int) segments.size();
        segCount = std::min (segCount + 1, (int) segments.size());
        segments[(size_t) segHead] = { t, ++segmentId };
        numTurns = 0;
        y = cand = lo = hi = midi;
        candT = t;
        dir = 0;
        tracking = true;
    }

    const VibratoTracker::Mid* VibratoTracker::lastGoodMid() const noexcept
    {
        if (segCount == 0)
            return nullptr;

        const auto id = segments[(size_t) segHead].id;
        for (int k = 0; k < midCount; ++k)
        {
            const auto& m = mids[(size_t) ((midHead - k + ringSize) % ringSize)];
            if (m.segment != id)
                return nullptr;
            if (m.good)
                return &m;
        }
        return nullptr;
    }

    void VibratoTracker::addTurn (std::int64_t t, float v, bool peak) noexcept
    {
        if (numTurns == 2)
        {
            const auto& a = turns[0];
            const auto& b = turns[1];
            if (a.peak != b.peak && b.peak != peak)
            {
                const auto hp1 = (float) ((double) (b.t - a.t) / rate);
                const auto hp2 = (float) ((double) (t - b.t) / rate);
                const auto sw1 = 0.5f * std::abs (b.v - a.v);
                const auto sw2 = 0.5f * std::abs (v - b.v);
                const auto inRange = [] (float x, float lo_, float hi_) { return x >= lo_ && x <= hi_; };
                const auto good = inRange (hp1, 1.0f / 17.0f, 1.0f / 7.0f) && inRange (hp2, 1.0f / 17.0f, 1.0f / 7.0f)
                               && inRange (sw1, 0.06f, 1.6f) && inRange (sw2, 0.06f, 1.6f)
                               && inRange (hp1 / hp2, 0.6f, 1.67f) && inRange (sw1 / sw2, 0.5f, 2.0f);

                midHead = (midHead + 1) % ringSize;
                midCount = std::min (midCount + 1, ringSize);
                mids[(size_t) midHead] = { b.t, 0.25f * (a.v + 2.0f * b.v + v), 0.5f * (sw1 + sw2), 0.5f * (hp1 + hp2),
                                           segments[(size_t) segHead].id, good };
            }
            turns[0] = turns[1];
            turns[1] = { t, v, peak };
        }
        else
        {
            turns[(size_t) numTurns++] = { t, v, peak };
        }
    }

    void VibratoTracker::push (std::int64_t t, float midi, bool voiced) noexcept
    {
        const auto gap = (std::int64_t) (0.040 * rate);

        if (! voiced || midi <= 0.0f)
        {
            if (tracking && t - lastVoicedT > gap)
                tracking = false;
            lastT = t;
            return;
        }

        const auto dt = (double) (t - lastT) / rate;
        lastT = t;

        if (! tracking || t - lastVoicedT > gap || std::abs (midi - lastMidi) > 0.8f)
        {
            lastVoicedT = t;
            lastMidi = midi;
            startSegment (t, midi);
            return;
        }

        lastVoicedT = t;
        lastMidi = midi;

        if (const auto* g = lastGoodMid())
        {
            if (std::abs (midi - g->v) > g->depth + 0.6f)
            {
                startSegment (t, midi);
                return;
            }
        }

        const auto a = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 20.0 * std::max (dt, 1.0e-6)));
        y += (midi - y) * a;
        constexpr float hysteresis = 0.04f;

        if (dir == 0)
        {
            hi = std::max (hi, y);
            lo = std::min (lo, y);
            if (y - lo > hysteresis)       { dir = 1;  cand = y; candT = t; }
            else if (hi - y > hysteresis)  { dir = -1; cand = y; candT = t; }
        }
        else if (dir > 0)
        {
            if (y >= cand) { cand = y; candT = t; }
            else if (cand - y > hysteresis) { addTurn (candT, cand, true); dir = -1; cand = y; candT = t; }
        }
        else
        {
            if (y <= cand) { cand = y; candT = t; }
            else if (y - cand > hysteresis) { addTurn (candT, cand, false); dir = 1; cand = y; candT = t; }
        }
    }

    VibratoTracker::State VibratoTracker::query (std::int64_t t) const noexcept
    {
        State st;

        // The segment t falls in (the newest one that started at or before t).
        int id = -1;
        for (int k = 0; k < segCount; ++k)
        {
            const auto& seg = segments[(size_t) ((segHead - k + (int) segments.size()) % (int) segments.size())];
            if (seg.start <= t) { id = seg.id; break; }
        }
        if (id < 0)
            return st;

        // Good midpoints of that segment, oldest first.
        const Mid* first = nullptr;
        const Mid* last = nullptr;
        const Mid* before = nullptr;
        const Mid* after = nullptr;
        int good = 0;
        for (int k = midCount - 1; k >= 0; --k)
        {
            const auto& m = mids[(size_t) ((midHead - k + ringSize) % ringSize)];
            if (m.segment != id || ! m.good)
                continue;
            ++good;
            if (first == nullptr) first = &m;
            last = &m;
            if (m.t <= t) before = &m;
            if (m.t >= t && after == nullptr) after = &m;
        }
        if (good < 2 || first == nullptr || last == nullptr)
            return st;

        const auto hp = (double) last->halfPeriod * rate;
        if ((double) t < (double) first->t - hp || (double) t > (double) last->t + 3.0 * hp + 0.030 * rate)
            return st;

        const Mid* near = before != nullptr ? before : after;
        if (before != nullptr && after != nullptr && after->t > before->t)
        {
            const auto u = (float) ((double) (t - before->t) / (double) (after->t - before->t));
            st.centreMidi = before->v + u * (after->v - before->v);
        }
        else
        {
            st.centreMidi = near->v;
        }
        st.depthSemis = near->depth;
        st.rateHz = near->halfPeriod > 0.0f ? 0.5f / near->halfPeriod : 0.0f;
        st.active = true;
        return st;
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
        vibrato.prepare (sampleRate);
        shifter.prepare (sampleRate, hq + 64);
        shifter.setDelayBuffers (delayL.data(), delayR.data(), delaySize);
        harmonizer.prepare (sampleRate, hq + 64, delayL.data(), delayR.data(), delaySize);
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
        vibrato.reset();
        vibState = {};
        vibWeight = vibCentre = vibDepth = lastCentreHz = 0.0f;
        vibratoUsed = false;
        shifter.reset();
        harmonizer.reset();
        mode = ProcessMode::mix;
        harmonyTail = 0;
        lofiState = {};
    }

    int PitchEngine::latencySamples (const PitchSettings& s, bool nonRealtime) const noexcept
    {
        return latencyForMode (processModeFor (s, nonRealtime), sampleRate);
    }

    float PitchEngine::readDelay (const std::vector<float>& buf, int delay) const noexcept
    {
        delay = std::clamp (delay, 0, delaySize - 1);
        return buf[(size_t) wrap (writePos - 1 - delay, delaySize)];
    }

    void PitchEngine::trackVibrato (const PitchSettings& s, int window, std::int64_t estimateTime, float threshold,
                                    float minHz, float maxHz, float a4, float& correctorInputHz, float& vibratoSemis,
                                    float respect) noexcept
    {
        juce::ignoreUnused (s);
        vibratoUsed = true;
        const auto hq = mode == ProcessMode::hq;
        const auto toMidi = [a4] (const PitchEstimate& e) { return e.voiced && e.hz > 20.0f ? PitchCorrector::hzToMidi (e.hz, a4) : 0.0f; };

        if (mode == ProcessMode::tracking)
        {
            // Causal: the tracker only knows the past, so the centre is the latest midpoint.
            vibrato.push (estimateTime, toMidi (lastEstimate), lastEstimate.voiced);
        }
        else
        {
            // Look-ahead: a second YIN on the newest input (the future of the playback point), so
            // the centre at the playback point is interpolated between midpoints on both sides.
            const auto w = std::min (window, delaySize - 4);
            for (int i = 0; i < w; ++i)
            {
                const auto d = w - 1 - i;
                monoScratch[(size_t) i] = 0.5f * (readDelay (delayL, d) + readDelay (delayR, d));
            }
            const auto future = detector.analyse (monoScratch.data(), w, minHz, maxHz, threshold, hq);
            vibrato.push (inputCount - 1 - (w - 1 - w / 2), toMidi (future), future.voiced);
        }

        vibState = vibrato.query (estimateTime);
        const auto m = toMidi (lastEstimate);
        const auto voiced = lastEstimate.voiced && m > 0.0f;

        if (vibState.active)
        {
            vibCentre = vibState.centreMidi;
            vibDepth = vibState.depthSemis;
        }

        // A reading far outside the vibrato's swing is a new note (or a scoop): drop the vibrato
        // at once so it snaps like it always has.
        const auto outside = vibWeight > 0.0f && std::abs (m - vibCentre) > vibDepth * 1.3f + 0.5f;

        if (! voiced || outside || (vibState.active && std::abs (m - vibState.centreMidi) > vibState.depthSemis * 1.3f + 0.5f))
        {
            vibWeight = 0.0f;
        }
        else
        {
            const auto coeff = 1.0f - std::exp (-(float) hop / (0.040f * (float) sampleRate));
            vibWeight += ((vibState.active ? 1.0f : 0.0f) - vibWeight) * coeff;
            if (vibWeight < 1.0e-3f)
                vibWeight = 0.0f;
        }

        if (vibWeight <= 0.0f)
        {
            lastCentreHz = 0.0f;
            return; // exactly the plain path: the corrector sees the detected pitch
        }

        const auto swing = m - vibCentre;
        const auto centreMidi = m - vibWeight * swing;
        correctorInputHz = PitchCorrector::midiToHz (centreMidi, a4);
        vibratoSemis = respect * vibWeight * swing;
        lastCentreHz = PitchCorrector::midiToHz (vibCentre, a4);
    }

    void PitchEngine::analyseIfDue (const PitchSettings& s, const HarmonySettings& h) noexcept
    {
        const auto hq = mode == ProcessMode::hq;
        const auto range = detectorRangeFor (s);
        const auto minHz = range.minHz;
        const auto maxHz = range.maxHz;
        int window = 0;
        std::int64_t estimateTime = 0; // input sample number at the centre of the analysed window
        const auto newest = inputCount - 1;

        if (mode == ProcessMode::tracking)
        {
            // Causal: the newest window of input (there is no look-ahead to centre it in).
            window = std::min (detector.recommendedWindow (minHz), delaySize - 4);
            for (int i = 0; i < window; ++i)
            {
                const auto d = window - 1 - i;
                monoScratch[(size_t) i] = 0.5f * (readDelay (delayL, d) + readDelay (delayR, d));
            }
            estimateTime = newest - (window - 1 - window / 2);
        }
        else
        {
            window = std::min (detector.recommendedWindow (minHz) * (hq ? 2 : 1), delaySize - latency - 4);
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
            estimateTime = newest - std::max (0, latency - ahead - 1);
        }

        const auto threshold = hq ? 0.10f : 0.15f;
        lastEstimate = detector.analyse (monoScratch.data(), window, minHz, maxHz, threshold, hq);

        // Hop-scaled corrector: treat one hop as the time step.
        // Temporarily scale by calling process once per hop (the 64x factor inside matches live hop).
        const auto a4 = std::clamp (s.a4Hz, 415.0f, 466.0f);
        const auto respect = std::clamp (s.vibratoRespect, 0.0f, 1.0f);
        auto correctorInputHz = lastEstimate.hz;
        auto vibratoSemis = 0.0f;

        if (respect > 0.0f)
            trackVibrato (s, window, estimateTime, threshold, minHz, maxHz, a4, correctorInputHz, vibratoSemis, respect);
        else if (vibratoUsed)
        {
            vibrato.reset();
            vibState = {};
            vibWeight = vibCentre = vibDepth = lastCentreHz = 0.0f;
            vibratoUsed = false;
        }

        lastCorrectedHz = corrector.process (correctorInputHz, lastEstimate.voiced, s, hop);
        const auto correctedCentreHz = lastCorrectedHz;
        if (! juce::exactlyEqual (vibratoSemis, 0.0f) && lastCorrectedHz > 20.0f)
            lastCorrectedHz *= std::exp2 (vibratoSemis / 12.0f);
        lastVoiced = lastEstimate.voiced && lastCorrectedHz > 20.0f && lastEstimate.hz > 20.0f;

        if (lastVoiced)
            lastRatio = std::clamp (lastCorrectedHz / lastEstimate.hz, 0.5f, 2.0f);
        else
            lastRatio = 1.0f;

        // Harmonies follow what the lead plays: the corrected note when correction is on.
        // With Vibrato Respect the lead note (for Scale intervals) is the vibrato's centre; leadHz
        // still carries the vibrato, so every voice keeps it.
        const auto leadHz = s.enabled ? lastCorrectedHz : lastEstimate.hz;
        const auto leadNoteHz = s.enabled ? correctedCentreHz : correctorInputHz;
        harmonizer.setPitch (lastEstimate.hz, leadHz, lastVoiced, h, harmonyMask (s), a4, leadNoteHz);
    }

    void PitchEngine::process (juce::AudioBuffer<float>& buffer, int numSamples, const PitchSettings& s,
                               const HarmonySettings& harmony, bool nonRealtime, PitchTap* tap) noexcept
    {
        const auto n = std::min (numSamples, maxBlock);
        const auto channels = std::min (2, buffer.getNumChannels());
        if (n <= 0 || channels <= 0)
            return;

        auto* left = buffer.getWritePointer (0);
        auto* right = channels > 1 ? buffer.getWritePointer (1) : left;

        mode = processModeFor (s, nonRealtime);
        latency = latencyForMode (mode, sampleRate);
        hop = mode == ProcessMode::hq ? 32 : 64;
        const auto causal = mode == ProcessMode::tracking;

        const auto mixTarget = s.enabled ? std::clamp (s.amount, 0.0f, 1.0f) : 0.0f;
        const auto mixCoeff = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        const auto voiceCoeff = 1.0f - std::exp (-1.0f / (0.008f * (float) sampleRate));

        const auto period = [this] { return lastEstimate.hz > 20.0f ? (float) sampleRate / lastEstimate.hz
                                                                    : (float) sampleRate / 200.0f; };

        // Harmony Lo-Fi, per block. The voices' rate drops from 32 kHz (amount just above 0) to
        // 16 kHz (amount 1), band-limited to ~0.42 of that rate by two one-poles, then held
        // (some aliasing is the point) and quantised from 14 down to 10 bits.
        const auto lofiAmount = std::clamp (harmony.lofi, 0.0f, 1.0f);
        const auto lofiOn = lofiAmount > 0.0f;
        float lofiStep = 1.0f, lofiCoeff = 1.0f, lofiQ = 1.0f;
        if (lofiOn)
        {
            const auto targetRate = std::min ((float) sampleRate, 32000.0f - 16000.0f * lofiAmount);
            lofiStep = targetRate / (float) sampleRate;
            const auto cutoff = std::min (0.42f * targetRate, 0.45f * (float) sampleRate);
            lofiCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sampleRate);
            lofiQ = std::exp2 (14.0f - 4.0f * lofiAmount - 1.0f);
        }

        for (int i = 0; i < n; ++i)
        {
            delayL[(size_t) writePos] = left[i];
            delayR[(size_t) writePos] = right[i];
            writePos = wrap (writePos + 1, delaySize);
            ++inputCount;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyseIfDue (s, harmony);

                if (tap != nullptr)
                    tap->push ({ lastEstimate.hz, lastVoiced ? lastCorrectedHz : 0.0f, lastEstimate.confidence, lastVoiced,
                                 0.0f, lastVoiced ? lastCentreHz : 0.0f, vibWeight, vibState.rateHz, vibDepth * 100.0f });
                if (learnTap != nullptr && s.learnRange)
                    learnTap->push ({ lastEstimate.hz, 0.0f, lastEstimate.confidence, lastEstimate.voiced, (float) ((double) hop / sampleRate) });
            }

            mixSmoothed += (mixTarget - mixSmoothed) * mixCoeff;
            const auto voicedNow = lastVoiced && s.enabled;

            // Output time: the input sample written latency samples ago.
            const auto newest = inputCount - 1;
            float wetL = 0.0f, wetR = 0.0f;
            const auto tOut = newest - latency;
            shifter.processSample (tOut, newest, lastRatio, period(), voicedNow, s.formant, wetL, wetR, causal);

            const auto dryL = readDelay (delayL, latency);
            const auto dryR = readDelay (delayR, latency);
            const auto want = (voicedNow && mixSmoothed > 1.0e-4f) ? 1.0f : 0.0f;
            voiceEnv += (want - voiceEnv) * voiceCoeff;
            const auto wetAmt = voiceEnv * mixSmoothed;
            auto outL = dryL + wetAmt * (wetL - dryL);
            auto outR = dryR + wetAmt * (wetR - dryR);

            // Harmonies: same tOut as the lead, so they share its latency exactly. After the
            // switch goes off the voices run until their gains have faded (no click).
            if (harmony.enabled || harmonyTail > 0)
            {
                harmonyTail = harmony.enabled ? (int) (0.1 * sampleRate) : harmonyTail - 1;
                float hL = 0.0f, hR = 0.0f;
                harmonizer.processSample (tOut, newest, period(), causal, harmony, hL, hR);
                if (lofiOn)
                {
                    auto& z = lofiState;
                    z.lp1L += lofiCoeff * (hL - z.lp1L);  z.lp2L += lofiCoeff * (z.lp1L - z.lp2L);
                    z.lp1R += lofiCoeff * (hR - z.lp1R);  z.lp2R += lofiCoeff * (z.lp1R - z.lp2R);
                    z.phase += lofiStep;
                    if (z.phase >= 1.0f)
                    {
                        z.phase -= 1.0f;
                        z.holdL = std::round (z.lp2L * lofiQ) / lofiQ;
                        z.holdR = std::round (z.lp2R * lofiQ) / lofiQ;
                    }
                    hL = z.holdL;
                    hR = z.holdR;
                }
                if (channels > 1)
                {
                    outL += hL;
                    outR += hR;
                }
                else
                    outL += 0.5f * (hL + hR);
            }

            left[i] = outL;
            if (channels > 1)
                right[i] = outR;
        }
    }
}
