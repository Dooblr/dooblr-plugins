#include "FeedbackEngine.h"

#include <algorithm>
#include <cmath>

namespace dooblrfeedback::dsp
{
    namespace
    {
        constexpr float tiny = 1.0e-14f;

        inline float monotoneAt (const float* v, float pos, int first, int last) noexcept
        {
            pos = juce::jlimit ((float) first, (float) last, pos);
            const auto i = std::min ((int) pos, last - 1);
            if (i < first) return v[first];
            const auto t = pos - (float) i;
            const auto at = [&] (int k) { return v[juce::jlimit (first, last, k)]; };
            const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
            const auto d0 = y1 - y0, d1 = y2 - y1, d2 = y3 - y2;
            const auto tangent = [] (float a, float b) { return a * b <= 0.0f ? 0.0f : 2.0f * a * b / (a + b); };
            const auto m1 = tangent (d0, d1), m2 = tangent (d1, d2);
            const auto t2 = t * t, t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * y1 + (t3 - 2.0f * t2 + t) * m1 + (-2.0f * t3 + 3.0f * t2) * y2 + (t3 - t2) * m2;
        }

        inline float toDb (float power) noexcept { return 10.0f * std::log10 (power + tiny); }
    }

    //==============================================================================
    float DisplayFrame::pointFrequency (int point) noexcept
    {
        const auto t = (float) point / (float) (numPoints - 1);
        return minHz * std::pow (maxHz / minHz, t);
    }

    void DisplayExchange::publish (const DisplayFrame& f) noexcept
    {
        frames[(size_t) writeIndex] = f;
        const auto previous = middle.exchange (writeIndex | freshBit, std::memory_order_acq_rel);
        writeIndex = previous & indexMask;
    }

