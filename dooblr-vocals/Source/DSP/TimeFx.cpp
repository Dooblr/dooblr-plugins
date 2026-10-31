#include "TimeFx.h"

#include <algorithm>
#include <cmath>

namespace dooblrvocals::dsp
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;

        int nextPowerOfTwo (int n) noexcept
        {
            int p = 1;
            while (p < n)
                p <<= 1;
            return p;
        }

        /** Linear-interpolated read `delay` samples behind writePos (line size is a power of two). */
        inline float readLine (const std::vector<float>& line, int writePos, float delay) noexcept
        {
            const auto mask = (int) line.size() - 1;
            const auto whole = (int) delay;
            const auto frac = delay - (float) whole;
            const auto i0 = (writePos - whole) & mask;
            const auto i1 = (i0 - 1) & mask;
            return line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
        }

        struct NoteValue { const char* name; double beats; };

        constexpr NoteValue noteValues[numNoteValues] {
            { "1/64", 0.0625 },        { "1/32T", 0.125 * 2.0 / 3.0 }, { "1/32", 0.125 },
            { "1/16T", 0.25 * 2.0 / 3.0 }, { "1/16", 0.25 },           { "1/16D", 0.375 },
            { "1/8T", 0.5 * 2.0 / 3.0 },  { "1/8", 0.5 },              { "1/8D", 0.75 },
            { "1/4T", 2.0 / 3.0 },        { "1/4", 1.0 },              { "1/4D", 1.5 },
            { "1/2T", 4.0 / 3.0 },        { "1/2", 2.0 },              { "1/2D", 3.0 },
            { "1/1", 4.0 }
        };

        constexpr NoteValue preDelaySyncs[numPreDelaySyncs] {
            { "Off", 0.0 }, { "1/64", 0.0625 }, { "1/32T", 0.125 * 2.0 / 3.0 }, { "1/32", 0.125 },
            { "1/16T", 0.25 * 2.0 / 3.0 }, { "1/16", 0.25 }, { "1/8", 0.5 }
        };

        double safeBpm (double bpm) noexcept { return bpm > 1.0 && std::isfinite (bpm) ? std::clamp (bpm, 20.0, 400.0) : 120.0; }
    }

    const char* noteValueName (int index) noexcept { return noteValues[std::clamp (index, 0, numNoteValues - 1)].name; }
    double noteValueBeats (int index) noexcept { return noteValues[std::clamp (index, 0, numNoteValues - 1)].beats; }

    double noteValueSamples (int index, double bpm, double sampleRate) noexcept
    {
        return noteValueBeats (index) * 60.0 / safeBpm (bpm) * sampleRate;
    }

    const char* preDelaySyncName (int index) noexcept { return preDelaySyncs[std::clamp (index, 0, numPreDelaySyncs - 1)].name; }
    double preDelaySyncBeats (int index) noexcept { return preDelaySyncs[std::clamp (index, 0, numPreDelaySyncs - 1)].beats; }

    //==============================================================================
    void SimpleFilter::setLowPass (double hz, double sampleRate) noexcept
    {
        const auto w = std::tan (juce::MathConstants<double>::pi * std::clamp (hz, 10.0, 0.45 * sampleRate) / sampleRate);
        const auto q = 1.0 / std::sqrt (2.0);
        const auto n = 1.0 / (1.0 + w / q + w * w);
        b0 = (float) (w * w * n);
        b1 = 2.0f * b0;
        b2 = b0;
        a1 = (float) (2.0 * (w * w - 1.0) * n);
        a2 = (float) ((1.0 - w / q + w * w) * n);
    }

    void SimpleFilter::setHighPass (double hz, double sampleRate) noexcept
    {
        const auto w = std::tan (juce::MathConstants<double>::pi * std::clamp (hz, 5.0, 0.45 * sampleRate) / sampleRate);
        const auto q = 1.0 / std::sqrt (2.0);
        const auto n = 1.0 / (1.0 + w / q + w * w);
        b0 = (float) n;
        b1 = -2.0f * b0;
        b2 = b0;
        a1 = (float) (2.0 * (w * w - 1.0) * n);
        a2 = (float) ((1.0 - w / q + w * w) * n);
    }

    //==============================================================================
    void Ducker::prepare (double rate) noexcept
    {
        sampleRate = rate;
        attackCoeff = (float) std::exp (-1.0 / (0.002 * sampleRate));
        lastReleaseMs = -1.0f;
        reset();
    }

    float Ducker::process (float keyAbs, float amountDb, float releaseMs) noexcept
    {
        if (! juce::exactlyEqual (releaseMs, lastReleaseMs))
        {
            lastReleaseMs = releaseMs;
            releaseCoeff = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sampleRate));
        }

        const auto c = keyAbs > envelope ? attackCoeff : releaseCoeff;
        envelope = keyAbs + c * (envelope - keyAbs);

        if (amountDb <= 0.0f)
        {
            gain = 1.0f;
            return gain;
        }

        const auto envDb = envelope > 1.0e-6f ? 20.0f * std::log10 (envelope) : -120.0f;
        const auto depth = std::clamp ((envDb - thresholdDb) / kneeDb, 0.0f, 1.0f);
        gain = std::pow (10.0f, -0.05f * amountDb * depth);
        return gain;
    }

    //==============================================================================
    namespace
    {
        constexpr std::array<double, VocalReverb::numLines> lineMs { 31.3, 37.9, 41.7, 47.3, 53.9, 61.1, 67.7, 73.1 };
        constexpr std::array<double, VocalReverb::numEarly> earlyMs { 4.3, 7.9, 11.2, 14.9, 19.7, 23.1, 28.3, 33.7, 39.1, 45.7, 52.9, 61.3 };
        constexpr std::array<double, VocalReverb::numDiffusers> diffuserMs { 4.77, 3.59, 2.73, 1.79 };
        constexpr std::array<float, VocalReverb::numLines> signL { 1, -1, 1, -1, 1, -1, 1, -1 };
        constexpr std::array<float, VocalReverb::numLines> signR { 1, 1, -1, -1, 1, 1, -1, -1 };
    }

    double VocalReverb::earlyTapMs (int t, float size) noexcept
    {
        return earlyMs[(size_t) std::clamp (t, 0, numEarly - 1)] * (0.5 + (double) std::clamp (size, 0.0f, 1.0f));
    }

    float VocalReverb::earlyTapGain (int t) noexcept
    {
        return (float) std::pow (0.86, (double) std::clamp (t, 0, numEarly - 1));
    }

    namespace
    {
        constexpr std::array<float, VocalReverb::numLines> signIn { 1, -1, -1, 1, 1, 1, -1, -1 };
        constexpr std::array<double, VocalReverb::numLines> lfoHz { 0.41, 0.0, 0.67, 0.0, 0.0, 0.83, 0.0, 1.07 };

        double sizeScale (float size) noexcept { return 0.4 + 1.2 * (double) std::clamp (size, 0.0f, 1.0f); }
        constexpr double maxModMs = 0.6;
        constexpr double calibrationHz = 1000.0; // Decay is the RT60 here
        constexpr double maxLineGain = 0.99995;  // keeps every loop strictly lossy at DC
    }

    void VocalReverb::prepare (double rate, int)
    {
        sampleRate = rate;
        preDelay.assign ((size_t) nextPowerOfTwo ((int) std::ceil (maxPreDelayMs * 0.001 * rate) + 8), 0.0f);
        earlyLine.assign ((size_t) nextPowerOfTwo ((int) std::ceil (earlyMs.back() * 1.6 * 0.001 * rate) + 8), 0.0f);

        for (size_t d = 0; d < diffusers.size(); ++d)
        {
            diffuserLength[d] = std::max (1, (int) std::lround (diffuserMs[d] * 0.001 * rate));
            diffusers[d].assign ((size_t) diffuserLength[d], 0.0f);
        }

        for (size_t i = 0; i < lines.size(); ++i)
            lines[i].assign ((size_t) nextPowerOfTwo ((int) std::ceil ((lineMs[i] * sizeScale (1.0f) + maxModMs) * 0.001 * rate) + 8), 0.0f);

        ducker.prepare (rate);
        reset();
    }

    void VocalReverb::reset() noexcept
    {
        std::fill (preDelay.begin(), preDelay.end(), 0.0f);
        std::fill (earlyLine.begin(), earlyLine.end(), 0.0f);
        for (auto& d : diffusers) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        preWrite = earlyWrite = 0;
        diffuserPos.fill (0);
        lineWrite.fill (0);
        dampState.fill (0.0f);
        for (size_t i = 0; i < lfoPhase.size(); ++i)
            lfoPhase[i] = 0.25 * (double) i;
        lowCut.reset();
        highCut.reset();
        ducker.reset();
        haveCoefficients = false;
        preNow = -1.0f;
    }

    float VocalReverb::readFrac (const std::vector<float>& line, int writePos, float delay) noexcept
    {
        return readLine (line, writePos, delay);
    }

    void VocalReverb::updateCoefficients (const ReverbSettings& s) noexcept
    {
        const auto changed = ! haveCoefficients || ! juce::exactlyEqual (s.decaySeconds, last.decaySeconds) || ! juce::exactlyEqual (s.size, last.size)
                          || ! juce::exactlyEqual (s.damping, last.damping) || ! juce::exactlyEqual (s.lowCutHz, last.lowCutHz) || ! juce::exactlyEqual (s.highCutHz, last.highCutHz)
                          || ! juce::exactlyEqual (s.earlyLate, last.earlyLate) || ! juce::exactlyEqual (s.diffusion, last.diffusion) || ! juce::exactlyEqual (s.modulation, last.modulation);
        if (! changed)
            return;

        const auto scale = sizeScale (s.size);
        const auto rt60 = std::clamp ((double) s.decaySeconds, 0.1, 30.0);
        const auto hfRatio = 1.0 - 0.85 * (double) std::clamp (s.damping, 0.0f, 1.0f); // HF RT60 / RT60

        modDepth = (float) (std::clamp (s.modulation, 0.0f, 1.0f) * maxModMs * 0.001 * sampleRate);

        // Decay calibration: Decay is the RT60 at mid frequencies (referenced to 1 kHz), so each
        // line's loss is set from what one trip round its loop really does there.
        const auto w = twoPi * calibrationHz / sampleRate;
        // Mean magnitude of the linear-interpolated read over a sweeping fraction (modulated lines).
        const auto interpLoss = std::sqrt (1.0 - (1.0 - std::cos (w)) / 3.0);

        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto modulated = lfoHz[i] > 0.0 && modDepth > 0.0f;
            auto d = lineMs[i] * scale * 0.001 * sampleRate;
            if (! modulated)
                d = std::round (d); // a whole-sample read: no interpolation loss
            lineDelay[i] = (float) d;

            // -60 dB after rt60 seconds: g = 10^(-3 d / (rt60 fs)).
            const auto g = std::pow (10.0, -3.0 * d / (rt60 * sampleRate));
            // One-pole absorber with DC gain 1 and Nyquist gain g^(1/hfRatio - 1), so the highs
            // fall at the rate of an RT60 of rt60 * hfRatio.
            const auto k = std::pow (g, 1.0 / hfRatio - 1.0);
            const auto a = (1.0 - k) / (1.0 + k);
            dampCoeff[i] = (float) a;

            // The absorber's gain and phase delay at the reference, plus the interpolation loss.
            const auto re = 1.0 - a * std::cos (w), im = a * std::sin (w);
            const auto absorberGain = (1.0 - a) / std::sqrt (re * re + im * im);
            const auto absorberDelay = std::atan2 (im, re) / w;
            const auto loopGain = std::pow (10.0, -3.0 * (d + absorberDelay) / (rt60 * sampleRate));
            lineGain[i] = (float) std::min (maxLineGain, loopGain / (absorberGain * (modulated ? interpLoss : 1.0)));
        }

        const auto earlyScale = 0.5 + (double) std::clamp (s.size, 0.0f, 1.0f);
        auto earlyEnergy = 0.0;
        for (size_t t = 0; t < earlyDelay.size(); ++t)
        {
            earlyDelay[t] = (float) (earlyMs[t] * earlyScale * 0.001 * sampleRate);
            earlyGain[t] = (float) std::pow (0.86, (double) t);
            earlyEnergy += (double) earlyGain[t] * earlyGain[t];
        }
        for (auto& g : earlyGain)
            g = (float) ((double) g / std::sqrt (earlyEnergy * 0.5));

        const auto b = (double) std::clamp (s.earlyLate, 0.0f, 1.0f) * juce::MathConstants<double>::halfPi;
        earlyLevel = (float) std::min (1.0, std::cos (b) * std::sqrt (2.0));
        lateLevel = (float) std::min (1.0, std::sin (b) * std::sqrt (2.0));

        // Keep the tail's loudness roughly independent of the decay and size.
        lateNorm = (float) (0.9 / std::sqrt ((double) numLines) / std::sqrt (std::max (0.25, rt60 / scale)));
        diffuseGain = 0.72f * std::clamp (s.diffusion, 0.0f, 1.0f);
        lowCut.setHighPass (s.lowCutHz, sampleRate);
        highCut.setLowPass (s.highCutHz, sampleRate);

        last = s;
        haveCoefficients = true;
    }

    double VocalReverb::preDelayMsFor (const ReverbSettings& s, double bpm) noexcept
    {
        const auto ms = s.preDelaySync > 0 ? preDelaySyncBeats (s.preDelaySync) * 60000.0 / safeBpm (bpm) : (double) s.preDelayMs;
        return std::clamp (ms, 0.0, maxPreDelayMs);
    }

    void VocalReverb::renderWet (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                                 const float* keyL, const float* keyR, const ReverbSettings& s, double bpm) noexcept
    {
        updateCoefficients (s);

        const auto preMs = preDelayMsFor (s, bpm);
        const auto preTarget = (float) std::clamp (preMs * 0.001 * sampleRate, 0.0, maxPreDelayMs * 0.001 * sampleRate);
        if (preNow < 0.0f)
            preNow = preTarget;
        const auto preGlide = 1.0f - (float) std::exp (-1.0 / (0.03 * sampleRate));
        const auto preMask = (int) preDelay.size() - 1;
        const auto earlyMask = (int) earlyLine.size() - 1;
        const auto width = std::clamp (s.width, 0.0f, 1.5f);

        for (int i = 0; i < n; ++i)
        {
            // Pre-delay (mono).
            preNow += preGlide * (preTarget - preNow);
            if (std::abs (preTarget - preNow) < 1.0e-3f)
                preNow = preTarget;
            const auto preSamples = preNow;
            preDelay[(size_t) preWrite] = 0.5f * (inL[i] + inR[i]);
            const auto x = preSamples < 0.5f ? preDelay[(size_t) preWrite] : readLine (preDelay, preWrite, preSamples);
            preWrite = (preWrite + 1) & preMask;

            // Early reflections.
            earlyLine[(size_t) earlyWrite] = x;
            auto eL = 0.0f, eR = 0.0f;
            for (size_t t = 0; t < earlyDelay.size(); ++t)
            {
                const auto v = earlyGain[t] * readLine (earlyLine, earlyWrite, earlyDelay[t]);
                if ((t & 1) == 0) eL += v; else eR += v;
            }
            earlyWrite = (earlyWrite + 1) & earlyMask;

            // Input diffusion: Schroeder allpasses in series.
            auto d = x;
            for (size_t k = 0; k < diffusers.size(); ++k)
            {
                auto& buf = diffusers[k];
                auto& pos = diffuserPos[k];
                const auto delayed = buf[(size_t) pos];
                const auto y = delayed - diffuseGain * d;
                buf[(size_t) pos] = d + diffuseGain * y;
                pos = pos + 1 < diffuserLength[k] ? pos + 1 : 0;
                d = y;
            }

            // FDN: read, absorb, Householder mix, write.
            std::array<float, numLines> out {}, filtered {};
            auto sum = 0.0f;
            for (size_t l = 0; l < lines.size(); ++l)
            {
                auto delay = lineDelay[l];
                if (lfoHz[l] > 0.0 && modDepth > 0.0f)
                {
                    delay += modDepth * (float) std::sin (twoPi * lfoPhase[l]);
                    lfoPhase[l] += lfoHz[l] / sampleRate;
                    if (lfoPhase[l] >= 1.0) lfoPhase[l] -= 1.0;
                }
                out[l] = readLine (lines[l], lineWrite[l], std::max (1.0f, delay));
                dampState[l] = (1.0f - dampCoeff[l]) * out[l] + dampCoeff[l] * dampState[l];
                filtered[l] = lineGain[l] * dampState[l];
                sum += filtered[l];
            }

            const auto householder = sum * (2.0f / (float) numLines);
            auto lL = 0.0f, lR = 0.0f;
            for (size_t l = 0; l < lines.size(); ++l)
            {
                auto& line = lines[l];
                line[(size_t) lineWrite[l]] = filtered[l] - householder + signIn[l] * 0.5f * d;
                lineWrite[l] = (lineWrite[l] + 1) & ((int) line.size() - 1);
                lL += signL[l] * out[l];
                lR += signR[l] * out[l];
            }

            auto yL = earlyLevel * eL + lateLevel * lateNorm * lL;
            auto yR = earlyLevel * eR + lateLevel * lateNorm * lR;

            yL = highCut.process (0, lowCut.process (0, yL));
            yR = highCut.process (1, lowCut.process (1, yR));

            const auto mid = 0.5f * (yL + yR), side = 0.5f * (yL - yR) * width;
            const auto duck = ducker.process (std::max (std::abs (keyL[i]), std::abs (keyR[i])), s.duckDb, s.duckReleaseMs);
            wetL[i] = (mid + side) * duck;
            wetR[i] = (mid - side) * duck;
        }

        // Flush denormals / guard against any non-finite input poisoning the tank.
        if (! std::isfinite (wetL[n - 1]) || ! std::isfinite (wetR[n - 1]))
        {
            reset();
            std::fill (wetL, wetL + n, 0.0f);
            std::fill (wetR, wetR + n, 0.0f);
        }
    }

    //==============================================================================
    void VocalDelay::prepare (double rate, int)
    {
        sampleRate = rate;
        const auto size = nextPowerOfTwo ((int) std::ceil (maxDelaySeconds * rate) + 256);
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        ducker.prepare (rate);
        reset();
    }

    void VocalDelay::reset() noexcept
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        currentDelay = { -1.0f, -1.0f };
        lowCut.reset();
        highCut.reset();
        lastLowCut = lastHighCut = -1.0f;
        ducker.reset();
        lfoPhase = 0.0;
    }

    double VocalDelay::delaySamplesFor (const DelaySettings& s, int channel, double bpm, double rate) noexcept
    {
        const auto right = channel == 1 && s.mode == DelaySettings::dual;
        const auto samples = s.sync ? noteValueSamples (right ? s.noteR : s.noteL, bpm, rate)
                                    : (double) (right ? s.timeRMs : s.timeLMs) * 0.001 * rate;
        return std::clamp (samples, 1.0, maxDelaySeconds * rate);
    }

    void VocalDelay::renderWet (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                                const float* keyL, const float* keyR, const DelaySettings& s, double bpm) noexcept
    {
        if (! juce::exactlyEqual (s.lowCutHz, lastLowCut))  { lastLowCut = s.lowCutHz;  lowCut.setHighPass (s.lowCutHz, sampleRate); }
        if (! juce::exactlyEqual (s.highCutHz, lastHighCut)) { lastHighCut = s.highCutHz; highCut.setLowPass (s.highCutHz, sampleRate); }

        const std::array<float, 2> target { (float) delaySamplesFor (s, 0, bpm, sampleRate),
                                            (float) delaySamplesFor (s, 1, bpm, sampleRate) };
        for (size_t c = 0; c < 2; ++c)
            if (currentDelay[c] < 0.0f)
                currentDelay[c] = target[c];

        const auto glide = 1.0f - (float) std::exp (-1.0 / (0.05 * sampleRate)); // 50 ms tape-style glide
        const auto feedback = std::clamp (s.feedback, 0.0f, maxFeedback);
        const auto depth = (float) (std::clamp (s.modulation, 0.0f, 1.0f) * 0.002 * sampleRate);
        const auto drive = std::clamp (s.drive, 0.0f, 1.0f);
        const auto k = 1.0f + 4.0f * drive;
        const auto mask = (int) lines[0].size() - 1;
        const auto width = std::clamp (s.width, 0.0f, 1.5f);

        const auto shape = [&] (int ch, float x) noexcept
        {
            auto y = highCut.process (ch, lowCut.process (ch, x));
            if (drive > 0.001f)
                y = std::tanh (k * y) / k;
            return y;
        };

        for (int i = 0; i < n; ++i)
        {
            std::array<float, 2> tap {};
            for (size_t c = 0; c < 2; ++c)
            {
                currentDelay[c] += glide * (target[c] - currentDelay[c]);
                if (std::abs (target[c] - currentDelay[c]) < 1.0e-3f)
                    currentDelay[c] = target[c];

                auto d = currentDelay[c];
                if (depth > 0.0f)
                    d += depth * (1.0f + (float) std::sin (twoPi * (lfoPhase + 0.25 * (double) c))) * 0.5f;
                tap[c] = shape ((int) c, readLine (lines[c], writePos, std::max (1.0f, d)));
            }

            if (depth > 0.0f)
            {
                lfoPhase += 0.6 / sampleRate;
                if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            }

            if (s.mode == DelaySettings::pingPong)
            {
                lines[0][(size_t) writePos] = 0.5f * (inL[i] + inR[i]) + feedback * tap[1];
                lines[1][(size_t) writePos] = feedback * tap[0];
            }
            else
            {
                lines[0][(size_t) writePos] = inL[i] + feedback * tap[0];
                lines[1][(size_t) writePos] = inR[i] + feedback * tap[1];
            }
            writePos = (writePos + 1) & mask;

            const auto mid = 0.5f * (tap[0] + tap[1]), side = 0.5f * (tap[0] - tap[1]) * width;
            const auto duck = ducker.process (std::max (std::abs (keyL[i]), std::abs (keyR[i])), s.duckDb, s.duckReleaseMs);
            wetL[i] = (mid + side) * duck;
            wetR[i] = (mid - side) * duck;
        }

        if (! std::isfinite (wetL[n - 1]) || ! std::isfinite (wetR[n - 1]))
        {
            reset();
            std::fill (wetL, wetL + n, 0.0f);
            std::fill (wetR, wetR + n, 0.0f);
        }
    }

    //==============================================================================
    void FxBlock::prepare (double rate, int block)
    {
        sampleRate = rate;
        maxBlock = std::max (1, block);
        rampSamples = std::max (1, (int) std::lround (0.02 * rate));
        reverb.prepare (rate, maxBlock);
        delay.prepare (rate, maxBlock);
        keyBuffer.setSize (2, maxBlock);
        stageBuffer.setSize (2, maxBlock);
        wetBuffer.setSize (2, maxBlock);
        sumBuffer.setSize (2, maxBlock);
        reset();
    }

    void FxBlock::reset() noexcept
    {
        reverb.reset();
        delay.reset();
        reverbDry = reverbWet = delayDry = delayWet = Ramp {};
        reverbDry.value = reverbDry.target = delayDry.value = delayDry.target = 1.0f;
        reverbActive = delayActive = false;
        reverbDuckDb = delayDuckDb = 0.0f;
    }

    void FxBlock::runStage (bool isReverb, const float* inL, const float* inR, float* outL, float* outR, int n,
                            const float* keyL, const float* keyR, const ReverbSettings& rs, const DelaySettings& ds,
                            double bpm, Ramp& dry, Ramp& wet, bool addToOutput) noexcept
    {
        auto* wL = wetBuffer.getWritePointer (0);
        auto* wR = wetBuffer.getWritePointer (1);

        if (probe != nullptr)
        {
            auto* dest = isReverb ? probe->reverbIn : probe->delayIn;
            if (dest != nullptr)
                for (int i = 0; i < n && probe->written + i < probe->capacity; ++i)
                    dest[probe->written + i] = inL[i];
        }

        if (isReverb)
            reverb.renderWet (inL, inR, wL, wR, n, keyL, keyR, rs, bpm);
        else
            delay.renderWet (inL, inR, wL, wR, n, keyL, keyR, ds, bpm);

        for (int i = 0; i < n; ++i)
        {
            const auto g = wet.next();
            const auto d = dry.next();
            if (addToOutput)
            {
                outL[i] += g * wL[i];
                outR[i] += g * wR[i];
            }
            else
            {
                outL[i] = d * inL[i] + g * wL[i];
                outR[i] = d * inR[i] + g * wR[i];
            }
        }
    }

    void FxBlock::process (juce::AudioBuffer<float>& buffer, int numSamples, const ReverbSettings& rs,
                           const DelaySettings& ds, FxRouting routing, double bpm) noexcept
    {
        for (int offset = 0; offset < numSamples; offset += maxBlock)
        {
            const auto n = std::min (maxBlock, numSamples - offset);
            auto* L = buffer.getWritePointer (0, offset);
            auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, offset) : L;

            reverbDry.set (rs.enabled ? dryGainFor (rs.mix) : 1.0f, rampSamples);
            reverbWet.set (rs.enabled ? wetGainFor (rs.mix) : 0.0f, rampSamples);
            delayDry.set (ds.enabled ? dryGainFor (ds.mix) : 1.0f, rampSamples);
            delayWet.set (ds.enabled ? wetGainFor (ds.mix) : 0.0f, rampSamples);

            // A module runs while it is on or still fading out; a finished one is reset once.
            const auto wantReverb = rs.enabled || ! juce::exactlyEqual (reverbWet.value, 0.0f) || ! juce::exactlyEqual (reverbDry.value, 1.0f);
            const auto wantDelay = ds.enabled || ! juce::exactlyEqual (delayWet.value, 0.0f) || ! juce::exactlyEqual (delayDry.value, 1.0f);
            if (reverbActive && ! wantReverb) reverb.reset();
            if (delayActive && ! wantDelay) delay.reset();
            reverbActive = wantReverb;
            delayActive = wantDelay;

            if (! reverbActive && ! delayActive)
            {
                reverbDuckDb = delayDuckDb = 0.0f;
                if (probe != nullptr) probe->written += n;
                continue;
            }

            auto* kL = keyBuffer.getWritePointer (0);
            auto* kR = keyBuffer.getWritePointer (1);
            std::copy (L, L + n, kL);
            std::copy (R, R + n, kR);

            if (reverbActive && delayActive && routing == FxRouting::parallel)
            {
                // out = in * dryD * dryR + wetD * delay(in) + wetR * reverb(in)
                auto* sL = sumBuffer.getWritePointer (0);
                auto* sR = sumBuffer.getWritePointer (1);
                for (int i = 0; i < n; ++i)
                {
                    const auto d = delayDry.next() * reverbDry.next();
                    sL[i] = d * kL[i];
                    sR[i] = d * kR[i];
                }
                // The dry ramps were advanced above; the stages only advance their wet ramps.
                auto delayDryCopy = delayDry, reverbDryCopy = reverbDry;
                runStage (false, kL, kR, sL, sR, n, kL, kR, rs, ds, bpm, delayDryCopy, delayWet, true);
                runStage (true, kL, kR, sL, sR, n, kL, kR, rs, ds, bpm, reverbDryCopy, reverbWet, true);
                std::copy (sL, sL + n, L);
                if (R != L) std::copy (sR, sR + n, R);
            }
            else
            {
                const auto reverbFirst = routing == FxRouting::reverbToDelay;
                auto* tL = stageBuffer.getWritePointer (0);
                auto* tR = stageBuffer.getWritePointer (1);
                const float* curL = kL;
                const float* curR = kR;

                for (int stage = 0; stage < 2; ++stage)
                {
                    const auto isReverb = (stage == 0) == reverbFirst;
                    if (isReverb ? ! reverbActive : ! delayActive)
                        continue;

                    auto* outL = curL == tL ? sumBuffer.getWritePointer (0) : tL;
                    auto* outR = curL == tL ? sumBuffer.getWritePointer (1) : tR;
                    runStage (isReverb, curL, curR, outL, outR, n, kL, kR, rs, ds, bpm,
                              isReverb ? reverbDry : delayDry, isReverb ? reverbWet : delayWet, false);
                    curL = outL;
                    curR = outR;
                }

                // Mono-summed parallel case (one module on): same as series with one stage.
                std::copy (curL, curL + n, L);
                if (R != L) std::copy (curR, curR + n, R);
            }

            if (probe != nullptr) probe->written += n;
            reverbDuckDb = reverbActive ? reverb.getDuckDb() : 0.0f;
            delayDuckDb = delayActive ? delay.getDuckDb() : 0.0f;
        }
    }
}
