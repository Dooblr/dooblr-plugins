#include "LearnSetup.h"

#include "Loudness.h"

#include <algorithm>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

namespace dooblrl::dsp
{
    namespace
    {
        float toDb (float x) noexcept
        {
            return x > 1.0e-12f ? 20.0f * std::log10 (x) : -120.0f;
        }

        void renderCandidate (LimiterEngine& engine, Settings s, const float* left, const float* right,
                              int n, int nCh, std::vector<float>& outL, std::vector<float>& outR)
        {
            s.dither = 0;
            s.unityGain = false;
            s.delta = false;
            s.bypass = false;
            s.lufsTarget = 0;
            engine.setSettings (s);
            engine.reset();
            outL.resize ((size_t) n);
            outR.resize ((size_t) n);
            juce::FloatVectorOperations::copy (outL.data(), left, n);
            if (nCh > 1)
                juce::FloatVectorOperations::copy (outR.data(), right, n);
            else
                juce::FloatVectorOperations::copy (outR.data(), left, n);

            constexpr int chunk = 256;
            for (int i = 0; i < n; i += chunk)
            {
                const auto m = std::min (chunk, n - i);
                float* io[2] { outL.data() + i, outR.data() + i };
                engine.process (io, 2, m);
            }
        }

        float blockPeak (const float* x, int n) noexcept
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
            return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
        }