    bool DisplayExchange::read (DisplayFrame& out) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & freshBit) == 0)
            return false;
        const auto previous = middle.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = previous & indexMask;
        out = frames[(size_t) readIndex];
        return true;
    }

    //==============================================================================
    FeedbackEngine::Thresholds FeedbackEngine::thresholdsFor (const Settings& s) noexcept
    {
        const auto aggressive = s.aggressive || s.ringOut;
        const auto d = 0.5f - juce::jlimit (0.0f, 1.0f, s.sensitivity); // -0.5 (hot) .. +0.5 (cautious)
        Thresholds t {};
        t.prominenceDb = (aggressive ? 11.0f : 15.0f) + d * 12.0f;
        // A runaway tone is tracked from a lower prominence while it is still climbing: at high
        // frequencies the loop resonance is wide in Hz, so several loop modes (spaced 1 / loop
        // delay apart) grow together and the strongest stands only 8-14 dB over its neighbours
        // until it is already at full level. Such a seed is only confirmed by fast, sustained growth.
        t.seedProminenceDb = std::max (8.0f, t.prominenceDb - 7.0f);
        t.floorDb = -72.0f + d * 24.0f;
        // Growth is only judged once the analysis window has filled with the tone (an onset fills
        // the window too, which looks like growth): it must then keep rising for persistSec, by at
        // least growthDb and at growthDbPerSec. A tone that holds still is caught at stableSec.
        t.persistSec = (aggressive ? 0.03f : 0.05f) * std::pow (2.0f, 2.0f * d);
        t.stableSec = (aggressive ? 0.35f : 0.7f) * std::pow (2.0f, 2.0f * d);
        t.growthSec = (aggressive ? 0.10f : 0.16f) * std::pow (2.0f, 2.0f * d);
        t.growthDb = aggressive ? 1.0f : 1.5f;
        t.growthDbPerSec = aggressive ? 20.0f : 30.0f;
        t.harmonicDb = aggressive ? 18.0f : 26.0f;
        t.initialCutDb = aggressive ? 8.0f : 6.0f;
        t.deepenDbPerSec = aggressive ? 90.0f : 60.0f;
        return t;
    }

    float FeedbackEngine::notchResponseDb (float freq, float notchFreq, float cutDb, float q) noexcept
    {
        if (cutDb <= 0.0f || notchFreq <= 0.0f || q <= 0.0f) return 0.0f;
        // H(s) = (s^2 + s k g + 1) / (s^2 + s k + 1), k = 1 / (Q A), A = 10^(-min(cut, cap) / 40),
        // g = 10^(-cut / 20): the usual bell up to the cap; beyond it the zeros approach the unit
        // circle (g = 0 at full cut, a true notch).
        const auto a = std::pow (10.0f, -std::min (cutDb, bellCapDb) / 40.0f);
        const auto g = isFullCut (cutDb) ? 0.0f : std::pow (10.0f, -cutDb / 20.0f);
        const auto k = 1.0f / (q * a);
        const auto w = freq / notchFreq;
        const auto re = 1.0f - w * w;
        const auto num = re * re + std::pow (w * k * g, 2.0f);
        const auto den = re * re + std::pow (w * k, 2.0f);
        return 10.0f * std::log10 ((num + 1.0e-20f) / (den + 1.0e-20f));
    }

    float FeedbackEngine::bandResponseDb (float freq, float centreHz, float cutDb, float notchQ, float bellQ) noexcept
    {
        if (cutDb <= 0.0f || centreHz <= 0.0f) return 0.0f;
        auto total = 0.0f;
        const auto bellCut = std::min (cutDb, bellShareDb);
        if (bellCut > 0.0f && bellQ > 0.0f)
            total += notchResponseDb (freq, centreHz, bellCut, bellQ);
        if (cutDb > bellShareDb && notchQ > 0.0f)
        {
            const auto notchCut = isFullCut (cutDb) ? fullCutDb : cutDb - bellShareDb;
            total += notchResponseDb (freq, centreHz, notchCut, notchQ);
        }
        return total;
    }

    float FeedbackEngine::bankResponseDb (const NotchArray& notches, float freq, float bellQ) noexcept
    {
        auto total = 0.0f;
        for (const auto& n : notches)
            if (n.active)
                total += bandResponseDb (freq, n.freq, n.cutDb, n.q, bellQ);
        return total;
    }

    //==============================================================================
    void FeedbackEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        const int scale = sampleRate <= 50000.0 ? 1 : (sampleRate <= 100000.0 ? 2 : 4);
        fftOrder = 12 + (scale == 1 ? 0 : (scale == 2 ? 1 : 2));
        fftSize = 1 << fftOrder;
        hopSize = 256 * scale;
        numBins = fftSize / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);
        auto sum = 0.0f;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
            sum += window[(size_t) i];
        }
        specNorm = 2.0f / sum;

        fftData.assign ((size_t) fftSize * 2, 0.0f);
        detRing.assign ((size_t) fftSize, 0.0f);
        inRing.assign ((size_t) fftSize, 0.0f);
        outRing.assign ((size_t) fftSize, 0.0f);
        detDb.assign ((size_t) numBins, -140.0f);
        detPower.assign ((size_t) numBins, 0.0f);
        for (auto* v : { &detRe, &detIm, &prevRe, &prevIm }) v->assign ((size_t) numBins, 0.0f);
        havePrevSpectrum = false;
        inDb.assign ((size_t) numBins, -140.0f);
        outDb.assign ((size_t) numBins, -140.0f);

        const auto perTick = (double) controlInterval / sampleRate;
        cutAttack = (float) (1.0 - std::exp (-perTick / 0.004));
        cutRelease = (float) (1.0 - std::exp (-perTick / 0.040));
        safetyAttack = (float) (1.0 - std::exp (-1.0 / (safetyAttackSec * sampleRate)));
        safetyEnvRelease = (float) std::exp (-1.0 / (0.05 * sampleRate));
        glide = (float) (1.0 - std::exp (-perTick / 0.020));

        const auto binHz = getBinHz();
        const auto step = std::pow (DisplayFrame::maxHz / DisplayFrame::minHz, 0.5f / (DisplayFrame::numPoints - 1));
        for (int p = 0; p < DisplayFrame::numPoints; ++p)
        {
            const auto f = DisplayFrame::pointFrequency (p);
            pointBin[(size_t) p] = f / binHz;
            pointWidth[(size_t) p] = f * (step - 1.0f / step) / binHz;
        }

        setWallClockBase (juce::Time::currentTimeMillis());
        reset();
    }

    void FeedbackEngine::reset() noexcept
    {
        std::fill (detRing.begin(), detRing.end(), 0.0f);
        std::fill (inRing.begin(), inRing.end(), 0.0f);
        std::fill (outRing.begin(), outRing.end(), 0.0f);
        ringPos = 0;
        sampleInHop = 0;
        hopCount = 0;
        havePrevSpectrum = false;
        safetyGain = 1.0f;
        safetyEnv = 0.0f;
        safetyHold = safetyHint = safetyLate = 0;
        safetyDucking = false;
        safetyStage = safetyDwell = safetyCalm = 0;
        safetyStageGain = 1.0f;
        safetyStartDb = 0.0f;
        safetyScale = 1.0f;
        safetyActive.store (false);
        safetyReductionDb.store (0.0f);
        for (auto& s : slots)
            s.bell = s.notch = {};
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (auto& t : trackers) t = {};
            for (auto& n : model) { n.historyCount = 0; n.graceSec = 0.0f; }
        }
        seenVersion = -1;
    }

    void FeedbackEngine::setWallClockBase (juce::int64 wallMs) noexcept
    {
        clockAtBase.store (clock.load());
        wallBaseMs.store (wallMs);
    }

    juce::int64 FeedbackEngine::wallMsNow() const noexcept
    {
        const auto samples = clock.load() - clockAtBase.load();
        return wallBaseMs.load() + (juce::int64) ((double) samples * 1000.0 / sampleRate);
    }

    //==============================================================================
    void FeedbackEngine::designCut (Filter& filter, float freq, float q, float cutDb, bool fullNotch) noexcept
    {
        // Constant-Q bell up to bellCapDb (A^2 = depth). A full notch (fullNotch and fullCutDb)
        // puts the zero on the unit circle; see notchResponseDb.
        const auto nyquistLimit = (float) (sampleRate * 0.45);
        const auto a = std::pow (10.0f, -std::min (cutDb, bellCapDb) / 40.0f);
        const auto depth = (fullNotch && isFullCut (cutDb)) ? 0.0f : std::pow (10.0f, -std::max (0.0f, cutDb) / 20.0f);
        const auto g = std::tan (juce::MathConstants<float>::pi * std::min (freq, nyquistLimit) / (float) sampleRate);
        const auto k = 1.0f / (std::max (0.5f, q) * a);
        filter.a1 = 1.0f / (1.0f + g * (g + k));
        filter.a2 = g * filter.a1;
        filter.a3 = g * filter.a2;
        filter.m1 = k * (depth - 1.0f);
    }

    void FeedbackEngine::updateControl() noexcept
    {
        const auto version = modelVersion.load (std::memory_order_acquire);
        if (version != seenVersion)
        {
            const juce::SpinLock::ScopedTryLockType lock (modelLock);
            if (lock.isLocked())
            {
                for (size_t i = 0; i < slots.size(); ++i)
                {
                    const auto& n = model[i];
                    auto& s = slots[i];
                    s.targetId = n.active ? n.id : 0;
                    s.targetCut = n.active ? n.cutDb : 0.0f;
                    s.targetFreq = n.freq;
                    s.targetQ = n.q;
                }
                seenVersion = version;
            }
        }

        const auto bellQ = juce::jlimit (0.7f, 16.0f, settings.bellQ);
        for (auto& s : slots)
        {
            s.targetBellQ = bellQ;
            if (s.id != s.targetId)
            {
                // Fade the previous band out before this slot takes the new one (no jump in frequency).
                if (s.id != 0 && s.cut > 0.02f)
                    s.cut += (0.0f - s.cut) * cutAttack * 0.5f;
                else
                {
                    s.id = s.targetId;
                    s.cut = 0.0f;
                    s.freq = s.targetFreq;
                    s.q = s.targetQ;
                    s.bellQ = s.targetBellQ;
                    s.bell = {};
                    s.notch = {};
                }
            }

            if (s.id == s.targetId)
            {
                const auto target = s.id != 0 ? std::min (s.targetCut, settings.maxCutDb) : 0.0f;
                s.cut += (target - s.cut) * (target > s.cut ? cutAttack : cutRelease);
                if (s.targetFreq > 0.0f && s.freq > 0.0f)
                    s.freq *= std::exp (glide * std::log (s.targetFreq / s.freq));
                s.q += (s.targetQ - s.q) * glide;
                s.bellQ += (s.targetBellQ - s.bellQ) * glide;
            }

            // The smoothed cut drives both filters: the first bellShareDb is the parametric bell,
            // and the rest is the narrow notch (a full cut is still a true notch).
            const auto bellCut = std::min (s.cut, bellShareDb);
            const auto notchCut = s.cut <= bellShareDb ? 0.0f : (isFullCut (s.cut) ? fullCutDb : s.cut - bellShareDb);
            const auto bellLive = bellCut > 1.0e-3f;
            const auto notchLive = notchCut > 1.0e-3f;
            if (! bellLive && s.bell.live) s.bell = {};
            if (! notchLive && s.notch.live) s.notch = {};
            s.bell.live = bellLive;
            s.notch.live = notchLive;
            if (bellLive)  designCut (s.bell, s.freq, s.bellQ, bellCut, false);
            if (notchLive) designCut (s.notch, s.freq, s.q, notchCut, true);
        }
    }

    void FeedbackEngine::process (float* const* io, int numChannels, int numSamples, const float* detector, bool analyse) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        const auto channels = juce::jlimit (0, 2, numChannels);
        if (channels == 0 || fft == nullptr) return;
        const auto inverse = 1.0f / (float) channels;
        const auto mask = fftSize - 1;
        detectorIsOutput = detector == nullptr;

        int pos = 0;
        while (pos < numSamples)
        {
            if (sampleInHop % controlInterval == 0)
                updateControl();
            const auto len = std::min (numSamples - pos, controlInterval - sampleInHop % controlInterval);

            for (int i = 0; i < len; ++i)
            {
                auto sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch) sum += io[ch][pos + i];
                inRing[(size_t) ((ringPos + i) & mask)] = sum * inverse;
            }

            const auto apply = [] (Filter& f, float* x, int n, int ch) noexcept
            {
                auto c1 = f.ic1[(size_t) ch], c2 = f.ic2[(size_t) ch];
                for (int i = 0; i < n; ++i)
                {
                    const auto v0 = x[i];
                    const auto v3 = v0 - c2;
                    const auto v1 = f.a1 * c1 + f.a2 * v3;
                    const auto v2 = c2 + f.a2 * c1 + f.a3 * v3;
                    c1 = 2.0f * v1 - c1;
                    c2 = 2.0f * v2 - c2;
                    x[i] = v0 + f.m1 * v1;
                }
                f.ic1[(size_t) ch] = c1;
                f.ic2[(size_t) ch] = c2;
            };

            for (auto& s : slots)
            {
                if (! s.bell.live && ! s.notch.live) continue;
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* x = io[ch] + pos;
                    if (s.bell.live)  apply (s.bell, x, len, ch);
                    if (s.notch.live) apply (s.notch, x, len, ch);
                }
            }

            for (int i = 0; i < len; ++i)
            {
                auto sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch) sum += io[ch][pos + i];
                const auto idx = (size_t) ((ringPos + i) & mask);
                outRing[idx] = sum * inverse;
                if (detector != nullptr) detRing[idx] = detector[pos + i];
            }

            processSafety (io, channels, pos, len, analyse);

            ringPos = (ringPos + len) & mask;
            sampleInHop += len;
            clock.fetch_add (len, std::memory_order_relaxed);
            pos += len;

            if (sampleInHop >= hopSize)
            {
                sampleInHop = 0;
                analyseHop (analyse);
            }
        }
    }

    //==============================================================================
    /*  Safety duck: the last resort when the notches cannot hold a howl.

        Trigger: the peak level after the notches (a peak follower, instant attack, 50 ms release)
        is over the threshold AND the detector reports a runaway: within the last 0.5 s a notch was
        engaged or re-triggered, a ringing notch sat at Max cut, or a confirmed howl found the bank
        full. ("Always" drops the second condition: a plain over-level duck.) Loud music that is not
        feeding back never arms it, so it is not ducked.

        Action: the main gain falls with a 5 ms time constant by up to Depth (or mutes), is held
        while the trigger persists (plus 80 ms), then recovers cautiously (0.1.2): in three equal dB
        steps, each at Depth / Recover dB per second (linear in dB), with a probe of 0.3 x Recover
        after each of the first two. The duck stays armed through the whole release, so a howl
        that comes back is ducked again at once; one that comes back after the gain had already
        stepped up makes the next release 1.5 x slower (up to 4 x, back to 1 x after 10 s without
        a duck). No lookahead, no latency; every gain change is a smooth ramp. */
    void FeedbackEngine::processSafety (float* const* io, int channels, int start, int len, bool analyse) noexcept
    {
        const auto enabled = settings.safety && analyse;
        if (! enabled && safetyGain >= 1.0f && ! safetyDucking)
        {
            safetyHint = std::max (0, safetyHint - len);
            return;
        }

        const auto thresholdLin = juce::Decibels::decibelsToGain (settings.safetyThresholdDb);
        const auto muting = settings.safetyDepthDb >= fullDuckDb;
        const auto depthLin = muting ? 0.0f : juce::Decibels::decibelsToGain (-std::max (0.0f, settings.safetyDepthDb));
        const auto rangeDb = muting ? 60.0f : std::max (1.0f, settings.safetyDepthDb);
        const auto releaseSec = std::max (0.05f, settings.safetyReleaseSec) * safetyScale;
        const auto recover = std::pow (10.0f, rangeDb / (20.0f * releaseSec * (float) sampleRate));
        const auto holdSamples = (int) (safetyHoldSec * (float) sampleRate);
        const auto dwellSamples = (int) (safetyDwellFraction * releaseSec * (float) sampleRate);
        const auto lateLimit = (int) ((releaseSec * (1.0f + safetyDwellFraction * (float) (safetyStages - 1)) + 0.5f) * (float) sampleRate);
        const auto calmSamples = (int) (safetyCalmSec * (float) sampleRate);

        for (int i = 0; i < len; ++i)
        {
            auto peak = 0.0f;
            for (int ch = 0; ch < channels; ++ch) peak = std::max (peak, std::abs (io[ch][start + i]));
            safetyEnv = peak > safetyEnv ? peak : safetyEnv * safetyEnvRelease;

            // Once a duck has started it stays armed through its release (up to the release time
            // plus 0.5 s after the detector's last report): a runaway that comes back while the
            // level recovers is caught again as the same event, before the detector re-confirms it.
            // Loud programme after the feedback has gone cannot keep it down for longer than that.
            safetyLate = safetyHint > 0 ? 0 : std::min (safetyLate + 1, 1 << 30);
            const auto armed = settings.safetyAlways || safetyHint > 0 || (safetyDucking && safetyLate < lateLimit);
            if (enabled && armed && safetyEnv > thresholdLin)
            {
                if (safetyHold <= 0 && ! safetyDucking)
                {
                    safetyDucking = true;
                    safetyEvents.fetch_add (1, std::memory_order_relaxed);
                    safetyLastMs.store (wallMsNow(), std::memory_order_relaxed);
                }
                else if (safetyHold <= 0 && safetyStage >= 2)
                    safetyScale = std::min (safetyMaxScale, 1.5f * safetyScale);   // came back after a step up
                safetyHold = holdSamples;
                safetyStage = 0;
                safetyDwell = 0;
                safetyCalm = 0;
            }
            if (safetyHint > 0) --safetyHint;

            if (safetyHold > 0)
            {
                --safetyHold;
                safetyGain += (depthLin - safetyGain) * safetyAttack;
            }
            else if (safetyGain < 1.0f)
            {
                if (safetyStage == 0)
                {
                    // Release starts: plan the steps from where the gain is now.
                    safetyStartDb = juce::Decibels::gainToDecibels (std::max (safetyGain, 1.0e-5f), -200.0f);
                    safetyStage = 1;
                    safetyStageGain = juce::Decibels::decibelsToGain (safetyStartDb * (float) (safetyStages - 1) / (float) safetyStages);
                }
                if (safetyDwell > 0)
                    --safetyDwell;   // probing at this step
                else
                {
                    safetyGain = std::min (safetyStageGain, std::max (safetyGain, 1.0e-5f) * recover);
                    if (safetyGain >= safetyStageGain && safetyStage < safetyStages)
                    {
                        ++safetyStage;
                        safetyStageGain = safetyStage >= safetyStages ? 1.0f
                                        : juce::Decibels::decibelsToGain (safetyStartDb * (float) (safetyStages - safetyStage) / (float) safetyStages);
                        safetyDwell = dwellSamples;
                    }
                }
                if (safetyGain >= 1.0f) { safetyGain = 1.0f; safetyDucking = false; safetyStage = 0; }
            }
            else
            {
                safetyDucking = false;
                safetyStage = 0;
                if (safetyScale > 1.0f && ++safetyCalm >= calmSamples) { safetyScale = 1.0f; safetyCalm = 0; }
            }

            for (int ch = 0; ch < channels; ++ch) io[ch][start + i] *= safetyGain;
        }

        if (safetyGain < 0.999f) safetyTotalSamples.fetch_add (len, std::memory_order_relaxed);
        safetyActive.store (safetyHold > 0 || safetyGain < 0.989f, std::memory_order_relaxed); // > 0.1 dB
        safetyReductionDb.store (safetyGain <= 1.0e-5f ? fullDuckDb : -juce::Decibels::gainToDecibels (safetyGain, -fullDuckDb), std::memory_order_relaxed);
    }

    FeedbackEngine::SafetyState FeedbackEngine::getSafety() const noexcept
    {
        SafetyState st;
        st.active = safetyActive.load (std::memory_order_relaxed);
        st.reductionDb = st.active ? safetyReductionDb.load (std::memory_order_relaxed) : 0.0f;
        st.events = safetyEvents.load (std::memory_order_relaxed);
        st.lastEventMs = safetyLastMs.load (std::memory_order_relaxed);
        st.totalSec = (float) ((double) safetyTotalSamples.load (std::memory_order_relaxed) / sampleRate);
        return st;
    }

    //==============================================================================
    void FeedbackEngine::transform (const std::vector<float>& ring, std::vector<float>& db, std::vector<float>* power) noexcept
    {
        const auto mask = fftSize - 1;
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & mask)] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fftData.data(), true);
        const auto norm2 = specNorm * specNorm;
        for (int k = 0; k < numBins; ++k)
        {
            const auto re = fftData[(size_t) (2 * k)], im = fftData[(size_t) (2 * k + 1)];
            const auto pw = (re * re + im * im) * norm2;
            db[(size_t) k] = toDb (pw);
            if (power != nullptr) (*power)[(size_t) k] = pw;
        }
        if (power != nullptr)
        {
            // The detector: keep this hop's and the last hop's spectrum for refineBin.
            std::swap (detRe, prevRe);
            std::swap (detIm, prevIm);
            for (int k = 0; k < numBins; ++k)
            {
                detRe[(size_t) k] = fftData[(size_t) (2 * k)];
                detIm[(size_t) k] = fftData[(size_t) (2 * k + 1)];
            }
        }
    }

    float FeedbackEngine::refineBin (int k, float quadraticBin) const noexcept
    {
        // Phase vocoder: a steady sinusoid advances its phase by 2 pi f hop / fs between two hops;
        // the deviation from bin k's own advance gives its frequency to a small fraction of a bin
        // (the parabola alone is off by up to ~0.05 bin and biased between bins). Only trusted when
        // it agrees with the parabola (two partials in one bin, or noise, do not).
        if (! havePrevSpectrum) return quadraticBin;
        const auto re = detRe[(size_t) k], im = detIm[(size_t) k], pr = prevRe[(size_t) k], pi = prevIm[(size_t) k];
        if (re * re + im * im < 1.0e-20f || pr * pr + pi * pi < 1.0e-20f) return quadraticBin;
        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto advance = std::atan2 ((double) im, (double) re) - std::atan2 ((double) pi, (double) pr);
        const auto expected = twoPi * (double) k * (double) hopSize / (double) fftSize;
        auto deviation = std::remainder (advance - expected, twoPi);
        const auto bin = (double) k + deviation * (double) fftSize / (twoPi * (double) hopSize);
        return std::abs (bin - (double) quadraticBin) <= 0.6 ? (float) bin : quadraticBin;
    }

    void FeedbackEngine::publishDisplay() noexcept
    {
        const auto& out = detectorIsOutput ? detDb : outDb;
        const auto last = numBins - 1;
        for (int p = 0; p < DisplayFrame::numPoints; ++p)
        {
            const auto bin = pointBin[(size_t) p];
            const auto t = juce::jlimit (0.0f, 1.0f, (pointWidth[(size_t) p] - 2.0f) * 0.5f);
            const auto lo = juce::jlimit (1, last, (int) std::floor (bin - 0.5f * pointWidth[(size_t) p]));
            const auto hi = juce::jlimit (1, last, (int) std::ceil (bin + 0.5f * pointWidth[(size_t) p]));
            const auto resample = [&] (const std::vector<float>& v)
            {
                const auto smooth = monotoneAt (v.data(), bin, 1, last);
                if (t <= 0.0f) return smooth;
                auto peak = v[(size_t) lo];
                for (int k = lo + 1; k <= hi; ++k) peak = std::max (peak, v[(size_t) k]);
                return smooth + t * (peak - smooth);
            };
            frame.input[(size_t) p] = resample (inDb);
            frame.output[(size_t) p] = resample (out);
        }
        frame.binHz = getBinHz();
        ++frame.serial;
        display.publish (frame);
    }

    void FeedbackEngine::analyseHop (bool analyse) noexcept
    {
        ++hopCount;
        transform (detectorIsOutput ? outRing : detRing, detDb, &detPower);
        havePrevSpectrum = true;
        if ((hopCount & 1) == 0)
        {
            transform (inRing, inDb);
            if (! detectorIsOutput) transform (outRing, outDb);
            publishDisplay();
        }

        if (! analyse || ! settings.detect)
            return;

        const juce::SpinLock::ScopedTryLockType lock (modelLock);
        if (! lock.isLocked())
        {
            skippedHops.fetch_add (1);
            return;
        }
        detect ((float) hopSize / (float) sampleRate);
        modelVersion.fetch_add (1, std::memory_order_release);
    }

    //==============================================================================
    float FeedbackEngine::localFloorDb (int k, float level) const noexcept
    {
        // The usual neighbourhood is 4 - 12 bins either side. A loud peak (>= -20 dBFS in one bin)
        // is measured against a wider, frequency-relative neighbourhood (about 1/40 - 1/12 of its
        // frequency): with a long loop delay (big host buffers) a howl at a broad resonance grows
        // as a cluster of comb lines ~23 Hz apart, and inside a 4 - 12 bin window it has no
        // prominence at all. Only loud single bins get the wide view, so cymbals and sibilance
        // (spread thinly over many bins) keep the strict local test.
        if (level >= -20.0f)
            return neighbourDb ((float) k, std::max (4.0f, 0.025f * (float) k), std::max (12.0f, 0.08f * (float) k));
        return neighbourDb ((float) k, 4.0f, 12.0f);
    }

    float FeedbackEngine::neighbourDb (float centreBin, float inner, float outer) const noexcept
    {
        auto sum = 0.0f;
        int count = 0;
        const auto add = [&] (int from, int to)
        {
            from = std::max (from, 1);
            to = std::min (to, numBins - 2);
            for (int k = from; k <= to; ++k)
            {
                sum += detPower[(size_t) k];
                ++count;
            }
        };
        add ((int) std::ceil (centreBin - outer), (int) std::floor (centreBin - inner));
        add ((int) std::ceil (centreBin + inner), (int) std::floor (centreBin + outer));
        return count > 0 ? toDb (sum / (float) count) : -140.0f;
    }

    void FeedbackEngine::findPeaks (float floorDb) noexcept
    {
        numPeaks = 0;
        const auto binHz = getBinHz();
        const auto minLevel = floorDb - 12.0f;
        for (int k = 3; k < numBins - 3; ++k)
        {
            const auto m = detDb[(size_t) k];
            if (m < minLevel) continue;
            if (! (m > detDb[(size_t) k - 1] && m >= detDb[(size_t) k + 1] && m > detDb[(size_t) k - 2] && m >= detDb[(size_t) k + 2]))
                continue;

            const auto a = detDb[(size_t) k - 1], c = detDb[(size_t) k + 1];
            const auto denom = a - 2.0f * m + c;
            const auto delta = denom < 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;
            const auto level = m - 0.25f * (a - c) * delta;
            const auto prominence = level - localFloorDb (k, level);
            if (prominence < 8.0f) continue;

            const Peak peak { refineBin (k, (float) k + delta) * binHz, level, prominence };
            if (numPeaks < maxPeaks)
                peaks[(size_t) numPeaks++] = peak;
            else
            {
                // Full: replace the weakest (keeps frequency order by re-sorting below).
                int weakest = 0;
                for (int i = 1; i < numPeaks; ++i)
                    if (peaks[(size_t) i].level < peaks[(size_t) weakest].level) weakest = i;
                if (peaks[(size_t) weakest].level < level)
                    peaks[(size_t) weakest] = peak;
            }
        }
        std::sort (peaks.begin(), peaks.begin() + numPeaks, [] (const Peak& x, const Peak& y) { return x.freq < y.freq; });
    }

    const FeedbackEngine::Peak* FeedbackEngine::peakNear (float freq, float tolerance) const noexcept
    {
        const auto* begin = peaks.data();
        const auto* end = begin + numPeaks;
        const auto* it = std::lower_bound (begin, end, freq - tolerance, [] (const Peak& p, float f) { return p.freq < f; });
        const Peak* best = nullptr;
        for (; it != end && it->freq <= freq + tolerance; ++it)
            if (best == nullptr || it->level > best->level) best = it;
        return best;
    }

    int FeedbackEngine::musicalReason (float freq, float level, float harmonicDb, bool checkOvertones) const noexcept
    {
        const auto binHz = getBinHz();
        const auto nyquist = (float) sampleRate * 0.5f;
        const auto tolerance = [&] (float f) { return std::max (1.1f * binHz, 0.012f * f); };

        if (checkOvertones)
        {
            // Its own second harmonic: a feedback tone is (close to) a pure sine. Only the octave
            // counts: a howl driven into a clipping amp or speaker grows odd harmonics (3f, 5f)
            // from the symmetric clipping, while voices and instruments have a strong 2f.
            const auto f = freq * 2.0f;
            if (f <= nyquist * 0.92f)
                if (const auto* p = peakNear (f, tolerance (f)))
                    if (p->level > level - harmonicDb && p->prominence >= 10.0f)
                        return 1;
        }

        // A partial of a lower series. Either a clearly prominent peak at freq / n with one more of
        // its partials, or (in a chord the fundamentals sit in each other's neighbourhoods and may
        // not read as peaks at all) two of the partials right around this one: n-2, n-1, n+1, n+2.
        const auto tight = [&] (float f) { return std::max (1.1f * binHz, 0.006f * f); };
        const auto partialAt = [&] (float f)
        {
            if (f > nyquist * 0.92f) return false;
            // A real partial is itself a clear peak: at high frequencies the Hz tolerance spans many
            // bins, and the weak ripple of the room noise or of the loop's own neighbouring modes
            // must not pass for a harmonic series (which would let a howl through).
            const auto* p = peakNear (f, tight (f));
            return p != nullptr && p->prominence >= 10.0f && p->level > level - harmonicDb - 12.0f;
        };
        for (int n = 2; n <= 12; ++n)
        {
            const auto f0 = freq / (float) n;
            if (f0 < 30.0f) break;
            int around = 0;
            for (const auto k : { n - 2, n - 1, n + 1, n + 2 })
                if (k >= 1 && partialAt (f0 * (float) k)) ++around;
            if (around >= 2) return 2;
        }
        for (int i = 0; i < numPeaks; ++i)
        {
            const auto& base = peaks[(size_t) i];
            if (base.freq >= freq / 1.9f) break;
            if (base.freq < 30.0f || base.prominence < 12.0f || base.level < level - harmonicDb - 12.0f) continue;
            const auto n = (int) std::lround (freq / base.freq);
            if (n < 2 || n > 16 || std::abs (freq - (float) n * base.freq) > tolerance (freq)) continue;
            for (int m = 2; m <= 16; ++m)
            {
                if (m == n) continue;
                const auto f = base.freq * (float) m;
                if (f > nyquist * 0.92f) break;
                if (const auto* p = peakNear (f, tolerance (f)))
                    if (p->prominence >= 10.0f && p->level > level - harmonicDb - 12.0f)
                        return 3;
            }
        }
        return 0;
    }

    //==============================================================================
    void FeedbackEngine::detect (float hopSec) noexcept
    {
        const auto t = thresholdsFor (settings);
        findPeaks (t.floorDb);
        closedLoop (hopSec, t);

        const auto binHz = getBinHz();
        const auto lowHz = std::max (settings.lowHz, 3.0f * binHz);
        const auto highHz = std::min (settings.highHz, (float) sampleRate * 0.45f);
        if (watchHz > 0.0f) updateWatch (t, lowHz, highHz);

        // The interpolated peak of a tone right at a range edge can land a fraction of a bin outside.
        const auto edgeLow = lowHz - binHz, edgeHigh = highHz + binHz;

        // Candidates: prominent enough to seed, loud enough, in range, not already under a notch.
        std::array<const Peak*, 12> candidates {};
        int numCandidates = 0;
        for (int i = 0; i < numPeaks; ++i)
        {
            const auto& p = peaks[(size_t) i];
            if (p.freq < edgeLow || p.freq > edgeHigh || p.level < t.floorDb || p.prominence < t.seedProminenceDb)
                continue;
            auto covered = false;
            for (const auto& n : model)
                if (n.active && std::abs (p.freq - n.freq) <= std::max (n.freq / n.q, 2.0f * binHz))
                    covered = true;
            if (covered) continue;

            if (numCandidates < (int) candidates.size())
                candidates[(size_t) numCandidates++] = &p;
            else
            {
                int weakest = 0;
                for (int c = 1; c < numCandidates; ++c)
                    if (candidates[(size_t) c]->level < candidates[(size_t) weakest]->level) weakest = c;
                if (candidates[(size_t) weakest]->level < p.level) candidates[(size_t) weakest] = &p;
            }
        }

        for (auto& tr : trackers) tr.seen = false;

        for (int c = 0; c < numCandidates; ++c)
        {
            const auto& p = *candidates[(size_t) c];
            const auto strong = p.prominence >= t.prominenceDb;
            const auto matchTolerance = std::max (0.75f * binHz, 0.005f * p.freq);
            const auto driftTolerance = std::max (1.25f * binHz, 0.008f * p.freq);

            Tracker* match = nullptr;
            for (auto& tr : trackers)
                if (tr.used && ! tr.seen && std::abs (tr.freq - p.freq) <= matchTolerance)
                    if (match == nullptr || std::abs (tr.freq - p.freq) < std::abs (match->freq - p.freq))
                        match = &tr;

            if (match != nullptr)
            {
                match->seen = true;
                match->missed = 0;
                if (std::abs (p.freq - match->firstFreq) > driftTolerance)
                {
                    // Drifting (vibrato, a gliding voice): start over from here.
                    match->firstFreq = p.freq;
                    match->firstLevel = p.level;
                    match->frames = 1;
                    match->windowFrames = 0;
                    match->strongFrames = 0;
                    match->weakRun = 0;
                    match->maxDropDb = 0.0f;
                }
                else
                {
                    ++match->frames;
                    if (match->windowFrames > 0)
                        match->maxDropDb = std::max (match->maxDropDb, match->lastLevel - p.level);
                }
                if (strong) { ++match->strongFrames; match->weakRun = 0; }
                else if (++match->weakRun > 2) match->strongFrames = 0;
                // The strong-run growth window restarts on any weak frame or a dip: feedback
                // climbs steadily, a voice or instrument wobbles.
                if (! strong || match->lastLevel - p.level > strongDipDb) match->strongWindowFrames = 0;
                match->freq = p.freq;
                match->lastLevel = p.level;
                match->prominence = p.prominence;
            }
            else
            {
                Tracker* slot = nullptr;
                for (auto& tr : trackers)
                    if (! tr.used) { slot = &tr; break; }
                if (slot == nullptr)
                    for (auto& tr : trackers)
                        if (! tr.seen && (slot == nullptr || tr.frames < slot->frames)) slot = &tr;
                if (slot != nullptr)
                {
                    *slot = {};
                    slot->used = slot->seen = true;
                    slot->freq = slot->firstFreq = p.freq;
                    slot->firstLevel = slot->lastLevel = p.level;
                    slot->prominence = p.prominence;
                    slot->frames = 1;
                    slot->strongFrames = strong ? 1 : 0;
                }
            }
        }

        for (auto& tr : trackers)
        {
            if (! tr.used) continue;
            if (! tr.seen)
            {
                if (++tr.missed > 2) tr = {};
                continue;
            }

            const auto age = (float) tr.frames * hopSec;
            if (tr.windowFrames == 0 && age >= windowFillSec())
            {
                tr.windowFrames = tr.frames;
                tr.windowLevel = tr.lastLevel;
                tr.windowProminence = tr.prominence;
            }
            const auto after = tr.windowFrames > 0 ? (float) (tr.frames - tr.windowFrames) * hopSec : 0.0f;
            const auto rise = tr.lastLevel - tr.windowLevel;
            const auto strongNow = tr.prominence >= t.prominenceDb;
            // Growing at full prominence, or (from a seed) climbing steeply and without a dip for
            // fastSec: a feedback runaway; voices and instruments do not swell 9 dB+ at 60 dB/s
            // for that long once the window has filled. Or holding at full prominence for stableSec.
            // The growth test runs over the current run at full prominence (as 0.1.0, whose
            // trackers only lived while strong): its own window, filled from the strong start.
            if (tr.strongFrames < tr.strongWindowFrames) tr.strongWindowFrames = 0;
            if (tr.strongWindowFrames == 0 && tr.strongFrames > 0 && (float) tr.strongFrames * hopSec >= windowFillSec())
            {
                tr.strongWindowFrames = tr.strongFrames;
                tr.strongWindowLevel = tr.lastLevel;
            }
            const auto strongAfter = tr.strongWindowFrames > 0 ? (float) (tr.strongFrames - tr.strongWindowFrames) * hopSec : 0.0f;
            const auto strongRise = tr.lastLevel - tr.strongWindowLevel;
            // A vowel onset or swell climbs a few dB within 50 - 100 ms and then stops; feedback
            // keeps climbing, so growth must hold for growthSec without a dip (which restarts it).
            const auto growing = tr.strongWindowFrames > 0 && strongNow && strongAfter >= t.growthSec
                              && strongRise >= t.growthDb && strongRise >= t.growthDbPerSec * strongAfter;
            const auto runaway = tr.windowFrames > 0 && after >= fastSec && rise >= fastRiseDb
                              && rise >= fastDbPerSec * after && tr.maxDropDb <= 0.75f
                              && tr.prominence >= tr.windowProminence + 2.0f;
            const auto confirmed = growing || runaway || (float) tr.strongFrames * hopSec >= t.stableSec;
            if (! confirmed) continue;

            const auto reason = musicalReason (tr.freq, tr.lastLevel, t.harmonicDb, true);
            const auto watched = watchHz > 0.0f && std::abs (tr.freq - watchHz) <= watchTolHz;
            if (watched)
            {
                ++watch.confirmed;
                if (reason == 1) ++watch.musicalOvertone;
                if (reason == 2) ++watch.musicalAround;
                if (reason == 3) ++watch.musicalBase;
                if (reason == 0) ++watch.engaged;
            }
            if (reason == 0)
            {
                // The faster it grew, the higher the loop gain: start deeper (up to +6 dB), so a
                // fast runaway is not left ringing on the initial cut while the closed loop deepens it.
                const auto rate = std::max (after > 0.0f ? rise / after : 0.0f, strongAfter > 0.0f ? strongRise / strongAfter : 0.0f);
                engage (tr.freq, tr.lastLevel, t, juce::jlimit (0.0f, 6.0f, 0.05f * (rate - 30.0f)));
            }
            tr = {};
        }
    }

    void FeedbackEngine::updateWatch (const Thresholds& t, float lowHz, float highHz) noexcept
    {
        ++watch.hops;
        const auto binHz = getBinHz();
        const auto from = juce::jlimit (3, numBins - 4, (int) std::floor ((watchHz - watchTolHz) / binHz));
        const auto to = juce::jlimit (3, numBins - 4, (int) std::ceil ((watchHz + watchTolHz) / binHz));
        int k = from;
        for (int i = from; i <= to; ++i)
            if (detDb[(size_t) i] > detDb[(size_t) k]) k = i;
        const auto level = detDb[(size_t) k];
        const auto prominence = level - localFloorDb (k, level);
        const auto freq = (float) k * binHz;
        watch.maxProminence = std::max (watch.maxProminence, prominence);
        watch.maxLevel = std::max (watch.maxLevel, level);
        if (watch.hops <= 256)
        {
            watch.traceLevel[(size_t) watch.hops - 1] = level;
            watch.traceProminence[(size_t) watch.hops - 1] = prominence;
            watch.traceFreq[(size_t) watch.hops - 1] = freq;
        }
        auto covered = false;
        for (const auto& n : model)
            if (n.active && std::abs (freq - n.freq) <= std::max (n.freq / n.q, 2.0f * binHz))
                covered = true;
        if (covered) ++watch.covered;
        else if (freq < lowHz || freq > highHz) ++watch.outOfRange;
        else if (level < t.floorDb) ++watch.belowFloor;
        else if (prominence < t.prominenceDb) ++watch.lowProminence;
        else ++watch.candidate;
    }

    float FeedbackEngine::mergeHz (const Notch& n, float binHz) noexcept
    {
        // Two notch bandwidths, kept between 1/12 and 1/6 octave either side (+-5.9 % .. +-12 %),
        // never under two bins: a detection that close is the same howl (moved, or estimated a
        // little off) and goes to this notch; howls a third of an octave apart stay separate.
        const auto bandwidth = n.freq / n.q;
        return std::max (2.0f * binHz, juce::jlimit (0.0595f * n.freq, 0.1225f * n.freq, 2.0f * bandwidth));
    }

    void FeedbackEngine::retrigger (Notch& n, const Thresholds& t, float freq) noexcept
    {
        if (freq > 0.0f)
        {
            const auto bandwidth = n.freq / n.q;
            const auto offset = freq - n.freq;
            // A notch already at Max cut with the howl still ringing off its centre: fix that one
            // notch (re-centre halfway, the filter glides there with no step, and widen it) rather
            // than add a neighbour. A notch that still has depth to give is only deepened: the
            // closed loop already leans it after a slow drift, and moving it on every detection
            // lets passing partials (speech, instruments) drag it off the howl it holds.
            if (n.cutDb >= settings.maxCutDb - 0.5f && std::abs (offset) > 0.2f * bandwidth)
            {
                if (! n.fixed || settings.ringOut) n.freq += 0.5f * offset;
                n.q = std::max (minQ, n.q * 0.85f);
            }
            else if (std::abs (offset) > 0.35f * bandwidth && (! n.fixed || settings.ringOut))
            {
                // A fresh confirmation well off the centre (at high frequencies the loop modes sit
                // close together and the first to be confirmed is not always the one that wins):
                // re-centre halfway and widen a little, still one notch.
                n.freq += 0.5f * offset;
                n.q = std::max (minQ, n.q * 0.9f);
            }
            // The lean is bounded around the latest confirmed howl.
            n.anchorHz = freq;
        }
        if (n.recovering) reonset (n);
        if (! n.ringing)
        {
            if (n.quietSec > 0.25f) ++n.hits;
            n.ringing = true;
        }
        n.quietSec = 0.0f;
        if (settings.ringOut) n.fixed = true;
        n.cutDb = std::min (settings.maxCutDb, n.cutDb + 0.5f * t.initialCutDb);
        armSafety();
        n.neededDb = std::max (n.neededDb, n.cutDb);
        n.graceSec = 0.04f;
    }

    void FeedbackEngine::engage (float freq, float level, const Thresholds& t, float extraCutDb) noexcept
    {
        const auto binHz = getBinHz();

        for (auto& n : model)
            if (n.active && ! n.merging && std::abs (freq - n.freq) <= std::max (0.75f * n.freq / n.q, binHz))
            {
                n.levelDb = level;
                retrigger (n, t, freq);
                return;
            }

        for (auto& n : model)
        {
            const auto bandwidth = n.freq / n.q;
            const auto window = mergeHz (n, binHz);
            if (n.active && ! n.merging && std::abs (freq - n.freq) <= window)
            {
                // A neighbour: widen and re-centre one notch over both instead of stacking two.
                const auto centre = 0.5f * (freq + n.freq);
                const auto needed = std::abs (freq - n.freq) + bandwidth;
                n.q = juce::jlimit (minQ, n.q, centre / needed);
                n.freq = centre;
                n.anchorHz = centre;
                n.levelDb = level;
                retrigger (n, t);
                return;
            }
        }

        Notch* slot = nullptr;
        for (auto& n : model)
            if (! n.active) { slot = &n; break; }
        if (slot == nullptr)
        {
            // Bank full: replace the shallowest floating notch that is not ringing.
            for (auto& n : model)
                if (! n.fixed && ! n.ringing && (slot == nullptr || n.cutDb < slot->cutDb)) slot = &n;
        }
        if (slot == nullptr)
        {
            bankFull.store (true);
            armSafety(); // a runaway with nowhere to put a notch
            return;
        }

        Notch n;
        n.id = nextId++;
        n.active = true;
        n.fixed = settings.ringOut;
        n.ringing = true;
        n.freq = freq;
        n.anchorHz = freq;
        n.q = juce::jlimit (minQ, 64.0f, settings.baseQ);
        n.cutDb = std::min (settings.maxCutDb, t.initialCutDb + extraCutDb);
        n.neededDb = n.cutDb;
        n.levelDb = level;
        n.catchDb = level;
        n.hits = 1;
        n.detectedMs = wallMsNow();
        n.detectedSample = clock.load();
        n.graceSec = 0.04f;
        *slot = n;
        armSafety();
    }

    void FeedbackEngine::closedLoop (float hopSec, const Thresholds& t) noexcept
    {
        const auto binHz = getBinHz();
        for (auto& n : model)
        {
            if (! n.active) continue;
            if (n.merging)
            {
                // Folded into a neighbour that already holds the cut: fade out, then free the slot.
                n.ringing = false;
                n.cutDb -= mergeFadeDbPerSec * hopSec;
                if (n.cutDb < 0.25f) n = Notch {};
                continue;
            }
            n.graceSec = std::max (0.0f, n.graceSec - hopSec);

            const auto bandwidth = n.freq / n.q;
            const auto centre = n.freq / binHz;
            const auto radius = std::max (2.0f, bandwidth / binHz);
            // The peak is read on the narrow notch. Prominence is against bins outside the parametric
            // bell, so the bell's own scoop is not mistaken for a peak in this region.
            const auto bellRadius = std::max (radius, (n.freq / std::max (settings.bellQ, 0.7f)) / binHz);
            const auto from = juce::jlimit (1, numBins - 2, (int) std::floor (centre - radius));
            const auto to = juce::jlimit (1, numBins - 2, (int) std::ceil (centre + radius));
            auto peak = -200.0f;
            int peakBin = from;
            for (int k = from; k <= to; ++k)
                if (detDb[(size_t) k] > peak) { peak = detDb[(size_t) k]; peakBin = k; }
            const auto prominence = peak - neighbourDb (centre, bellRadius + 3.0f, bellRadius + 12.0f);
            const auto peakFreq = (float) peakBin * binHz;
            n.levelDb = peak;
            // Smoothed level for the recovery probe (always running, so it is settled when recovery starts).
            n.probeDb = n.probeDb < -150.0f ? peak : n.probeDb + probeSmoothing * (peak - n.probeDb);

            if (n.historyCount < 4)
                n.history[n.historyCount++] = peak;
            else
            {
                std::copy (n.history + 1, n.history + 4, n.history);
                n.history[3] = peak;
            }
            const auto slope = n.historyCount >= 2 ? (n.history[n.historyCount - 1] - n.history[0]) / ((float) (n.historyCount - 1) * hopSec) : 0.0f;

            // A steady tonal peak inside the span but well off the centre, holding still for
            // half of stableSec: the loop mode that is really running (close loop modes at high
            // frequencies; the first one confirmed is not always the winner) left ringing on the
            // notch's skirt. Re-centre halfway onto it, still one notch (0.1.0 left 12 kHz at
            // 48 kHz / 64 held 3.6 dB over the loop-free level with the notch 290 Hz off).
            if ((! n.fixed || settings.ringOut)
                && std::abs (peakFreq - n.freq) > 0.35f * bandwidth && prominence >= 6.0f && peak >= t.floorDb
                && ! isMusical (peakFreq, peak, t.harmonicDb, false))
            {
                if (std::abs (peakFreq - n.offPeakHz) <= 1.5f * binHz) n.offPeakSec += hopSec;
                else { n.offPeakHz = peakFreq; n.offPeakSec = 0.0f; }
                // Just after the catch, a loud off-centre peak is the howl itself (the estimate
                // landed on the neighbouring mode): move within a few hops, before the skirt
                // cut has pushed it under the floor (12 kHz at 48 kHz / 64 decays from -47 to
                // -78 dBFS in 0.3 s and then stays 3.6 dB over the loop-free level). Only a peak as loud
                // as the catch: the skirts of a moving howl under the new cut are much quieter.
                const auto young = (double) (clock.load() - n.detectedSample) < 0.4 * sampleRate;
                const auto holdSec = (young && prominence >= t.prominenceDb - 4.0f && peak >= n.catchDb - 3.0f) ? 0.05f : 0.5f * t.stableSec;
                if (n.offPeakSec >= holdSec)
                {
                    n.freq += 0.5f * (n.offPeakHz - n.freq);
                    n.anchorHz = n.offPeakHz;
                    n.q = std::max (minQ, n.q * 0.9f);
                    n.offPeakHz = 0.0f;
                    n.offPeakSec = 0.0f;
                }
            }
            else
                n.offPeakSec = 0.0f;

            const auto ringThreshold = t.prominenceDb - 4.0f;
            const auto ringing = peak >= t.floorDb && prominence >= ringThreshold
                              && ! isMusical (peakFreq, peak, t.harmonicDb, false);
            n.ringSec = ringing ? n.ringSec + hopSec : 0.0f;
            if (ringing && n.recovering && n.ringSec < ringConfirmSec)
            {
                // Recovering and it reads as ringing: hold the cut where it is and listen for
                // ringConfirmSec (about two analysis windows). Noise or programme can look like a
                // ring for one window; a real re-onset keeps ringing and is put straight back below.
            }
            else if (ringing)
            {
                if (n.recovering) reonset (n);
                if (! n.ringing)
                {
                    if (n.quietSec > 0.25f) ++n.hits;
                    n.ringing = true;
                }
                n.quietSec = 0.0f;
                if (settings.ringOut) n.fixed = true;
                if (n.graceSec <= 0.0f && slope > -30.0f)
                {
                    n.cutDb = std::min (settings.maxCutDb, n.cutDb + t.deepenDbPerSec * hopSec);
                    if (std::abs (peakFreq - n.freq) > 0.35f * bandwidth && slope > 0.0f)
                    {
                        // Growing on the notch's shoulder (the howl drifted): lean the notch over and
                        // widen it a little. A shoulder peak that is not growing is the neighbouring
                        // room mode (loop gain under 1 there, it rings but does not run away) or the
                        // programme: following it would pull the notch off the howl it holds.
                        // Bounded around where the howl was confirmed: the notch's own hole leaves
                        // the shoulders (speech partials, the next room mode) as the local maximum,
                        // and an unbounded lean walks the notch off the howl it is holding (0.1.0
                        // left 800 Hz under speech with a notch at 737 Hz, 12 kHz at 12.19 kHz).
                        // A howl that really moved further is re-confirmed and re-centred (retrigger).
                        if (n.anchorHz <= 0.0f) n.anchorHz = n.freq;
                        const auto reach = 0.3f * n.anchorHz / n.q;
                        n.freq = juce::jlimit (n.anchorHz - reach, n.anchorHz + reach, n.freq + 0.25f * (peakFreq - n.freq));
                        n.q = std::max (minQ, n.q * 0.97f);
                    }
                }
                n.neededDb = std::max (n.neededDb, n.cutDb);
                // Still ringing at the deepest cut allowed: the notches cannot hold it alone.
                if (n.graceSec <= 0.0f && n.cutDb >= settings.maxCutDb - 0.5f && slope > -30.0f)
                    armSafety();
            }
            else
            {
                n.ringing = false;
                n.quietSec += hopSec;
                if (n.fixed || settings.hold)
                    n.recovering = false;   // locked / ring-out / Auto-recover off: never recovers
                else if (n.quietSec > settings.releaseSec * n.recoverScale)
                    recover (n, hopSec, peak, prominence, t);
            }
            if (n.active) n.cutDb = std::min (n.cutDb, settings.maxCutDb);
        }
        consolidate();
    }

    //==============================================================================
    /*  Auto-recover (0.1.2): once a floating notch has been quiet for Recover after seconds, it
        steps back toward 0 dB slowly and cautiously. Each stage lowers the cut a little (25 % of
        it, 3 - 6 dB), glides there (a ramp in dB, smoothed again by the filter: no clicks), then
        waits and listens. If the ring comes back - the usual ringing test, or the level at the
        notch rising more than the removed cut explains - reonset() puts the cut straight back to
        where the stage started plus a margin and doubles this notch's recovery time for next
        time. After the last stage (at 0 dB) has been probed too, the notch is freed and its label
        fades out. Fixed (locked or ring-out) notches and Auto-recover off never get here. */
    float FeedbackEngine::recoverStageTarget (float cutDb) noexcept
    {
        if (cutDb > bellCapDb) return bellCapDb;
        if (cutDb <= 3.5f) return 0.0f;
        return cutDb - juce::jlimit (3.0f, 6.0f, 0.25f * cutDb);
    }

    float FeedbackEngine::expectedRecoverySec (float cutDb, float recoverAfterSec, int speed) noexcept
    {
        speed = juce::jlimit (0, 2, speed);
        auto sec = recoverAfterSec;
        for (auto cut = cutDb; cut > 0.0f;)
        {
            const auto target = recoverStageTarget (cut);
            sec += (cut - target) / (cut > bellCapDb ? recoverDeepDbPerSec : recoverGlideDbPerSec[speed]) + recoverDwellSec[speed];
            cut = target;
        }
        return sec;
    }

    void FeedbackEngine::startStage (Notch& n) noexcept
    {
        n.stageStartDb = n.cutDb;
        n.stageTargetDb = recoverStageTarget (n.cutDb);
        n.stageDwellSec = 0.0f;
        n.stageBaseDb = n.probeDb;
        n.probeHops = 0;
    }

    void FeedbackEngine::recover (Notch& n, float hopSec, float peak, float prominence, const Thresholds& t) noexcept
    {
        const auto speed = juce::jlimit (0, 2, settings.recoverSpeed);
        if (! n.recovering)
        {
            n.recovering = true;
            startStage (n);
        }
        else
        {
            // A probe: the smoothed (about 0.45 s) level at the notch rising further than the cut removed so
            // far in this stage explains (the programme there comes back up by at most that much),
            // for several hops in a row. Single FFT frames of noise or programme jump around by
            // more than that, so neither the raw peak nor one frame is trusted.
            const auto removed = std::min (n.stageStartDb, bellCapDb) - std::min (n.cutDb, bellCapDb);
            const auto rising = peak >= t.floorDb && n.probeDb - n.stageBaseDb > removed + reonsetRiseDb
                             && prominence >= t.prominenceDb - 10.0f && ! isMusical (n.freq, peak, t.harmonicDb, false);
            n.probeHops = rising ? n.probeHops + 1 : 0;
            if (n.probeHops >= probeConfirmHops)
            {
                reonset (n);
                return;
            }
        }

        if (n.cutDb > n.stageTargetDb)
        {
            const auto rate = (n.cutDb > bellCapDb ? recoverDeepDbPerSec : recoverGlideDbPerSec[speed]) / n.recoverScale;
            n.cutDb = std::max (n.stageTargetDb, n.cutDb - rate * hopSec);
            return;
        }
        n.stageDwellSec += hopSec;
        if (n.stageDwellSec < recoverDwellSec[speed] * n.recoverScale) return;
        if (n.stageTargetDb <= 0.0f) { n = Notch {}; return; }   // fully recovered: free it
        startStage (n);
    }

    void FeedbackEngine::reonset (Notch& n) noexcept
    {
        n.cutDb = std::min (settings.maxCutDb, std::max (n.cutDb, n.stageStartDb + recoverMarginDb));
        n.neededDb = std::max (n.neededDb, n.cutDb);
        n.recoverScale = std::min (recoverMaxScale, 2.0f * n.recoverScale);
        n.recovering = false;
        n.quietSec = 0.0f;
        n.graceSec = 0.04f;
        n.probeHops = 0;
        ++n.reonsets;
    }

    void FeedbackEngine::consolidate() noexcept
    {
        // Notches whose centres drifted to within half a bandwidth of each other do one job: keep
        // one (a fixed one first, else the deeper), give it the deeper cut, the wider Q and both
        // histories, and crossfade the other out (it fades at mergeFadeDbPerSec while the survivor
        // ramps to the cut it took over).
        for (size_t i = 0; i < model.size(); ++i)
            for (size_t j = i + 1; j < model.size(); ++j)
            {
                auto& a = model[i];
                auto& b = model[j];
                if (! a.active || ! b.active || a.merging || b.merging) continue;
                const auto bandwidth = std::max (a.freq / a.q, b.freq / b.q);
                if (std::abs (a.freq - b.freq) > 0.5f * bandwidth) continue;
                const auto keepA = a.fixed != b.fixed ? a.fixed : a.cutDb >= b.cutDb;
                auto& keep = keepA ? a : b;
                auto& drop = keepA ? b : a;
                keep.cutDb = std::max (keep.cutDb, drop.cutDb);
                keep.neededDb = std::max (keep.neededDb, drop.neededDb);
                keep.q = std::min (keep.q, drop.q);
                keep.hits += drop.hits;
                keep.ringing = keep.ringing || drop.ringing;
                keep.recovering = false;   // starts over from the cut it took over
                if (drop.detectedMs > 0 && (keep.detectedMs <= 0 || drop.detectedMs < keep.detectedMs)) keep.detectedMs = drop.detectedMs;
                drop.merging = true;
                drop.fixed = false;
                drop.ringing = false;
                ++mergeCount;
            }
    }

    //==============================================================================
    NotchArray FeedbackEngine::getNotches() const
    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        return model;
    }

    void FeedbackEngine::setNotches (const NotchArray& notches)
    {
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (size_t i = 0; i < model.size(); ++i)
            {
                auto n = notches[i];
                if (n.active)
                {
                    n.id = nextId++;
                    n.ringing = false;
                    n.quietSec = 0.0f;
                    n.graceSec = 0.0f;
                    n.historyCount = 0;
                    n.freq = juce::jlimit (10.0f, 24000.0f, n.freq);
                    n.q = juce::jlimit (minQ, 64.0f, n.q);
                    n.cutDb = juce::jlimit (0.0f, fullCutDb, n.cutDb);
                }
                else
                    n = Notch {};
                model[i] = n;
            }
            bankFull.store (false);
        }
        modelVersion.fetch_add (1, std::memory_order_release);
    }

    bool FeedbackEngine::clearNotch (int notchId)
    {
        auto found = false;
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (auto& n : model)
                if (n.active && n.id == notchId) { n = Notch {}; found = true; }
            if (found) bankFull.store (false);
        }
        modelVersion.fetch_add (1, std::memory_order_release);
        return found;
    }

    void FeedbackEngine::clearAll()
    {
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (auto& n : model) n = Notch {};
            for (auto& tr : trackers) tr = {};
            bankFull.store (false);
        }
        modelVersion.fetch_add (1, std::memory_order_release);
    }

    bool FeedbackEngine::setFixed (int notchId, bool fixed)
    {
        auto found = false;
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (auto& n : model)
                if (n.active && n.id == notchId) { n.fixed = fixed; n.quietSec = 0.0f; found = true; }
        }
        modelVersion.fetch_add (1, std::memory_order_release);
        return found;
    }

    int FeedbackEngine::addNotch (float freq, float cutDb, float q, bool fixed, int hits, juce::int64 detectedMs)
    {
        int newId = 0;
        {
            const juce::SpinLock::ScopedLockType lock (modelLock);
            for (auto& n : model)
            {
                if (n.active) continue;
                n = Notch {};
                n.id = newId = nextId++;
                n.active = true;
                n.fixed = fixed;
                n.freq = juce::jlimit (10.0f, 24000.0f, freq);
                n.cutDb = juce::jlimit (0.0f, fullCutDb, cutDb);
                n.neededDb = n.cutDb;
                n.q = juce::jlimit (minQ, 64.0f, q);
                n.hits = std::max (1, hits);
                n.detectedMs = detectedMs != 0 ? detectedMs : juce::Time::currentTimeMillis();
                break;
            }
        }
        modelVersion.fetch_add (1, std::memory_order_release);
        return newId;
    }

    int FeedbackEngine::countActive() const
    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        return (int) std::count_if (model.begin(), model.end(), [] (const Notch& n) { return n.active; });
    }
}
