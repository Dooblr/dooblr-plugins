#include "Analyzer.h"

#include <algorithm>
#include <cmath>

namespace dooblrmeter::dsp
{
    namespace
    {
        constexpr double slotSeconds = 0.1;
        constexpr float clipLevel = 0.999f;

        float toDb (double linear) { return (float) gainToDb (linear); }

        double medianOf (std::vector<double> v)
        {
            if (v.empty())
                return 0.0;
            const auto mid = v.begin() + (std::ptrdiff_t) (v.size() / 2);
            std::nth_element (v.begin(), mid, v.end());
            return *mid;
        }

        /** Median offset between levels and curve over 63 Hz - 8 kHz (robust to one bad region). */
        double alignOffset (const bands::Levels& levels, const bands::Levels& curve)
        {
            std::vector<double> d;
            for (int b = 0; b < bands::count; ++b)
                if (bands::centre (b) >= 60.0 && bands::centre (b) <= 8500.0)
                    d.push_back (levels[(size_t) b] - curve[(size_t) b]);
            return medianOf (d);
        }
    }

    const std::vector<float>& spectrumHz()
    {
        static const std::vector<float> hz = []
        {
            std::vector<float> f;
            constexpr int points = 240;
            for (int i = 0; i < points; ++i)
                f.push_back ((float) (20.0 * std::pow (1000.0, i / (double) (points - 1))));
            return f;
        }();
        return hz;
    }

    void SlotData::merge (const SlotData& o) noexcept
    {
        energy += o.energy; kL += o.kL; kR += o.kR; sqL += o.sqL; sqR += o.sqR; sumL += o.sumL; sumR += o.sumR;
        tp = std::max (tp, o.tp); sp = std::max (sp, o.sp); n += o.n; clips += o.clips;
    }

    SlotData* Analyzer::Table::slot (juce::int64 index)
    {
        if (index < 0 || (size_t) index >= maxSlots)
            return nullptr;
        if ((size_t) index >= slots.size())
            slots.resize ((size_t) index + 1);
        return &slots[(size_t) index];
    }

    SegmentData* Analyzer::Table::segment (juce::int64 index)
    {
        if (index < 0 || (size_t) index >= maxSlots / slotsPerSegment)
            return nullptr;
        if ((size_t) index >= segments.size())
            segments.resize ((size_t) index + 1);
        return &segments[(size_t) index];
    }

    Analyzer::Analyzer()
        : records (8192), chunks (2048)
    {
        snapshot = std::make_shared<Snapshot>();
        prepare (48000.0, 512, 2);
    }

    Analyzer::~Analyzer() = default;

    // =============================================================================================
    // Audio side
    // =============================================================================================
    void Analyzer::prepare (double sr, int, int numChannels)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        channels = juce::jlimit (1, 2, numChannels);
        slotLength = std::max (1, (int) std::lround (slotSeconds * sampleRate));
        for (auto& k : kw)
            k.prepare (sampleRate);
        truePeak.prepare (sampleRate);
        clipRun = {};
        liveAcc.clear();
        songAcc.clear();
        liveSampleCount = 0;
        liveSlotIndex = 0;
        songSlot = -1;
        pending.n = 0;
        pending.liveSample = 0;
        pending.songSample = -1;
        recordFifo.reset();
        chunkFifo.reset();