        float onsetScore (const float* inL, const float* inR, const float* outL, const float* outR,
                          int n, int hop) noexcept
        {
            if (n < hop * 4)
                return 1.0f;
            double retain = 0;
            int count = 0;
            float prevIn = 0;
            for (int i = 0; i + hop < n; i += hop)
            {
                auto inPeak = blockPeak (inL + i, hop);
                auto outPeak = blockPeak (outL + i, hop);
                if (inR != nullptr)
                    inPeak = std::max (inPeak, blockPeak (inR + i, hop));
                if (outR != nullptr)
                    outPeak = std::max (outPeak, blockPeak (outR + i, hop));
                const auto inOn = inPeak > prevIn * 1.6f && inPeak > 0.05f;
                if (inOn)
                {
                    retain += outPeak / std::max (1.0e-6f, inPeak);
                    ++count;
                }
                prevIn = inPeak;
            }
            return count > 0 ? (float) (retain / (double) count) : 1.0f;
        }
    }

    LearnSetup::LearnSetup() : juce::Thread ("dooblr-L Learn")
    {
        ringL.assign ((size_t) fifoCapacity, 0.0f);
        ringR.assign ((size_t) fifoCapacity, 0.0f);
    }

    LearnSetup::~LearnSetup()
    {
        cancel();
        stopThread (2000);
    }

    void LearnSetup::prepare (double sr) noexcept
    {
        sampleRate.store (sr > 1.0 ? sr : 48000.0, std::memory_order_relaxed);
    }

    void LearnSetup::start (const Settings& current, float seconds)
    {
        cancel();
        stopThread (2000);
        baseSettings = current;
        const auto sr = sampleRate.load (std::memory_order_relaxed);
        const auto sec = juce::jlimit (minSeconds, maxSeconds, seconds);
        const auto n = std::max (256, (int) std::lround (sec * sr));
        captureL.assign ((size_t) n, 0.0f);
        captureR.assign ((size_t) n, 0.0f);
        targetSamples.store (n, std::memory_order_relaxed);
        capturedSamples.store (0, std::memory_order_relaxed);
        progressNorm.store (0.0f, std::memory_order_relaxed);
        resultReady.store (false, std::memory_order_release);
        cancelFlag.store (false, std::memory_order_release);
        state.store (listening, std::memory_order_release);
        startThread();
    }

    void LearnSetup::cancel()
    {
        cancelFlag.store (true, std::memory_order_release);
        if (state.load (std::memory_order_acquire) != idle)
            state.store (idle, std::memory_order_release);
        notify();
    }

    LearnResult LearnSetup::takeResult()
    {
        const juce::ScopedLock lock (resultLock);
        resultReady.store (false, std::memory_order_release);
        auto r = pending;
        pending = {};
        return r;
    }

    void LearnSetup::pushAudio (const float* left, const float* right, int numSamples, int numChannels) noexcept
    {
        if (state.load (std::memory_order_acquire) != listening || numSamples <= 0 || left == nullptr)
            return;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        auto write = [&] (int start, int size, int offset)
        {
            if (size <= 0)
                return;
            juce::FloatVectorOperations::copy (ringL.data() + start, left + offset, size);
            if (numChannels > 1 && right != nullptr)
                juce::FloatVectorOperations::copy (ringR.data() + start, right + offset, size);
            else
                juce::FloatVectorOperations::copy (ringR.data() + start, left + offset, size);
        };
        write (start1, size1, 0);
        write (start2, size2, size1);
        fifo.finishedWrite (size1 + size2);
    }

    LearnFeatures LearnSetup::analyse (const float* left, const float* right, int n, int nCh, double sr)
    {
        LearnFeatures f;
        if (n <= 0 || left == nullptr)
            return f;

        float peak = blockPeak (left, n);
        if (nCh > 1 && right != nullptr)
            peak = std::max (peak, blockPeak (right, n));

        double sumSq = 0, sumLow = 0, sumLr = 0, sumL2 = 0, sumR2 = 0;
        float lpL = 0, lpR = 0;
        const auto lpC = (float) std::exp (-2.0 * 3.141592653589793 * 120.0 / std::max (1.0, sr));
        float env = 0, prevEnv = 0;
        int onsets = 0;
        double onsetAcc = 0;
        const auto envAtk = 0.01f, envRel = 0.995f;

        for (int i = 0; i < n; ++i)
        {
            const auto l = left[i];
            const auto r = (nCh > 1 && right != nullptr) ? right[i] : l;
            sumSq += (double) l * (double) l + (double) r * (double) r;
            sumL2 += (double) l * (double) l;
            sumR2 += (double) r * (double) r;
            sumLr += (double) l * (double) r;
            lpL = l + (lpL - l) * lpC;
            lpR = r + (lpR - r) * lpC;
            sumLow += (double) lpL * (double) lpL + (double) lpR * (double) lpR;
            const auto mag = std::max (std::abs (l), std::abs (r));
            env = mag > env ? mag + (env - mag) * envAtk : mag + (env - mag) * envRel;
            if (env > prevEnv * 1.8f && env > 0.08f)
            {
                ++onsets;
                onsetAcc += (double) env;
            }
            prevEnv = env;
        }

        const auto rms = (float) std::sqrt (sumSq / (double) std::max (1, n * std::max (1, nCh)));
        f.peakDb = toDb (peak);
        f.rmsDb = toDb (rms);
        f.crestDb = f.peakDb - f.rmsDb;
        f.onsetRate = (float) onsets / (float) std::max (1.0e-3, (double) n / sr);
        f.onsetStrength = onsets > 0 ? (float) (onsetAcc / (double) onsets) : 0.0f;
        f.lowEnergy = (float) (sumLow / std::max (1.0e-12, sumSq));
        const auto denom = std::sqrt (sumL2 * sumR2);
        f.stereoCorr = denom > 1.0e-12 ? (float) (sumLr / denom) : 1.0f;

        LoudnessMeter loud;
        loud.prepare (sr);
        loud.process (left, nCh > 1 && right != nullptr ? right : left, n, nCh > 1 ? 2 : 1);
        f.momentaryLufs = loud.momentaryLufs();
        f.shortTermLufs = loud.shortTermLufs();
        f.integratedLufs = loud.integratedLufs();
        return f;
    }

    LearnResult LearnSetup::analyseAndSearch (const float* left, const float* right, int n, int nCh,
                                              double sr, const Settings& base, const std::atomic<bool>* cancel)
    {
        LearnResult best;
        best.settings = base;
        if (n < 256 || left == nullptr)
            return best;

        const auto feat = analyse (left, right, n, nCh, sr);

        int style = (int) Style::allround;
        if (feat.onsetRate > 3.5f && feat.crestDb > 12.0f)
            style = (int) Style::punchy;
        else if (feat.lowEnergy > 0.35f && feat.stereoCorr > 0.75f)
            style = (int) Style::bus;
        else if (feat.crestDb < 8.0f)
            style = (int) Style::modern;
        else if (feat.onsetRate > 2.0f)
            style = (int) Style::dynamicStyle;

        const int styles[] { style, (int) Style::allround };
        const float thresh0 = juce::jlimit (-36.0f, -1.0f, feat.peakDb - 1.0f);
        const float threshes[] { thresh0, thresh0 - 3.0f, -3.0f };
        const float attacks[] { 0.2f, 2.5f };
        const float releases[] { 30.0f, 80.0f };
        const float enhances[] { 0.0f, 22.0f };
        const float knees[] { feat.crestDb > 12.0f ? 2.0f : 4.0f, feat.crestDb > 12.0f ? 0.5f : 8.0f };
        const float looks[] { 0.0f, base.latencyMode >= 2 ? 8.0f : 4.0f };
        const int nLooks = base.latencyMode <= 0 ? 1 : 2;

        LimiterEngine engine;
        auto work = base;
        work.oversamplingIndex = 0;
        work.latencyMode = 0; // search at Zero so offline renders don't pay FIR
        work.autoComp = true;
        work.lookaheadMs = 0.0f;
        engine.setSettings (work);
        engine.prepare (sr, 256, 0);

        const auto ceilLin = dbToLinear (base.ceilingDb);
        std::vector<float> outL, outR;

        const auto inFeatOnsetHop = std::max (32, (int) std::lround (0.01 * sr));
        const auto inLufs = feat.integratedLufs > -60.0f ? feat.integratedLufs : feat.shortTermLufs;
        const auto tLink = feat.onsetRate > 2.0f ? 0.55f : 1.0f;
        const auto rLink = feat.stereoCorr > 0.7f ? 0.85f : 0.4f;

        for (int st : styles)
            for (float th : threshes)
                for (float at : attacks)
                    for (float rel : releases)
                        for (float en : enhances)
                            for (float kn : knees)
                                for (int li = 0; li < nLooks; ++li)
                                {
                                    if (cancel != nullptr && cancel->load (std::memory_order_acquire))
                                        return best;

                                    Settings cand = work;
                                    cand.style = juce::jlimit (0, (int) Style::safe, st);
                                    cand.thresholdDb = juce::jlimit (-40.0f, 0.0f, th);
                                    cand.attackMs = at;
                                    cand.releaseMs = rel;
                                    cand.enhance = en;
                                    cand.kneeDb = kn;
                                    cand.transientLink = tLink;
                                    cand.releaseLink = rLink;
                                    const auto look = looks[li];
                                    cand.lookaheadMs = look;
                                    cand.ceilingDb = base.ceilingDb;
                                    cand.inputGainDb = base.inputGainDb;

                                    renderCandidate (engine, cand, left, right, n, nCh, outL, outR);

                                    auto peak = blockPeak (outL.data(), n);
                                    peak = std::max (peak, blockPeak (outR.data(), n));
                                    if (peak > ceilLin * 1.02f)
                                        continue;

                                    LoudnessMeter loud;
                                    loud.prepare (sr);
                                    loud.process (outL.data(), outR.data(), n, 2);
                                    const auto outLufs = loud.hasIntegrated() ? loud.integratedLufs() : loud.shortTermLufs();
                                    const auto dLufs = outLufs - inLufs;
                                    if (dLufs < -0.05f)
                                        continue;
                                    const auto retain = onsetScore (left, nCh > 1 ? right : left,
                                                                    outL.data(), outR.data(), n, inFeatOnsetHop);

                                    double sumSq = 0, sumDiff = 0;
                                    for (int i = 0; i < n; ++i)
                                    {
                                        const auto in = nCh > 1 && right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
                                        const auto out = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
                                        sumSq += (double) in * (double) in;
                                        const auto d = out - in;
                                        sumDiff += (double) d * (double) d;
                                    }
                                    const auto dist = (float) (sumDiff / std::max (1.0e-12, sumSq));
                                    const auto score = 6.0f * dLufs + 8.0f * retain - 1.2f * dist;

                                    if (score > best.score)
                                    {
                                        best.score = score;
                                        best.settings = cand;
                                        best.settings.oversamplingIndex = base.oversamplingIndex;
                                        best.settings.latencyMode = base.latencyMode;
                                        best.settings.lookaheadMs = base.latencyMode <= 0 ? look
                                            : (base.latencyMode >= 2 ? std::max (look, 8.0f) : look);
                                        best.settings.autoComp = true;
                                        best.targetLufs = outLufs;
                                        best.valid = true;
                                    }
                                }

        if (! best.valid)
        {
            best.settings = base;
            best.targetLufs = feat.integratedLufs;
            best.valid = false;
        }
        return best;
    }

    void LearnSetup::run()
    {
        // Drain anything left in the ring from a previous take (AbstractFifo::reset is not SPSC-safe).
        for (;;)
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
            const auto got = n1 + n2;
            if (got == 0)
                break;
            fifo.finishedRead (got);
        }

        const auto target = targetSamples.load (std::memory_order_relaxed);
        int write = 0;
        while (! threadShouldExit() && ! cancelFlag.load (std::memory_order_acquire) && write < target)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (std::min (2048, target - write), start1, size1, start2, size2);
            const auto got = size1 + size2;
            if (got == 0)
            {
                wait (4);
                continue;
            }
            auto copy = [&] (int start, int size)
            {
                if (size <= 0)
                    return;
                juce::FloatVectorOperations::copy (captureL.data() + write, ringL.data() + start, size);
                juce::FloatVectorOperations::copy (captureR.data() + write, ringR.data() + start, size);
                write += size;
            };
            copy (start1, size1);
            copy (start2, size2);
            fifo.finishedRead (got);
            capturedSamples.store (write, std::memory_order_relaxed);
            progressNorm.store (0.85f * (float) write / (float) std::max (1, target), std::memory_order_relaxed);
        }

        if (cancelFlag.load (std::memory_order_acquire) || threadShouldExit() || write < 256)
        {
            state.store (idle, std::memory_order_release);
            progressNorm.store (0.0f, std::memory_order_relaxed);
            return;
        }

        state.store (analysing, std::memory_order_release);
        progressNorm.store (0.88f, std::memory_order_relaxed);
        const auto sr = sampleRate.load (std::memory_order_relaxed);
        auto result = analyseAndSearch (captureL.data(), captureR.data(), write, 2, sr, baseSettings, &cancelFlag);
        progressNorm.store (1.0f, std::memory_order_relaxed);
        {
            const juce::ScopedLock lock (resultLock);
            pending = result;
        }
        resultReady.store (true, std::memory_order_release);
        state.store (done, std::memory_order_release);
    }
}
