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
        t.floorDb = -72.0f + d * 24.0f;
        // Growth is only judged once the analysis window has filled with the tone (an onset fills
        // the window too, which looks like growth): it must then keep rising for persistSec, by at
        // least growthDb and at growthDbPerSec. A tone that holds still is caught at stableSec.
        t.persistSec = (aggressive ? 0.03f : 0.05f) * std::pow (2.0f, 2.0f * d);
        t.stableSec = (aggressive ? 0.35f : 0.7f) * std::pow (2.0f, 2.0f * d);
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
        const auto a = std::pow (10.0f, -cutDb / 40.0f);
        const auto w = freq / notchFreq;
        const auto re = 1.0f - w * w;
        const auto num = re * re + std::pow (w * a / q, 2.0f);
        const auto den = re * re + std::pow (w / (a * q), 2.0f);
        return 10.0f * std::log10 ((num + 1.0e-20f) / (den + 1.0e-20f));
    }

    float FeedbackEngine::bankResponseDb (const NotchArray& notches, float freq) noexcept
    {
        auto total = 0.0f;
        for (const auto& n : notches)
            if (n.active)
                total += notchResponseDb (freq, n.freq, n.cutDb, n.q);
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
        inDb.assign ((size_t) numBins, -140.0f);
        outDb.assign ((size_t) numBins, -140.0f);

        const auto perTick = (double) controlInterval / sampleRate;
        cutAttack = (float) (1.0 - std::exp (-perTick / 0.004));
        cutRelease = (float) (1.0 - std::exp (-perTick / 0.040));
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
        for (auto& s : slots)
        {
            s.ic1 = {};
            s.ic2 = {};
        }
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

        const auto nyquistLimit = (float) (sampleRate * 0.45);
        for (auto& s : slots)
        {
            if (s.id != s.targetId)
            {
                // Fade the previous notch out before this slot takes the new one (no jump in frequency).
                if (s.id != 0 && s.cut > 0.02f)
                    s.cut += (0.0f - s.cut) * cutAttack * 0.5f;
                else
                {
                    s.id = s.targetId;
                    s.cut = 0.0f;
                    s.freq = s.targetFreq;
                    s.q = s.targetQ;
                    s.ic1 = {};
                    s.ic2 = {};
                }
            }

            if (s.id == s.targetId)
            {
                const auto target = s.id != 0 ? std::min (s.targetCut, settings.maxCutDb) : 0.0f;
                s.cut += (target - s.cut) * (target > s.cut ? cutAttack : cutRelease);
                if (s.targetFreq > 0.0f && s.freq > 0.0f)
                    s.freq *= std::exp (glide * std::log (s.targetFreq / s.freq));
                s.q += (s.targetQ - s.q) * glide;
            }

            const auto live = s.cut > 1.0e-3f;
            if (! live && s.live)
            {
                s.ic1 = {};
                s.ic2 = {};
            }
            s.live = live;
            if (live)
            {
                const auto a = std::pow (10.0f, -s.cut / 40.0f);
                const auto g = std::tan (juce::MathConstants<float>::pi * std::min (s.freq, nyquistLimit) / (float) sampleRate);
                const auto k = 1.0f / (std::max (0.5f, s.q) * a);
                s.a1 = 1.0f / (1.0f + g * (g + k));
                s.a2 = g * s.a1;
                s.a3 = g * s.a2;
                s.m1 = k * (a * a - 1.0f);
            }
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

            for (auto& s : slots)
            {
                if (! s.live) continue;
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* x = io[ch] + pos;
                    auto c1 = s.ic1[(size_t) ch], c2 = s.ic2[(size_t) ch];
                    for (int i = 0; i < len; ++i)
                    {
                        const auto v0 = x[i];
                        const auto v3 = v0 - c2;
                        const auto v1 = s.a1 * c1 + s.a2 * v3;
                        const auto v2 = c2 + s.a2 * c1 + s.a3 * v3;
                        c1 = 2.0f * v1 - c1;
                        c2 = 2.0f * v2 - c2;
                        x[i] = v0 + s.m1 * v1;
                    }
                    s.ic1[(size_t) ch] = c1;
                    s.ic2[(size_t) ch] = c2;
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
            const auto prominence = level - neighbourDb ((float) k, 4.0f, 12.0f);
            if (prominence < 8.0f) continue;

            const Peak peak { ((float) k + delta) * binHz, level, prominence };
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

    bool FeedbackEngine::isMusical (float freq, float level, float harmonicDb, bool checkOvertones) const noexcept
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
                        return true;
        }

        // A partial of a lower series. Either a clearly prominent peak at freq / n with one more of
        // its partials, or (in a chord the fundamentals sit in each other's neighbourhoods and may
        // not read as peaks at all) two of the partials right around this one: n-2, n-1, n+1, n+2.
        const auto tight = [&] (float f) { return std::max (1.1f * binHz, 0.006f * f); };
        const auto partialAt = [&] (float f)
        {
            if (f > nyquist * 0.92f) return false;
            const auto* p = peakNear (f, tight (f));
            return p != nullptr && p->level > level - harmonicDb - 12.0f;
        };
        for (int n = 2; n <= 12; ++n)
        {
            const auto f0 = freq / (float) n;
            if (f0 < 30.0f) break;
            int around = 0;
            for (const auto k : { n - 2, n - 1, n + 1, n + 2 })
                if (k >= 1 && partialAt (f0 * (float) k)) ++around;
            if (around >= 2) return true;
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
                        return true;
            }
        }
        return false;
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

        // Candidates: prominent, loud enough, in range, not already under a notch. Strongest 8.
        std::array<const Peak*, 8> candidates {};
        int numCandidates = 0;
        for (int i = 0; i < numPeaks; ++i)
        {
            const auto& p = peaks[(size_t) i];
            if (p.freq < lowHz || p.freq > highHz || p.level < t.floorDb || p.prominence < t.prominenceDb)
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
                }
                else
                    ++match->frames;
                match->freq = p.freq;
                match->lastLevel = p.level;
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
                    *slot = { true, true, p.freq, p.freq, p.level, p.level, 0.0f, 1, 0, 0 };
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
            }
            const auto after = tr.windowFrames > 0 ? (float) (tr.frames - tr.windowFrames) * hopSec : 0.0f;
            const auto rise = tr.lastLevel - tr.windowLevel;
            const auto confirmed = (tr.windowFrames > 0 && after >= t.persistSec
                                    && rise >= t.growthDb && rise >= t.growthDbPerSec * after)
                                || age >= t.stableSec;
            if (! confirmed) continue;

            if (! isMusical (tr.freq, tr.lastLevel, t.harmonicDb, true))
                engage (tr.freq, tr.lastLevel, t);
            tr = {};
        }
    }

    void FeedbackEngine::retrigger (Notch& n, const Thresholds& t) noexcept
    {
        if (! n.ringing)
        {
            if (n.quietSec > 0.25f) ++n.hits;
            n.ringing = true;
        }
        n.quietSec = 0.0f;
        if (settings.ringOut) n.fixed = true;
        n.cutDb = std::min (settings.maxCutDb, n.cutDb + 0.5f * t.initialCutDb);
        n.neededDb = std::max (n.neededDb, n.cutDb);
        n.graceSec = 0.04f;
    }

    void FeedbackEngine::engage (float freq, float level, const Thresholds& t) noexcept
    {
        const auto binHz = getBinHz();

        for (auto& n : model)
            if (n.active && std::abs (freq - n.freq) <= std::max (0.75f * n.freq / n.q, binHz))
            {
                n.levelDb = level;
                retrigger (n, t);
                return;
            }

        for (auto& n : model)
        {
            const auto bandwidth = n.freq / n.q;
            if (n.active && std::abs (freq - n.freq) <= std::max (2.0f * bandwidth, 2.0f * binHz))
            {
                // A neighbour: widen and re-centre one notch over both instead of stacking two.
                const auto centre = 0.5f * (freq + n.freq);
                const auto needed = std::abs (freq - n.freq) + bandwidth;
                n.q = juce::jlimit (minQ, n.q, centre / needed);
                n.freq = centre;
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
            return;
        }

        Notch n;
        n.id = nextId++;
        n.active = true;
        n.fixed = settings.ringOut;
        n.ringing = true;
        n.freq = freq;
        n.q = juce::jlimit (minQ, 64.0f, settings.baseQ);
        n.cutDb = std::min (settings.maxCutDb, t.initialCutDb);
        n.neededDb = n.cutDb;
        n.levelDb = level;
        n.hits = 1;
        n.detectedMs = wallMsNow();
        n.detectedSample = clock.load();
        n.graceSec = 0.04f;
        *slot = n;
    }

    void FeedbackEngine::closedLoop (float hopSec, const Thresholds& t) noexcept
    {
        const auto binHz = getBinHz();
        for (auto& n : model)
        {
            if (! n.active) continue;
            n.graceSec = std::max (0.0f, n.graceSec - hopSec);

            const auto bandwidth = n.freq / n.q;
            const auto centre = n.freq / binHz;
            const auto radius = std::max (2.0f, bandwidth / binHz);
            const auto from = juce::jlimit (1, numBins - 2, (int) std::floor (centre - radius));
            const auto to = juce::jlimit (1, numBins - 2, (int) std::ceil (centre + radius));
            auto peak = -200.0f;
            int peakBin = from;
            for (int k = from; k <= to; ++k)
                if (detDb[(size_t) k] > peak) { peak = detDb[(size_t) k]; peakBin = k; }
            const auto prominence = peak - neighbourDb (centre, radius + 3.0f, radius + 12.0f);
            const auto peakFreq = (float) peakBin * binHz;
            n.levelDb = peak;

            if (n.historyCount < 4)
                n.history[n.historyCount++] = peak;
            else
            {
                std::copy (n.history + 1, n.history + 4, n.history);
                n.history[3] = peak;
            }
            const auto slope = n.historyCount >= 2 ? (n.history[n.historyCount - 1] - n.history[0]) / ((float) (n.historyCount - 1) * hopSec) : 0.0f;

            const auto ringing = peak >= t.floorDb && prominence >= t.prominenceDb - 4.0f
                              && ! isMusical (peakFreq, peak, t.harmonicDb, false);
            if (ringing)
            {
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
                    if (std::abs (peakFreq - n.freq) > 0.35f * bandwidth)
                    {
                        // Ringing on the notch's shoulder: lean the notch over and widen it a little.
                        n.freq += 0.25f * (peakFreq - n.freq);
                        n.q = std::max (minQ, n.q * 0.97f);
                    }
                }
                n.neededDb = std::max (n.neededDb, n.cutDb);
            }
            else
            {
                n.ringing = false;
                n.quietSec += hopSec;
                if (! n.fixed && ! settings.hold && n.quietSec > settings.releaseSec)
                {
                    n.cutDb -= releaseDbPerSec * hopSec;
                    if (n.cutDb < 0.25f) n = Notch {};
                }
            }
            if (n.active) n.cutDb = std::min (n.cutDb, settings.maxCutDb);
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
                    n.cutDb = juce::jlimit (0.0f, 30.0f, n.cutDb);
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
                n.cutDb = juce::jlimit (0.0f, 30.0f, cutDb);
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