        // Analysis side state that depends on the rate (tables are kept).
        const juce::ScopedLock lock (tableLock);
        liveBase = -1;
        if (pendingLiveCount < 0)
            pendingLiveCount = (juce::int64) liveTable.slots.size();
        recentLive.clear();

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        window.resize ((size_t) fftSize);
        double w2 = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
            w2 += (double) window[(size_t) i] * window[(size_t) i];
        }
        binScale = 2.0 / (fftSize * w2);
        ringMid.assign ((size_t) fftSize, 0.0f);
        ringSide.assign ((size_t) fftSize, 0.0f);
        fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
        livePower.assign ((size_t) fftSize / 2 + 1, 0.0);
        livePowerFrames = 0.0;
        ringWrite = samplesSinceFrame = 0;
        const auto binHz = sampleRate / fftSize;
        for (int b = 0; b < bands::count; ++b)
        {
            auto lo = (int) std::ceil (bands::lowerEdge (b) / binHz);
            auto hi = (int) std::ceil (bands::upperEdge (b) / binHz);
            lo = juce::jlimit (1, fftSize / 2, lo);
            hi = juce::jlimit (lo + 1, fftSize / 2 + 1, hi);
            bandBins[(size_t) b] = { lo, hi };
        }

        using T = Biquad::Type;
        for (auto* pair : { &lowL, &lowR })
            for (auto& b : *pair)
                b = Biquad::design (T::lowPass, sampleRate, 120.0, 0.7071);
        for (auto* pair : { &highL, &highR })
            for (auto& b : *pair)
                b = Biquad::design (T::highPass, sampleRate, 2000.0, 0.7071);
        sib[0] = Biquad::design (T::highPass, sampleRate, 5000.0, 0.7071);
        sib[1] = sib[0];
        sib[2] = Biquad::design (T::lowPass, sampleRate, 10000.0, 0.7071);
        sibFast = sibSlow = 0.0;
        sibFastCoefA = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
        sibFastCoefR = 1.0 - std::exp (-1.0 / (0.020 * sampleRate));
        sibSlowCoef = 1.0 - std::exp (-1.0 / (0.300 * sampleRate));
        sibHold = 0;
        corrLR = corrLL = corrRR = {};
        corrCoef = 1.0 - std::exp (-1.0 / (0.300 * sampleRate));
        liveMid = liveSide = 0.0;
        scopeRing.assign (2048, 0.0f);
        scopeWrite = scopeDecimate = 0;
        lastLiveSegment = -1;
    }

    void Analyzer::processAudio (const float* left, const float* right, int n, juce::int64 songSample, bool nonRealtime) noexcept
    {
        if (right == nullptr)
            right = left;
        const bool stereo = channels > 1 && right != left;

        if (songSample < 0 && songSlot >= 0)
        {
            if (songAcc.n > 0)
                pushRecord (1, songSlot, songAcc, nonRealtime);
            songAcc.clear();
            songSlot = -1;
        }

        pending.liveSample = liveSampleCount;
        pending.songSample = songSample;
        pending.n = 0;

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i], r = right[i];
            const auto kl = kw[0].process (l);
            const auto kr = stereo ? kw[1].process (r) : 0.0;

            float tp = truePeak.push (0, l);
            if (stereo)
                tp = std::max (tp, truePeak.push (1, r));
            const float sp = std::max (std::abs (l), std::abs (r));

            int clipsHere = 0;
            const float x[2] = { l, r };
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                if (std::abs (x[c]) >= clipLevel)
                {
                    if (++clipRun[(size_t) c] == 3)
                        ++clipsHere;
                }
                else
                {
                    clipRun[(size_t) c] = 0;
                }
            }

            const auto addTo = [&] (SlotData& s)
            {
                s.energy += kl * kl + kr * kr;
                s.kL += kl * kl;
                s.kR += stereo ? kr * kr : kl * kl;
                s.sqL += (double) l * l;
                s.sqR += (double) r * r;
                s.sumL += l;
                s.sumR += r;
                s.tp = std::max (s.tp, tp);
                s.sp = std::max (s.sp, sp);
                s.clips += clipsHere;
                ++s.n;
            };

            addTo (liveAcc);
            if (liveAcc.n >= slotLength)
            {
                pushRecord (0, liveSlotIndex++, liveAcc, nonRealtime);
                liveAcc.clear();
            }

            if (songSample >= 0)
            {
                const auto idx = (songSample + i) / slotLength;
                if (idx != songSlot)
                {
                    if (songSlot >= 0 && songAcc.n > 0)
                        pushRecord (1, songSlot, songAcc, nonRealtime);
                    songAcc.clear();
                    songSlot = idx;
                }
                addTo (songAcc);
            }

            pending.l[pending.n] = l;
            pending.r[pending.n] = r;
            if (++pending.n == Chunk::capacity)
            {
                pushChunk (nonRealtime);
                pending.liveSample = liveSampleCount + i + 1;
                pending.songSample = songSample >= 0 ? songSample + i + 1 : -1;
                pending.n = 0;
            }
        }

        if (pending.n > 0)
            pushChunk (nonRealtime);
        pending.n = 0;
        liveSampleCount += n;
    }

    void Analyzer::pushRecord (int table, juce::int64 index, const SlotData& d, bool nonRealtime) noexcept
    {
        for (int attempt = 0;; ++attempt)
        {
            if (recordFifo.getFreeSpace() > 0)
            {
                const auto scope = recordFifo.write (1);
                records[(size_t) scope.startIndex1] = { table, index, d };
                return;
            }
            if (! nonRealtime || ! consumerRunning.load() || attempt > 2000)
            {
                droppedRecords.fetch_add (1);
                return;
            }
            juce::Thread::sleep (1); // offline render only: let the analysis thread catch up
        }
    }

    void Analyzer::pushChunk (bool nonRealtime) noexcept
    {
        for (int attempt = 0;; ++attempt)
        {
            if (chunkFifo.getFreeSpace() > 0)
            {
                const auto scope = chunkFifo.write (1);
                auto& c = chunks[(size_t) scope.startIndex1];
                c.liveSample = pending.liveSample;
                c.songSample = pending.songSample;
                c.n = pending.n;
                std::copy (pending.l, pending.l + pending.n, c.l);
                std::copy (pending.r, pending.r + pending.n, c.r);
                return;
            }
            if (! nonRealtime || ! consumerRunning.load() || attempt > 2000)
            {
                droppedChunks.fetch_add (1);
                return;
            }
            juce::Thread::sleep (1);
        }
    }

    // =============================================================================================
    // Analysis side
    // =============================================================================================
    void Analyzer::setSettings (const Settings& s) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (settingsLock);
        const auto changed = s.target != settings.target || s.curve != settings.curve || s.historySpan != settings.historySpan
                          || s.sensitivity != settings.sensitivity || s.songMode != settings.songMode
                          || ! juce::exactlyEqual (s.customLufs, settings.customLufs) || ! juce::exactlyEqual (s.customCeiling, settings.customCeiling)
                          || s.averaging != settings.averaging;
        settings = s;
        if (changed)
            settingsChanged.store (true);
    }

    void Analyzer::setTempo (double bpm, double beatsPerBar) noexcept
    {
        tempoBpm.store (bpm);
        tempoBeats.store (beatsPerBar);
    }

    std::shared_ptr<const Snapshot> Analyzer::getSnapshot() const
    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        return snapshot;
    }

    void Analyzer::clearAll()
    {
        liveTable.clear();
        songTable.clear();
        liveBase = -1;
        pendingLiveCount = 0;
        lastSongSlotWritten = -1;
        std::fill (livePower.begin(), livePower.end(), 0.0);
        livePowerFrames = 0.0;
    }

    void Analyzer::pump (bool forceFullUpdate)
    {
        const juce::ScopedLock lock (tableLock);

        if (resetRequested.exchange (false))
        {
            clearAll();
            forceFullUpdate = true;
        }

        for (int ready = recordFifo.getNumReady(); ready > 0; ready = recordFifo.getNumReady())
        {
            const auto scope = recordFifo.read (ready);
            for (int i = 0; i < scope.blockSize1; ++i)
                consumeRecord (records[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i)
                consumeRecord (records[(size_t) (scope.startIndex2 + i)]);
        }

        for (int ready = chunkFifo.getNumReady(); ready > 0; ready = chunkFifo.getNumReady())
        {
            const auto scope = chunkFifo.read (ready);
            for (int i = 0; i < scope.blockSize1; ++i)
                consumeChunk (chunks[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i)
                consumeChunk (chunks[(size_t) (scope.startIndex2 + i)]);
        }

        const auto now = juce::Time::getMillisecondCounterHiRes();
        const bool full = forceFullUpdate || settingsChanged.exchange (false) || now - lastFullUpdate > 200.0;
        if (full)
            lastFullUpdate = now;
        rebuildSnapshot (full);
    }

    void Analyzer::consumeRecord (const SlotRecord& rec)
    {
        if (rec.table == 0)
        {
            if (liveBase < 0)
            {
                liveBase = rec.index - std::max<juce::int64> (0, pendingLiveCount);
                pendingLiveCount = -1;
            }
            if (auto* s = liveTable.slot (rec.index - liveBase))
                *s = rec.data;
            recentLive.push_back (rec.data);
            if (recentLive.size() > 30)
                recentLive.erase (recentLive.begin());
        }
        else if (auto* s = songTable.slot (rec.index))
        {
            *s = rec.data; // replaying a section overwrites it
            lastSongSlotWritten = rec.index;
        }
    }

    void Analyzer::consumeChunk (const Chunk& c)
    {
        const bool frozen = [this] { const juce::SpinLock::ScopedLockType l (settingsLock); return settings.freeze; }();

        juce::int64 liveSeg = -1, songSeg = -1;
        if (liveBase >= 0)
        {
            const auto slot = c.liveSample / slotLength - liveBase;
            if (slot >= 0)
                liveSeg = slot / slotsPerSegment;
        }
        if (c.songSample >= 0)
        {
            songSeg = (c.songSample / slotLength) / slotsPerSegment;
            // A new pass over a segment (loop or replay) starts it afresh.
            auto* seg = songTable.segment (songSeg);
            const auto startSlot = songSeg * slotsPerSegment;
            const auto chunkSlot = c.songSample / slotLength;
            if (seg != nullptr && chunkSlot == startSlot && c.songSample % slotLength < c.n && seg->samples > 0
                && seg->samples >= (long long) slotsPerSegment * slotLength / 2)
                *seg = SegmentData();
        }

        std::array<double, 4> lr {}, ll {}, rr {};
        double midLow = 0.0, sideLow = 0.0;
        int bursts = 0;
        const auto cc = corrCoef;

        for (int i = 0; i < c.n; ++i)
        {
            const double l = c.l[i], r = c.r[i];
            const double m = 0.5 * (l + r), s = 0.5 * (l - r);

            ringMid[(size_t) ringWrite] = (float) m;
            ringSide[(size_t) ringWrite] = (float) s;
            ringWrite = (ringWrite + 1) % fftSize;

            const auto lowl = lowL[1].process (lowL[0].process (l)), lowr = lowR[1].process (lowR[0].process (r));
            const auto hil = highL[1].process (highL[0].process (l)), hir = highR[1].process (highR[0].process (r));
            const double bl[4] = { l, lowl, l - lowl - hil, hil };
            const double br[4] = { r, lowr, r - lowr - hir, hir };
            for (int b = 0; b < 4; ++b)
            {
                lr[(size_t) b] += bl[b] * br[b];
                ll[(size_t) b] += bl[b] * bl[b];
                rr[(size_t) b] += br[b] * br[b];
                corrLR[(size_t) b] += cc * (bl[b] * br[b] - corrLR[(size_t) b]);
                corrLL[(size_t) b] += cc * (bl[b] * bl[b] - corrLL[(size_t) b]);
                corrRR[(size_t) b] += cc * (br[b] * br[b] - corrRR[(size_t) b]);
            }
            const auto ml = 0.5 * (lowl + lowr), sl = 0.5 * (lowl - lowr);
            midLow += ml * ml;
            sideLow += sl * sl;
            liveMid += cc * (m * m - liveMid);
            liveSide += cc * (s * s - liveSide);

            const auto x = sib[2].process (sib[1].process (sib[0].process (m)));
            const auto p = x * x;
            sibFast += (p > sibFast ? sibFastCoefA : sibFastCoefR) * (p - sibFast);
            sibSlow += sibSlowCoef * (p - sibSlow);
            if (sibHold > 0)
                --sibHold;
            else if (sibFast > 4.0 * sibSlow && sibFast > 1.0e-5)
            {
                ++bursts;
                sibHold = (int) (0.08 * sampleRate);
            }

            if (! frozen && ++scopeDecimate >= 2)
            {
                scopeDecimate = 0;
                scopeRing[(size_t) scopeWrite] = (float) l;
                scopeRing[(size_t) scopeWrite + 1] = (float) r;
                scopeWrite = (scopeWrite + 2) % (int) scopeRing.size();
            }

            if (++samplesSinceFrame >= hop)
            {
                samplesSinceFrame = 0;
                analyseFrame (liveSeg, songSeg);
            }
        }

        for (auto* seg : { liveTable.segment (liveSeg), songTable.segment (songSeg) })
        {
            if (seg == nullptr)
                continue;
            for (size_t b = 0; b < 4; ++b)
            {
                seg->lr[b] += lr[b];
                seg->ll[b] += ll[b];
                seg->rr[b] += rr[b];
            }
            seg->midLow += midLow;
            seg->sideLow += sideLow;
            seg->bursts += bursts;
            seg->samples += c.n;
        }
    }

    void Analyzer::analyseFrame (juce::int64 liveSeg, juce::int64 songSeg)
    {
        std::array<double, bands::count> bm {}, bs {};
        std::vector<double> total ((size_t) fftSize / 2 + 1, 0.0);

        for (int pass = 0; pass < 2; ++pass)
        {
            const auto& ring = pass == 0 ? ringMid : ringSide;
            std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fftBuffer[(size_t) i] = ring[(size_t) ((ringWrite + i) % fftSize)] * window[(size_t) i];
            fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

            auto& out = pass == 0 ? bm : bs;
            for (int k = 0; k <= fftSize / 2; ++k)
            {
                const auto p = (double) fftBuffer[(size_t) k] * fftBuffer[(size_t) k] * binScale;
                total[(size_t) k] += p;
            }
            for (int b = 0; b < bands::count; ++b)
            {
                double sum = 0.0;
                for (int k = bandBins[(size_t) b].first; k < bandBins[(size_t) b].second; ++k)
                    sum += (double) fftBuffer[(size_t) k] * fftBuffer[(size_t) k];
                out[(size_t) b] = sum * binScale;
            }
        }

        for (auto* seg : { liveTable.segment (liveSeg), songTable.segment (songSeg) })
        {
            if (seg == nullptr)
                continue;
            for (size_t b = 0; b < (size_t) bands::count; ++b)
            {
                seg->mid[b] += bm[b];
                seg->side[b] += bs[b];
            }
            ++seg->frames;
        }

        Settings s;
        {
            const juce::SpinLock::ScopedLockType l (settingsLock);
            s = settings;
        }
        if (s.freeze)
            return;

        static constexpr double taus[3] = { 0.25, 1.0, 4.0 };
        double a;
        if (s.averaging >= 3)
        {
            livePowerFrames += 1.0;
            a = 1.0 / livePowerFrames;
        }
        else
        {
            a = 1.0 - std::exp (-(double) hop / (taus[juce::jlimit (0, 2, s.averaging)] * sampleRate));
            livePowerFrames = 0.0;
        }
        for (size_t k = 0; k < total.size(); ++k)
            livePower[k] += a * (total[k] - livePower[k]);
    }

    void Analyzer::rebuildSnapshot (bool full)
    {
        auto prev = getSnapshot();
        auto snap = std::make_shared<Snapshot> (*prev);

        Settings s;
        {
            const juce::SpinLock::ScopedLockType l (settingsLock);
            s = settings;
        }
        const auto target = targets::get (s.target, s.customLufs, s.customCeiling);
        snap->songMode = s.songMode;
        snap->bpm = tempoBpm.load();
        snap->beatsPerBar = tempoBeats.load();
        snap->droppedChunks = droppedChunks.load();

        // ---- Live readouts --------------------------------------------------------------------
        {
            const auto liveWindow = [this] (size_t count)
            {
                if (recentLive.size() < count)
                    return -150.0;
                double e = 0.0;
                long long n = 0;
                for (size_t i = recentLive.size() - count; i < recentLive.size(); ++i)
                {
                    e += recentLive[i].energy;
                    n += recentLive[i].n;
                }
                return n > 0 ? meanSquareToLufs (e / (double) n) : -150.0;
            };
            snap->momentary = (float) liveWindow (4);
            snap->shortTerm = (float) liveWindow (30);
            double sq = 0.0, tp = 0.0;
            long long n = 0;
            for (const auto& d : recentLive)
            {
                sq += d.sqL + d.sqR;
                n += d.n;
                tp = std::max (tp, (double) d.tp);
            }
            snap->rmsShort = n > 0 ? (float) powerToDb (sq / (2.0 * (double) n)) : -150.0f;
            snap->psr = snap->shortTerm > -100.0f ? toDb (tp) - snap->shortTerm : 0.0f;

            const auto corr = [this] (size_t b)
            {
                const auto d = std::sqrt (corrLL[b] * corrRR[b]);
                return d > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR[b] / d) : 1.0f;
            };
            snap->correlation = corr (0);
            for (size_t b = 0; b < 3; ++b)
                snap->bandCorrelation[b] = corr (b + 1);
            snap->widthDb = liveMid > 1.0e-12 ? (float) powerToDb (liveSide / liveMid) : -150.0f;

            if (! s.freeze)
            {
                snap->scope.resize (scopeRing.size());
                for (size_t i = 0; i < scopeRing.size(); ++i)
                    snap->scope[i] = scopeRing[(i + (size_t) scopeWrite) % scopeRing.size()];

                // Live spectrum in 1/3-octave band-power units (pink noise reads flat), 1/6-octave smoothing.
                const auto& hz = spectrumHz();
                snap->liveSpectrum.resize (hz.size());
                const auto binHz = sampleRate / fftSize;
                const auto thirdWidth = std::pow (2.0, 1.0 / 6.0) - std::pow (2.0, -1.0 / 6.0);
                for (size_t j = 0; j < hz.size(); ++j)
                {
                    const double f = hz[j];
                    auto lo = (int) std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz);
                    auto hi = (int) std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz);
                    lo = juce::jlimit (1, fftSize / 2, lo);
                    hi = juce::jlimit (lo, fftSize / 2, hi);
                    double sum = 0.0;
                    for (int k = lo; k <= hi; ++k)
                        sum += livePower[(size_t) k];
                    const auto mean = sum / (hi - lo + 1);
                    const auto binsPerThird = std::max (1.0, f * thirdWidth / binHz);
                    snap->liveSpectrum[j] = (float) powerToDb (mean * binsPerThird, -150.0);
                }
            }
        }

        if (! full)
        {
            const juce::SpinLock::ScopedLockType lock (snapshotLock);
            snapshot = std::move (snap);
            return;
        }

        // ---- Programme (active table) --------------------------------------------------------
        const auto& table = s.songMode ? songTable : liveTable;
        const auto count = table.slots.size();
        std::vector<SlotEnergy> se (count);
        double tpMax = 0.0, spMax = 0.0, sumL = 0.0, sumR = 0.0, kL = 0.0, kR = 0.0;
        long long nAll = 0;
        int clips = 0, overRuns = 0;
        double tpTime = -1.0, firstClip = -1.0;
        const double ceilingLin = std::pow (10.0, target.ceiling / 20.0);
        bool inOver = false;
        std::vector<double> overTimes;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& d = table.slots[i];
            se[i] = { d.energy, d.n };
            if (d.n <= 0)
            {
                inOver = false;
                continue;
            }
            if (d.tp > tpMax)
            {
                tpMax = d.tp;
                tpTime = (double) i * slotSeconds;
            }
            spMax = std::max (spMax, (double) d.sp);
            if (d.clips > 0 && firstClip < 0.0)
                firstClip = (double) i * slotSeconds;
            clips += d.clips;
            sumL += d.sumL;
            sumR += d.sumR;
            kL += d.kL;
            kR += d.kR;
            nAll += d.n;
            const bool over = d.tp > ceilingLin;
            if (over && ! inOver)
            {
                ++overRuns;
                if (overTimes.size() < 400)
                    overTimes.push_back ((double) i * slotSeconds);
            }
            inOver = over;
        }

        snap->loudness = computeLoudness (se.data(), count);
        snap->truePeakMax = tpMax > 0.0 ? toDb (tpMax) : -150.0f;
        snap->samplePeakMax = spMax > 0.0 ? toDb (spMax) : -150.0f;
        snap->clipRuns = clips;
        snap->overRuns = overRuns;
        snap->overTimes = overTimes;
        snap->dcLeftDb = nAll > 0 ? toDb (std::abs (sumL / (double) nAll)) : -150.0f;
        snap->dcRightDb = nAll > 0 ? toDb (std::abs (sumR / (double) nAll)) : -150.0f;
        snap->balanceDb = (kL > 0.0 && kR > 0.0) ? (float) (10.0 * std::log10 (kL / kR)) : 0.0f;
        snap->analysedSeconds = (double) nAll / sampleRate;
        snap->positionSeconds = s.songMode ? (double) (lastSongSlotWritten + 1) * slotSeconds : (double) count * slotSeconds;
        snap->plr = snap->loudness.hasIntegrated && tpMax > 0.0 ? snap->truePeakMax - (float) snap->loudness.integrated : 0.0f;

        // Per-slot momentary / short-term via prefix sums.
        std::vector<double> pe (count + 1, 0.0);
        std::vector<long long> pn (count + 1, 0);
        std::vector<int> pz (count + 1, 0);
        for (size_t i = 0; i < count; ++i)
        {
            pe[i + 1] = pe[i] + se[i].energy;
            pn[i + 1] = pn[i] + se[i].n;
            pz[i + 1] = pz[i] + (se[i].n > 0 ? 0 : 1);
        }
        const auto windowAt = [&] (size_t last, size_t w) -> double
        {
            if (last + 1 < w)
                return -150.0;
            const auto a = last + 1 - w, b = last + 1;
            if (pz[b] - pz[a] > 0 || pn[b] - pn[a] <= 0)
                return -150.0;
            return meanSquareToLufs ((pe[b] - pe[a]) / (double) (pn[b] - pn[a]));
        };

        // PSR median and short-term histogram (1 s steps).
        {
            std::vector<double> psrs;
            snap->stHistogram.fill (0);
            for (size_t last = 29; last < count; last += 10)
            {
                const auto st = windowAt (last, 30);
                if (st <= -70.0)
                    continue;
                const auto bin = (int) std::floor (st + 60.0);
                if (bin >= 0 && bin < 60)
                    ++snap->stHistogram[(size_t) bin];
                if (snap->loudness.hasIntegrated && st < snap->loudness.integrated - 20.0)
                    continue;
                float tp = 0.0f;
                for (size_t i = last - 29; i <= last; ++i)
                    tp = std::max (tp, table.slots[i].tp);
                if (tp > 0.0f)
                    psrs.push_back (toDb (tp) - st);
            }
            snap->medianPsr = (float) medianOf (psrs);
        }

        // History over the visible span.
        {
            static constexpr double spans[4] = { 30.0, 60.0, 180.0, 600.0 };
            const auto end = snap->positionSeconds;
            double t0 = 0.0, t1 = std::max (30.0, end);
            if (s.historySpan < 4)
            {
                const auto span = spans[juce::jlimit (0, 3, s.historySpan)];
                t1 = std::max (span, end);
                t0 = t1 - span;
            }
            snap->historyT0 = t0;
            snap->historyT1 = t1;
            constexpr int points = 800;
            snap->historyMomentary.assign ((size_t) points, -150.0f);
            snap->historyShortTerm.assign ((size_t) points, -150.0f);
            for (int j = 0; j < points; ++j)
            {
                const auto a = (size_t) std::max (0.0, std::floor ((t0 + (t1 - t0) * j / points) / slotSeconds));
                const auto b = (size_t) std::max ((double) a + 1, std::floor ((t0 + (t1 - t0) * (j + 1) / points) / slotSeconds));
                double m = -150.0, st = -150.0;
                for (size_t i = a; i < b && i < count; ++i)
                {
                    m = std::max (m, windowAt (i, 4));
                    st = std::max (st, windowAt (i, 30));
                }
                snap->historyMomentary[(size_t) j] = (float) m;
                snap->historyShortTerm[(size_t) j] = (float) st;
            }
        }

        // Spectrum, stereo and segments.
        const auto curve = curves::levels (s.curve);
        std::array<double, bands::count> mid {}, side {};
        std::array<double, 4> lr {}, ll {}, rr {};
        double midLow = 0.0, sideLow = 0.0, framesAll = 0.0;
        long long burstsAll = 0, segSamples = 0;
        std::vector<SegmentInfo> segInfos;

        for (size_t si = 0; si < table.segments.size(); ++si)
        {
            const auto& seg = table.segments[si];
            if (seg.samples <= 0)
                continue;
            for (size_t b = 0; b < (size_t) bands::count; ++b)
            {
                mid[b] += seg.mid[b];
                side[b] += seg.side[b];
            }
            framesAll += seg.frames;
            for (size_t b = 0; b < 4; ++b)
            {
                lr[b] += seg.lr[b];
                ll[b] += seg.ll[b];
                rr[b] += seg.rr[b];
            }
            midLow += seg.midLow;
            sideLow += seg.sideLow;
            burstsAll += seg.bursts;
            segSamples += seg.samples;

            SegmentInfo info;
            info.t0 = (double) si * slotsPerSegment * slotSeconds;
            info.t1 = info.t0 + slotsPerSegment * slotSeconds;
            double e = 0.0;
            long long n = 0;
            for (size_t i = si * slotsPerSegment; i < std::min (count, (si + 1) * slotsPerSegment); ++i)
            {
                e += table.slots[i].energy;
                n += table.slots[i].n;
            }
            info.loudness = n > 0 ? (float) meanSquareToLufs (e / (double) n) : -150.0f;
            if (seg.frames > 0)
            {
                bands::Levels lv {};
                for (size_t b = 0; b < (size_t) bands::count; ++b)
                    lv[b] = (float) powerToDb ((seg.mid[b] + seg.side[b]) / seg.frames, -150.0);
                const auto off = alignOffset (lv, curve);
                for (size_t b = 0; b < (size_t) bands::count; ++b)
                    info.deviationDb[b] = (float) (lv[b] - (curve[b] + off));
                info.spectrumValid = true;
            }
            info.sibilancePerMinute = (float) (seg.bursts / std::max (1.0e-3, (double) seg.samples / sampleRate / 60.0));
            for (size_t b = 0; b < 3; ++b)
            {
                const auto d = std::sqrt (seg.ll[b + 1] * seg.rr[b + 1]);
                info.bandCorrelation[b] = d > 1.0e-12 ? (float) (seg.lr[b + 1] / d) : 1.0f;
            }
            info.sideLowToMidLowDb = seg.midLow > 1.0e-12 ? (float) powerToDb (seg.sideLow / seg.midLow) : -150.0f;
            segInfos.push_back (info);
        }

        SpectralSummary summary;
        double totalPower = 0.0;
        for (size_t b = 0; b < (size_t) bands::count; ++b)
            totalPower += mid[b] + side[b];
        snap->ltasValid = framesAll >= 16.0 && totalPower > 1.0e-12 * framesAll;
        if (snap->ltasValid)
        {
            for (size_t b = 0; b < (size_t) bands::count; ++b)
            {
                summary.mid[b] = mid[b] / framesAll;
                summary.side[b] = side[b] / framesAll;
                snap->ltasDb[b] = (float) powerToDb (summary.mid[b] + summary.side[b], -150.0);
            }
            const auto off = alignOffset (snap->ltasDb, curve);
            for (size_t b = 0; b < (size_t) bands::count; ++b)
            {
                snap->curveDb[b] = (float) (curve[b] + off);
                snap->deviationDb[b] = snap->ltasDb[b] - snap->curveDb[b];
            }
            summary.deviationDb = snap->deviationDb;
            summary.truePeakDb = snap->truePeakMax;
            summary.valid = true;
        }

        const auto corrOf = [&] (size_t b)
        {
            const auto d = std::sqrt (ll[b] * rr[b]);
            return d > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, lr[b] / d) : 1.0f;
        };
        snap->correlationLong = corrOf (0);
        for (size_t b = 0; b < 3; ++b)
            snap->bandCorrelationLong[b] = corrOf (b + 1);
        {
            double m = 0.0, sd = 0.0;
            for (size_t b = 0; b < (size_t) bands::count; ++b)
            {
                m += summary.mid[b];
                sd += summary.side[b];
            }
            snap->sideToMidDb = m > 1.0e-15 ? (float) powerToDb (sd / m) : -150.0f;
        }
        snap->sideLowToMidLowDb = midLow > 1.0e-12 ? (float) powerToDb (sideLow / midLow) : -150.0f;
        snap->monoLossDb = summary.valid ? (float) monoSumLossDb (summary) : 0.0f;

        snap->translationValid = summary.valid;
        for (int p = 0; p < profiles::count; ++p)
            snap->translation[(size_t) p] = summary.valid ? scoreTranslation (profiles::get (p), summary) : TranslationResult();

        InsightInput in;
        in.integrated = (float) snap->loudness.integrated;
        in.hasIntegrated = snap->loudness.hasIntegrated;
        in.lra = (float) snap->loudness.lra;
        in.hasLra = snap->loudness.hasLra;
        in.truePeakMax = snap->truePeakMax;
        in.truePeakTime = tpTime;
        in.overRuns = overRuns;
        in.samplePeakMax = snap->samplePeakMax;
        in.clipRuns = clips;
        in.firstClipTime = firstClip;
        in.plr = snap->plr;
        in.medianPsr = snap->medianPsr;
        in.dcLeftDb = snap->dcLeftDb;
        in.dcRightDb = snap->dcRightDb;
        in.balanceDb = snap->balanceDb;
        in.correlation = snap->correlationLong;
        in.bandCorrelation = snap->bandCorrelationLong;
        in.sideToMidDb = snap->sideToMidDb;
        in.sideLowToMidLowDb = snap->sideLowToMidLowDb;
        in.monoLossDb = snap->monoLossDb;
        in.deviationDb = snap->deviationDb;
        in.spectrumValid = snap->ltasValid;
        in.sibilancePerMinute = segSamples > 0 ? (float) ((double) burstsAll / ((double) segSamples / sampleRate / 60.0)) : 0.0f;
        in.segments = std::move (segInfos);
        in.analysedSeconds = snap->analysedSeconds;
        in.target = target;
        in.sensitivity = s.sensitivity;
        in.translation = snap->translation;
        in.translationValid = snap->translationValid;
        in.bpm = snap->bpm;
        in.beatsPerBar = snap->beatsPerBar;
        in.songMode = s.songMode;
        snap->findings = findInsights (in);

        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        snapshot = std::move (snap);
    }

    // =============================================================================================
    // Persistence
    // =============================================================================================
    namespace
    {
        constexpr int formatVersion = 1;
        constexpr size_t maxLiveSaved = 12000; // last 20 minutes of the Live table

        void writeSlot (juce::OutputStream& out, const SlotData& d)
        {
            const auto n = std::max (1, d.n);
            out.writeInt (d.n);
            for (double v : { d.energy, d.kL, d.kR, d.sqL, d.sqR, d.sumL, d.sumR })
                out.writeFloat ((float) (v / n));
            out.writeFloat (d.tp);
            out.writeFloat (d.sp);
            out.writeShort ((short) juce::jlimit (0, 32767, d.clips));
        }

        SlotData readSlot (juce::InputStream& in)
        {
            SlotData d;
            d.n = std::max (0, in.readInt());
            double v[7];
            for (auto& x : v)
                x = (double) in.readFloat() * (double) d.n;
            d.energy = v[0]; d.kL = v[1]; d.kR = v[2]; d.sqL = v[3]; d.sqR = v[4]; d.sumL = v[5]; d.sumR = v[6];
            d.tp = in.readFloat();
            d.sp = in.readFloat();
            d.clips = in.readShort();
            return d;
        }

        void writeSegment (juce::OutputStream& out, const SegmentData& s)
        {
            for (auto v : s.mid) out.writeDouble (v);
            for (auto v : s.side) out.writeDouble (v);
            out.writeInt (s.frames);
            for (auto* arr : { &s.lr, &s.ll, &s.rr })
                for (auto v : *arr)
                    out.writeDouble (v);
            out.writeDouble (s.midLow);
            out.writeDouble (s.sideLow);
            out.writeInt (s.bursts);
            out.writeInt64 (s.samples);
        }

        SegmentData readSegment (juce::InputStream& in)
        {
            SegmentData s;
            for (auto& v : s.mid) v = in.readDouble();
            for (auto& v : s.side) v = in.readDouble();
            s.frames = in.readInt();
            for (auto* arr : { &s.lr, &s.ll, &s.rr })
                for (auto& v : *arr)
                    v = in.readDouble();
            s.midLow = in.readDouble();
            s.sideLow = in.readDouble();
            s.bursts = in.readInt();
            s.samples = in.readInt64();
            return s;
        }
    }

    juce::MemoryBlock Analyzer::saveAnalysis() const
    {
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream raw (block, false);
            juce::GZIPCompressorOutputStream out (raw, 6);
            const juce::ScopedLock lock (tableLock);
            out.writeInt (formatVersion);
            out.writeDouble (sampleRate);

            out.writeInt64 ((juce::int64) songTable.slots.size());
            for (const auto& d : songTable.slots)
                writeSlot (out, d);
            out.writeInt64 ((juce::int64) songTable.segments.size());
            for (const auto& sgm : songTable.segments)
                writeSegment (out, sgm);

            // Live table: keep whole segments only, so trimming never splits one.
            size_t first = 0;
            if (liveTable.slots.size() > maxLiveSaved)
                first = ((liveTable.slots.size() - maxLiveSaved) / slotsPerSegment + 1) * slotsPerSegment;
            out.writeInt64 ((juce::int64) (liveTable.slots.size() - std::min (first, liveTable.slots.size())));
            for (size_t i = first; i < liveTable.slots.size(); ++i)
                writeSlot (out, liveTable.slots[i]);
            const auto firstSeg = first / slotsPerSegment;
            out.writeInt64 ((juce::int64) (liveTable.segments.size() - std::min (firstSeg, liveTable.segments.size())));
            for (size_t i = firstSeg; i < liveTable.segments.size(); ++i)
                writeSegment (out, liveTable.segments[i]);
            out.writeInt64 (lastSongSlotWritten);
            out.flush();
        }
        return block;
    }

    void Analyzer::loadAnalysis (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream raw (block, false);
        juce::GZIPDecompressorInputStream in (raw);
        if (in.readInt() != formatVersion)
            return;
        in.readDouble();

        Table song, live;
        const auto readTable = [&in] (Table& t)
        {
            const auto slots = in.readInt64();
            if (slots < 0 || (size_t) slots > maxSlots)
                return false;
            t.slots.reserve ((size_t) slots);
            for (juce::int64 i = 0; i < slots && ! in.isExhausted(); ++i)
                t.slots.push_back (readSlot (in));
            const auto segs = in.readInt64();
            if (segs < 0 || (size_t) segs > maxSlots / slotsPerSegment)
                return false;
            for (juce::int64 i = 0; i < segs && ! in.isExhausted(); ++i)
                t.segments.push_back (readSegment (in));
            return true;
        };
        if (! readTable (song) || ! readTable (live))
            return;
        const auto lastSong = in.readInt64();

        const juce::ScopedLock lock (tableLock);
        songTable = std::move (song);
        liveTable = std::move (live);
        lastSongSlotWritten = lastSong;
        liveBase = -1;
        pendingLiveCount = (juce::int64) liveTable.slots.size();
        settingsChanged.store (true);
    }
}
