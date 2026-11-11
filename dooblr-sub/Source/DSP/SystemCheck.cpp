#include "SystemCheck.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

#include <juce_audio_basics/juce_audio_basics.h>

namespace dooblrsub::dsp
{
    Thresholds thresholdsFor (Target target) noexcept
    {
        Thresholds t;
        switch (target)
        {
            case Target::festival:
                t.infraRatio = 0.12f;
                t.infraRatioWarn = 0.06f;
                t.minCorrelation = 0.55f;
                t.minCorrelationWarn = 0.80f;
                t.clipPeak = 0.94f;
                t.clipPeakWarn = 0.82f;
                t.cancelFail = -0.35f;
                t.limiterCeiling = 0.80f;
                t.infraHzBias = 4.0f;
                t.sustainedFailDb = -6.0f;
                break;
            case Target::home:
                t.infraRatio = 0.40f;
                t.infraRatioWarn = 0.22f;
                t.minCorrelation = 0.15f;
                t.minCorrelationWarn = 0.45f;
                t.clipPeak = 0.995f;
                t.clipPeakWarn = 0.94f;
                t.cancelFail = -0.60f;
                t.limiterCeiling = 0.98f;
                t.infraHzBias = -4.0f;
                t.sustainedFailDb = -1.0f;
                break;
            case Target::club:
            default:
                break;
        }
        return t;
    }

    const char* verdictLabel (Verdict v) noexcept
    {
        switch (v)
        {
            case Verdict::works: return "Works on big subs";
            case Verdict::check: return "Check";
            case Verdict::wont:  return "Won't translate";
        }
        return "Check";
    }

    Verdict evaluate (const Analysis& a, Target target, ReasonList& reasons) noexcept
    {
        reasons = {};
        const auto th = thresholdsFor (target);
        int fails = 0, warns = 0;

        const auto sub = std::max (1.0e-12f, a.subRms);
        const auto infraRatio = a.infraRms / sub;
        if (infraRatio > th.infraRatio || a.bandRms[0] > 0.65f * sub)
        {
            reasons.add ("Too much energy below 30 Hz");
            ++fails;
        }
        else if (infraRatio > th.infraRatioWarn)
        {
            reasons.add ("Too much energy below 30 Hz");
            ++warns;
        }

        if (a.lrCorrelation < th.minCorrelation || a.sideRms > 0.45f * sub)
        {
            reasons.add ("Stereo low end below 120 Hz");
            ++fails;
        }
        else if (a.lrCorrelation < th.minCorrelationWarn && a.sideRms > 0.12f * sub)
        {
            reasons.add ("Stereo low end below 120 Hz");
            ++warns;
        }

        if (a.headroomPeak >= th.clipPeak || a.subPeak >= th.clipPeak)
        {
            reasons.add ("Sub peaks clipping headroom");
            ++fails;
        }
        else if (a.headroomPeak >= th.clipPeakWarn || a.subPeak >= th.clipPeakWarn)
        {
            reasons.add ("Sub peaks clipping headroom");
            ++warns;
        }

        if (a.cancellation < th.cancelFail)
        {
            reasons.add ("Sub cancels fundamental");
            ++fails;
        }
        else if (a.cancellation < th.cancelWarn)
        {
            reasons.add ("Sub cancels fundamental");
            ++warns;
        }

        if (std::abs (a.dc) > th.dcFail)
        {
            reasons.add ("DC offset");
            ++fails;
        }

        if (a.pitchHz > 1.0f && a.pitchStd / a.pitchHz > th.pitchRelStdFail)
        {
            reasons.add ("Unstable sub pitch");
            ++warns;
        }

        if (a.sustainedDb > th.sustainedFailDb)
        {
            reasons.add ("Excessive sustained low end");
            ++fails;
        }

        if (fails > 0) return Verdict::wont;
        if (warns > 0) return Verdict::check;
        return Verdict::works;
    }

    namespace
    {
        float rmsOf (const float* x, int n) noexcept
        {
            if (n <= 0) return 0.0f;
            double acc = 0.0;
            for (int i = 0; i < n; ++i) acc += (double) x[i] * (double) x[i];
            return (float) std::sqrt (acc / (double) n);
        }

        float meanOf (const float* x, int n) noexcept
        {
            if (n <= 0) return 0.0f;
            double acc = 0.0;
            for (int i = 0; i < n; ++i) acc += (double) x[i];
            return (float) (acc / (double) n);
        }

