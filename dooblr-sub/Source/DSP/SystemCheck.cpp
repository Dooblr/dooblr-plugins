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
                t.generatedMinHz = 28.0f;
                t.harmonicMin = 0.26f;
                t.subWeakRatio = 0.28f;
                t.subStrongRatio = 5.0f;
                t.mudPeakiness = 2.2f;
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
                t.generatedMinHz = 20.0f;
                t.harmonicMin = 0.08f;
                t.subWeakRatio = 0.12f;
                t.subStrongRatio = 9.0f;
                t.mudPeakiness = 3.2f;
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

    const char* checkReasonText (CheckId id) noexcept
    {
        switch (id)
        {
            case CheckId::infra:           return "Too much energy below 30 Hz";
            case CheckId::stereoLows:      return "Stereo low end below 120 Hz";
            case CheckId::clipping:        return "Sub peaks clipping headroom";
            case CheckId::cancellation:    return "Sub cancels fundamental";
            case CheckId::dcOffset:        return "DC offset";
            case CheckId::unstablePitch:   return "Unstable sub pitch";
            case CheckId::sustained:       return "Excessive sustained low end";
            case CheckId::subWeak:         return "Sub too weak against the mids";
            case CheckId::subStrong:       return "Sub too strong against the mids";
            case CheckId::mud:             return "Resonant buildup / mud";
            case CheckId::fundamentalLow:  return "Fundamental too low for big rigs";
            case CheckId::harmonicThin:    return "Missing harmonic presence";
            case CheckId::none: break;
            case CheckId::numChecks: break;
        }
        return "";
    }

    const char* checkFixComponentId (CheckId id) noexcept
    {
        switch (id)
        {
            case CheckId::infra:           return "fix_infra";
            case CheckId::stereoLows:      return "fix_stereoLows";
            case CheckId::clipping:        return "fix_clipping";
            case CheckId::cancellation:    return "fix_cancellation";
            case CheckId::dcOffset:        return "fix_dcOffset";
            case CheckId::unstablePitch:   return "fix_unstablePitch";
            case CheckId::sustained:       return "fix_sustained";
            case CheckId::subWeak:         return "fix_subWeak";
            case CheckId::subStrong:       return "fix_subStrong";
            case CheckId::mud:             return "fix_mud";
            case CheckId::fundamentalLow:  return "fix_fundamentalLow";
            case CheckId::harmonicThin:    return "fix_harmonicThin";
            case CheckId::none: break;
            case CheckId::numChecks: break;
        }
        return "fix_none";
    }

    const char* checkShortName (CheckId id) noexcept
    {
        switch (id)
        {
            case CheckId::infra:           return "infra";
            case CheckId::stereoLows:      return "stereo lows";
            case CheckId::clipping:        return "headroom";
            case CheckId::cancellation:    return "phase";
            case CheckId::dcOffset:        return "DC";
            case CheckId::unstablePitch:   return "tracking";
            case CheckId::sustained:       return "sustain";
            case CheckId::subWeak:         return "sub level";
            case CheckId::subStrong:       return "sub level";
            case CheckId::mud:             return "mud";
            case CheckId::fundamentalLow:  return "fundamental";
            case CheckId::harmonicThin:    return "harmonics";
            case CheckId::none: break;
            case CheckId::numChecks: break;
        }
        return "check";
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
            reasons.add (CheckId::infra, checkReasonText (CheckId::infra));
            ++fails;
        }
        else if (infraRatio > th.infraRatioWarn)
        {
            reasons.add (CheckId::infra, checkReasonText (CheckId::infra));
            ++warns;
        }

        if (a.lrCorrelation < th.minCorrelation || a.sideRms > 0.45f * sub)
        {
            reasons.add (CheckId::stereoLows, checkReasonText (CheckId::stereoLows));
            ++fails;
        }
        else if (a.lrCorrelation < th.minCorrelationWarn && a.sideRms > 0.12f * sub)
        {
            reasons.add (CheckId::stereoLows, checkReasonText (CheckId::stereoLows));
            ++warns;
        }

        if (a.headroomPeak >= th.clipPeak || a.subPeak >= th.clipPeak)
        {
            reasons.add (CheckId::clipping, checkReasonText (CheckId::clipping));
            ++fails;
        }
        else if (a.headroomPeak >= th.clipPeakWarn || a.subPeak >= th.clipPeakWarn)
        {
            reasons.add (CheckId::clipping, checkReasonText (CheckId::clipping));
            ++warns;
        }

        if (a.cancellation < th.cancelFail)
        {
            reasons.add (CheckId::cancellation, checkReasonText (CheckId::cancellation));
            ++fails;
        }
        else if (a.cancellation < th.cancelWarn)
        {
            reasons.add (CheckId::cancellation, checkReasonText (CheckId::cancellation));
            ++warns;
        }

        if (std::abs (a.dc) > th.dcFail)
        {
            reasons.add (CheckId::dcOffset, checkReasonText (CheckId::dcOffset));
            ++fails;
        }

        if (a.pitchHz > 1.0f && a.pitchStd / a.pitchHz > th.pitchRelStdFail)
        {
            reasons.add (CheckId::unstablePitch, checkReasonText (CheckId::unstablePitch));
            ++warns;
        }

        if (a.sustainedDb > th.sustainedFailDb)
        {
            reasons.add (CheckId::sustained, checkReasonText (CheckId::sustained));
            ++fails;
        }

        // Spectral balance vs mids: only when the mid band is actually present, so a
        // lone 50 Hz sine (the existing "works" fixture) is not flagged.
        if (a.midRms > 0.008f)
        {
            const auto ratio = a.subRms / a.midRms;
            if (ratio < th.subWeakRatio)
            {
                reasons.add (CheckId::subWeak, checkReasonText (CheckId::subWeak));
                ++warns;
            }
            else if (ratio > th.subStrongRatio && a.subRms > 0.12f)
            {
                reasons.add (CheckId::subStrong, checkReasonText (CheckId::subStrong));
                ++fails;
            }
        }

        const auto neighbour = std::max (a.bandRms[1], std::max (a.bandRms[2], 1.0e-8f));
        if (a.mudRms > 0.06f && a.mudRms > th.mudPeakiness * neighbour && a.mudRms > 0.30f * sub)
        {
            reasons.add (CheckId::mud, checkReasonText (CheckId::mud));
            ++warns;
        }

        if (a.generatedHz > 1.0f && a.generatedHz < th.generatedMinHz)
        {
            reasons.add (CheckId::fundamentalLow, checkReasonText (CheckId::fundamentalLow));
            ++warns;
        }

        if (a.pitchHz > 1.0f && a.pitchHz < 70.0f && a.subRms > 0.04f
            && a.harmonicRatio < th.harmonicMin)
        {
            reasons.add (CheckId::harmonicThin, checkReasonText (CheckId::harmonicThin));
            ++warns;
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
                            float subVsFundCorrelation, float trackedHz, float trackedStd,
                            float generatedHz) noexcept
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

        a.midRms = rangeRms (mid.data(), 200.0f, 800.0f);
        a.mudRms = rangeRms (mid.data(), 80.0f, 180.0f);
        a.presenceRms = rangeRms (mid.data(), 160.0f, 250.0f);
        const auto fundBand = rangeRms (mid.data(), 30.0f, 70.0f);
        const auto harmBand = rangeRms (mid.data(), 80.0f, 160.0f);
        a.harmonicRatio = fundBand > 1.0e-8f ? harmBand / fundBand : 0.0f;

        float mudPeak = a.mudRms, mudCentre = 90.0f;
        for (float hz = 70.0f; hz <= 180.0f; hz += 10.0f)
        {
            const auto e = rangeRms (mid.data(), hz - 8.0f, hz + 8.0f);
            if (e > mudPeak)
            {
                mudPeak = e;
                mudCentre = hz;
            }
        }
        a.mudHz = mudCentre;

        a.cancellation = juce::jlimit (-1.0f, 1.0f, subVsFundCorrelation);
        a.pitchHz = trackedHz;
        a.pitchStd = trackedStd;
        a.generatedHz = generatedHz;
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
        midLo = midHi = mudLo = mudHi = presLo = presHi = fundLo = fundHi = harmLo = harmHi = {};
        for (auto& b : band) b[0] = b[1] = {};
        std::fill (std::begin (accBand), std::end (accBand), 0.0);
        accSub = accPeak = accMid = accSide = accDc = accInfra = 0.0;
        accLowL = accLowR = accLowLR = accGen = accFund = accGenFund = 0.0;
        accMidBand = accMud = accPresence = accFundBand = accHarmBand = 0.0;
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
        const auto a200 = 1.0f - std::exp (-twoPi * 200.0f / (float) sampleRate);
        const auto a800 = 1.0f - std::exp (-twoPi * 800.0f / (float) sampleRate);
        const auto a80 = 1.0f - std::exp (-twoPi * 80.0f / (float) sampleRate);
        const auto a180 = 1.0f - std::exp (-twoPi * 180.0f / (float) sampleRate);
        const auto a160 = 1.0f - std::exp (-twoPi * 160.0f / (float) sampleRate);
        const auto a250 = 1.0f - std::exp (-twoPi * 250.0f / (float) sampleRate);
        const auto a30 = 1.0f - std::exp (-twoPi * 30.0f / (float) sampleRate);
        const auto a70 = 1.0f - std::exp (-twoPi * 70.0f / (float) sampleRate);

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

        const auto midHiZ = midHi.process (mid, a800);
        const auto midLoZ = midLo.process (mid, a200);
        const auto midBand = midHiZ - midLoZ;
        accMidBand += (double) midBand * midBand;

        const auto mudHiZ = mudHi.process (mid, a180);
        const auto mudLoZ = mudLo.process (mid, a80);
        const auto mudBand = mudHiZ - mudLoZ;
        accMud += (double) mudBand * mudBand;

        const auto presHiZ = presHi.process (mid, a250);
        const auto presLoZ = presLo.process (mid, a160);
        const auto presBand = presHiZ - presLoZ;
        accPresence += (double) presBand * presBand;

        const auto fundHiZ = fundHi.process (mid, a70);
        const auto fundLoZ = fundLo.process (mid, a30);
        const auto fundBand = fundHiZ - fundLoZ;
        accFundBand += (double) fundBand * fundBand;

        const auto harmHiZ = harmHi.process (mid, a160);
        const auto harmLoZ = harmLo.process (mid, a80);
        const auto harmBand = harmHiZ - harmLoZ;
        accHarmBand += (double) harmBand * harmBand;

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
        a.midRms = (float) std::sqrt (accMidBand * inv);
        a.mudRms = (float) std::sqrt (accMud * inv);
        a.presenceRms = (float) std::sqrt (accPresence * inv);
        const auto fundBand = (float) std::sqrt (accFundBand * inv);
        const auto harmBand = (float) std::sqrt (accHarmBand * inv);
        a.harmonicRatio = fundBand > 1.0e-8f ? harmBand / fundBand : 0.0f;
        a.generatedHz = generatedHz;

        float mudPeak = a.bandRms[3], mudCentre = 75.0f;
        for (int b = 2; b < numSubBands; ++b)
            if (a.bandRms[(size_t) b] >= mudPeak)
            {
                mudPeak = a.bandRms[(size_t) b];
                mudCentre = 0.5f * (subBandLoHz[b] + subBandHiHz[b]);
            }
        a.mudHz = mudCentre;

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
        accMidBand = accMud = accPresence = accFundBand = accHarmBand = 0.0;
        std::fill (std::begin (accBand), std::end (accBand), 0.0);
        accN = 0;
        seq.fetch_add (1, std::memory_order_release);
    }

    void SystemCheck::pushSample (float left, float right, float generatedSub, float originalLow) noexcept
    {
        tickFilters (left, right, generatedSub, originalLow);
        if (! specRing.empty())
        {
            specRing[(size_t) specWrite] = 0.5f * (left + right);
            specWrite = (specWrite + 1) % (int) specRing.size();
        }
    }

    void SystemCheck::finishBlock (int numSamples, float trackedHz) noexcept
    {
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

    void SystemCheck::process (const float* left, const float* right, int numSamples, int numChannels,
                               float generatedSub, float originalLow, float trackedHz) noexcept
    {
        if (left == nullptr || numSamples <= 0) return;
        const auto* r = (numChannels > 1 && right != nullptr) ? right : left;

        for (int i = 0; i < numSamples; ++i)
            pushSample (left[i], r[i], generatedSub, originalLow);

        finishBlock (numSamples, trackedHz);
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