        float peakOf (const float* x, int n) noexcept
        {
            float p = 0.0f;
            for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i]));
            return p;
        }

    }

    Analysis analyseBuffer (const float* left, const float* right, int n, double sr,
                            float subVsFundCorrelation, float trackedHz, float trackedStd) noexcept
    {
        Analysis a;
        if (n <= 0 || left == nullptr) return a;
        const auto* rch = right != nullptr ? right : left;

        std::vector<float> mid ((size_t) n), side ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            mid[(size_t) i] = 0.5f * (left[i] + rch[i]);
            side[(size_t) i] = 0.5f * (left[i] - rch[i]);
        }

        constexpr double twoPi = 6.283185307179586;
        auto rangeRms = [&] (const float* x, float lo, float hi) -> float
        {
            double acc = 0.0;
            int bins = 0;
            const auto k0 = std::max (0, (int) std::floor (lo * (double) n / sr));
            const auto k1 = std::min (n / 2, (int) std::ceil (hi * (double) n / sr));
            for (int k = k0; k < k1; ++k)
            {
                const auto w = twoPi * (double) k / (double) n;
                double re = 0.0, im = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    re += (double) x[i] * std::cos (w * (double) i);
                    im += (double) x[i] * std::sin (w * (double) i);
                }
                acc += re * re + im * im;
                ++bins;
            }
            if (bins == 0) return 0.0f;
            return (float) std::sqrt (2.0 * acc) / (float) n;
        };

        a.infraRms = rangeRms (mid.data(), 1.0f, 25.0f);
        a.subRms = rangeRms (mid.data(), 20.0f, 120.0f);
        a.subPeak = peakOf (mid.data(), n);
        a.headroomPeak = std::max (peakOf (left, n), peakOf (rch, n));
        a.crest = a.subRms > 1.0e-12f ? a.subPeak / a.subRms : 1.0f;
        a.peakToRms = a.crest;
        a.dc = 0.5f * (meanOf (left, n) + meanOf (rch, n));
        a.sideRms = rmsOf (side.data(), n);

        double eL = 0.0, eR = 0.0, eLR = 0.0;
        // Correlation of the raw channels (tests use 50 Hz throughout, so this is the
        // low-band correlation). For mixed-spectrum files the IIR path in SystemCheck
        // is the live meter.
        for (int i = 0; i < n; ++i)
        {
            eL += (double) left[i] * left[i];
            eR += (double) rch[i] * rch[i];
            eLR += (double) left[i] * rch[i];
        }
        const auto denom = std::sqrt (eL * eR);
        a.lrCorrelation = denom > 1.0e-20 ? (float) (eLR / denom) : 1.0f;

        for (int b = 0; b < numSubBands; ++b)
            a.bandRms[(size_t) b] = rangeRms (mid.data(), subBandLoHz[b], subBandHiHz[b]);

        a.cancellation = juce::jlimit (-1.0f, 1.0f, subVsFundCorrelation);
        a.pitchHz = trackedHz;
        a.pitchStd = trackedStd;
        a.sustainedDb = a.subRms > 1.0e-12f ? juce::Decibels::gainToDecibels (a.subRms) : -120.0f;
        return a;
    }

    void SystemCheck::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse > 1.0 ? sampleRateToUse : 44100.0;
        specRing.assign (2048, 0.0f);
        reset();
    }

    void SystemCheck::reset() noexcept
    {
        infraL = infraR = lowL = lowR = dcL = dcR = {};
        for (auto& b : band) b[0] = b[1] = {};
        std::fill (std::begin (accBand), std::end (accBand), 0.0);
        accSub = accPeak = accMid = accSide = accDc = accInfra = 0.0;
        accLowL = accLowR = accLowLR = accGen = accFund = accGenFund = 0.0;
        accN = hop = pitchWrite = pitchFilled = specWrite = 0;
        pitchHist.fill (0.0f);
        published = {};
        spectrum.fill (0.0f);
    }

    void SystemCheck::tickFilters (float l, float r, float genSub, float origLow) noexcept
    {
        const auto twoPi = 6.28318530718f;
        const auto infraA = 1.0f - std::exp (-twoPi * 25.0f / (float) sampleRate);
        const auto lowA = 1.0f - std::exp (-twoPi * 120.0f / (float) sampleRate);
        const auto dcA = 1.0f - std::exp (-twoPi * 2.0f / (float) sampleRate);

        const auto iL = infraL.process (l, infraA);
        const auto iR = infraR.process (r, infraA);
        const auto loL = lowL.process (l, lowA);
        const auto loR = lowR.process (r, lowA);
        dcL.process (l, dcA);
        dcR.process (r, dcA);

        const auto mid = 0.5f * (l + r);
        const auto side = 0.5f * (l - r);
        accMid += (double) mid * (double) mid;
        accSide += (double) side * (double) side;
        accDc += 0.5 * ((double) l + (double) r);
        accInfra += 0.5 * ((double) iL * iL + (double) iR * iR);
        accLowL += (double) loL * loL;
        accLowR += (double) loR * loR;
        accLowLR += (double) loL * loR;
        accSub += 0.5 * ((double) loL * loL + (double) loR * loR);
        accPeak = std::max (accPeak, (double) std::max (std::abs (l), std::abs (r)));
        accGen += (double) genSub * genSub;
        accFund += (double) origLow * origLow;
        accGenFund += (double) genSub * origLow;

        for (int b = 0; b < numSubBands; ++b)
        {
            const auto aHi = 1.0f - std::exp (-twoPi * subBandHiHz[b] / (float) sampleRate);
            const auto aLo = 1.0f - std::exp (-twoPi * subBandLoHz[b] / (float) sampleRate);
            const auto hi = band[b][0].process (mid, aHi);
            const auto lo = band[b][1].process (mid, aLo);
            const auto bp = hi - lo;
            accBand[b] += (double) bp * bp;
        }
        ++accN;
    }

    void SystemCheck::publish() noexcept
    {
        if (accN < 32) return;
        Analysis a;
        const auto inv = 1.0 / (double) accN;
        a.subRms = (float) std::sqrt (accSub * inv);
        a.subPeak = (float) accPeak;
        a.headroomPeak = (float) accPeak;
        a.crest = a.subRms > 1.0e-12f ? a.subPeak / a.subRms : 1.0f;
        a.peakToRms = a.crest;
        a.infraRms = (float) std::sqrt (accInfra * inv);
        a.dc = (float) (accDc * inv);
        a.sideRms = (float) std::sqrt (accSide * inv);
        const auto denom = std::sqrt (accLowL * accLowR);
        a.lrCorrelation = denom > 1.0e-20 ? (float) (accLowLR / denom) : 1.0f;
        for (int b = 0; b < numSubBands; ++b)
            a.bandRms[(size_t) b] = (float) std::sqrt (accBand[b] * inv);
        const auto gden = std::sqrt (accGen * accFund);
        a.cancellation = gden > 1.0e-20 ? (float) (accGenFund / gden) : 0.0f;
        a.sustainedDb = a.subRms > 1.0e-12f ? juce::Decibels::gainToDecibels (a.subRms) : -120.0f;

        if (pitchFilled > 2)
        {
            double mean = 0.0, n = 0.0;
            for (int i = 0; i < pitchFilled; ++i)
                if (pitchHist[(size_t) i] > 1.0f) { mean += (double) pitchHist[(size_t) i]; n += 1.0; }
            if (n > 1.0)
            {
                mean /= n;
                double var = 0.0;
                for (int i = 0; i < pitchFilled; ++i)
                    if (pitchHist[(size_t) i] > 1.0f)
                    {
                        const auto d = (double) pitchHist[(size_t) i] - mean;
                        var += d * d;
                    }
                a.pitchHz = (float) mean;
                a.pitchStd = (float) std::sqrt (var / n);
            }
        }

        published = a;
        accSub = accPeak = accMid = accSide = accDc = accInfra = 0.0;
        accLowL = accLowR = accLowLR = accGen = accFund = accGenFund = 0.0;
        std::fill (std::begin (accBand), std::end (accBand), 0.0);
        accN = 0;
        seq.fetch_add (1, std::memory_order_release);
    }

    void SystemCheck::process (const float* left, const float* right, int numSamples, int numChannels,
                               float generatedSub, float originalLow, float trackedHz) noexcept
    {
        if (left == nullptr || numSamples <= 0) return;
        const auto* r = (numChannels > 1 && right != nullptr) ? right : left;

        for (int i = 0; i < numSamples; ++i)
        {
            tickFilters (left[i], r[i], generatedSub, originalLow);
            if (! specRing.empty())
            {
                specRing[(size_t) specWrite] = 0.5f * (left[i] + r[i]);
                specWrite = (specWrite + 1) % (int) specRing.size();
            }
        }

        if (trackedHz > 1.0f)
        {
            pitchHist[(size_t) pitchWrite] = trackedHz;
            pitchWrite = (pitchWrite + 1) % (int) pitchHist.size();
            pitchFilled = std::min (pitchFilled + 1, (int) pitchHist.size());
        }

        hop += numSamples;
        const auto period = (int) std::max (256.0, sampleRate * 0.08);
        if (hop >= period)
        {
            hop = 0;
            publish();

            // Log-spaced 20-200 Hz magnitude from the ring, for the display.
            if (! specRing.empty())
            {
                const auto n = (int) specRing.size();
                for (int b = 0; b < spectrumBins; ++b)
                {
                    const auto hz = 20.0 * std::pow (10.0, (double) b / (spectrumBins - 1) * std::log10 (200.0 / 20.0));
                    const auto w = 6.28318530718 * hz / sampleRate;
                    double re = 0.0, im = 0.0;
                    for (int i = 0; i < n; i += 4)
                    {
                        const auto x = (double) specRing[(size_t) ((specWrite + i) % n)];
                        re += x * std::cos (w * (double) i);
                        im += x * std::sin (w * (double) i);
                    }
                    const auto mag = (float) (4.0 * std::sqrt (re * re + im * im) / (double) n);
                    spectrum[(size_t) b] += 0.35f * (mag - spectrum[(size_t) b]);
                }
            }
        }
    }

    Analysis SystemCheck::snapshot() const noexcept { return published; }

    Verdict SystemCheck::currentVerdict (ReasonList& reasons) const noexcept
    {
        return evaluate (published, target, reasons);
    }

    void SystemCheck::copySpectrum (float* dest, int n) const noexcept
    {
        const auto m = std::min (n, spectrumBins);
        for (int i = 0; i < m; ++i) dest[i] = spectrum[(size_t) i];
    }
}
