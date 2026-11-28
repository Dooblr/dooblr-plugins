#include "Translation.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "KWeighting.h"
#include "Targets.h"

namespace dooblrmeter::dsp
{
    namespace
    {
        struct Penalty
        {
            double points;
            juce::String reason;
        };

        juce::String hzText (double hz)
        {
            if (hz >= 1000.0)
                return juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz";
            return juce::String (juce::roundToInt (hz)) + " Hz";
        }

        double kWeightDb (double hz)
        {
            static const KWeighting k = [] { KWeighting w; w.prepare (48000.0); return w; }();
            return k.magnitudeDb (hz, 48000.0);
        }

        double sideKeep (const Profile& p, double hz)
        {
            switch (p.stereo)
            {
                case Profile::Stereo::mono:    return 0.0;
                case Profile::Stereo::partial: return hz < p.monoBelowHz ? 0.0 : p.sideGain * p.sideGain;
                case Profile::Stereo::full:    return p.sideGain * p.sideGain;
            }
            return 1.0;
        }
    }

    double monoSumLossDb (const SpectralSummary& s) noexcept
    {
        double m = 0.0, total = 0.0;
        for (int b = 0; b < bands::count; ++b)
        {
            const auto w = std::pow (10.0, kWeightDb (bands::centre (b)) / 10.0);
            m += w * s.mid[(size_t) b];
            total += w * (s.mid[(size_t) b] + s.side[(size_t) b]);
        }
        return total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-12, m) / total) : 0.0;
    }

    TranslationResult scoreTranslation (const Profile& p, const SpectralSummary& s)
    {
        TranslationResult result;
        if (! s.valid)
            return result;

        constexpr int n = bands::count;
        std::array<double, n> h2 {}, original {}, delivered {};
        double totalOriginal = 0.0, totalDelivered = 0.0;

        for (int b = 0; b < n; ++b)
        {
            const auto hz = bands::centre (b);
            h2[(size_t) b] = std::pow (10.0, profiles::responseDb (p, hz) / 10.0);
            original[(size_t) b] = s.mid[(size_t) b] + s.side[(size_t) b];
            delivered[(size_t) b] = h2[(size_t) b] * (s.mid[(size_t) b] + sideKeep (p, hz) * s.side[(size_t) b]);
            totalOriginal += original[(size_t) b];
            totalDelivered += delivered[(size_t) b];
        }

        if (totalOriginal <= 0.0 || totalDelivered <= 0.0)
        {
            result.score = 0;
            result.reasons.add ("Nothing of this mix is reproduced by this system");
            return result;
        }

        // Audibility: the programme plays at the profile's level; a band is lost when it falls
        // under the hearing threshold or the background noise, or the system cuts it by 15 dB+.
        std::array<bool, n> lost {};
        std::array<double, n> margin {};
        for (int b = 0; b < n; ++b)
        {
            const auto hz = bands::centre (b);
            const auto spl = p.splDb + powerToDb (delivered[(size_t) b] / totalDelivered);
            margin[(size_t) b] = spl - std::max (hearingThresholdDb (hz), profiles::noiseDb (p, hz));
            lost[(size_t) b] = margin[(size_t) b] < 0.0 || 10.0 * std::log10 (std::max (1.0e-15, h2[(size_t) b])) < -15.0;
        }

        std::vector<Penalty> penalties;

        // 1. Energy lost (K-weighted share of the whole mix) and the low-end share.
        {
            double lostW = 0.0, allW = 0.0, lowLost = 0.0, lowAll = 0.0;
            double cutHz = 0.0;
            for (int b = 0; b < n; ++b)
            {
                const auto hz = bands::centre (b);
                const auto w = std::pow (10.0, kWeightDb (hz) / 10.0) * original[(size_t) b];
                allW += w;
                if (lost[(size_t) b])
                    lostW += w;
                if (hz < 250.0)
                {
                    lowAll += original[(size_t) b];
                    if (lost[(size_t) b])
                    {
                        lowLost += original[(size_t) b];
                        cutHz = std::max (cutHz, bands::upperEdge (b));
                    }
                }
            }
            const auto share = allW > 0.0 ? lostW / allW : 0.0;
            result.lowEndLostPercent = lowAll > 0.0 ? (float) (100.0 * lowLost / lowAll) : 0.0f;
            const auto pts = 30.0 * share + 8.0 * (lowAll > 0.0 ? lowLost / lowAll : 0.0) * std::min (1.0, 4.0 * lowAll / totalOriginal);
            if (pts >= 1.0)
            {
                juce::String text;
                if (cutHz > 0.0 && result.lowEndLostPercent >= 5.0f)
                    text = "Energy below " + hzText (cutHz) + " vanishes here; " + juce::String (juce::roundToInt (result.lowEndLostPercent))
                         + "% of the low end is lost";
                else
                    text = juce::String (juce::roundToInt (100.0 * share)) + "% of the mix's loudness falls outside what this system plays";
                penalties.push_back ({ pts, text });
            }
        }

        // 2. Element survival: kick and bass carried by 100-300 Hz harmonics when 40-100 Hz is lost.
        {
            double fund = 0.0, harmDelivered = 0.0;
            bool fundLost = true;
            for (int b = 0; b < n; ++b)
            {
                const auto hz = bands::centre (b);
                if (hz >= 40.0 && hz < 100.0)
                {
                    fund += original[(size_t) b];
                    fundLost = fundLost && lost[(size_t) b];
                }
                else if (hz >= 100.0 && hz < 315.0)
                {
                    if (! lost[(size_t) b])
                        harmDelivered += original[(size_t) b];
                }
            }
            if (fundLost && fund > 0.15 * totalOriginal)
            {
                const auto ratio = powerToDb (std::max (1.0e-15, harmDelivered) / fund);
                const auto pts = juce::jlimit (0.0, 25.0, (-ratio - 3.0) * 1.5);
                if (pts >= 1.0)
                    penalties.push_back ({ pts, "Kick and bass live below 100 Hz with little 100" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                                                 + "300 Hz harmonic content, so they disappear here" });
            }
        }

        // 3. Balance against the curve, weighted by what this system plays and emphasises.
        {
            double weighted = 0.0, weights = 0.0, worst = 0.0;
            int worstBand = -1;
            for (int b = 0; b < n; ++b)
            {
                if (lost[(size_t) b])
                    continue;
                const auto emphasis = std::sqrt (h2[(size_t) b]);
                const auto excess = std::max (0.0, std::abs ((double) s.deviationDb[(size_t) b]) - curves::tolerance);
                weighted += emphasis * excess;
                weights += emphasis;
                if (emphasis * excess > worst)
                {
                    worst = emphasis * excess;
                    worstBand = b;
                }
            }
            if (weights > 0.0 && worstBand >= 0)
            {
                const auto pts = std::min (25.0, 4.0 * weighted / weights);
                if (pts >= 1.0)
                {
                    const auto dev = s.deviationDb[(size_t) worstBand];
                    auto emphasised = h2[(size_t) worstBand] > 1.25 ? " and this system emphasises it" : "";
                    penalties.push_back ({ pts, juce::String (bands::nominal (worstBand)) + (bands::centre (worstBand) >= 1000.0 ? "Hz" : " Hz")
                                                    + " sits " + juce::String (std::abs (dev), 1) + " dB " + (dev > 0 ? "over" : "under")
                                                    + " the curve" + emphasised });
                }
            }
        }

        // 4. Mono summing.
        if (p.stereo != Profile::Stereo::full)
        {
            double withSide = 0.0, summed = 0.0, lowS = 0.0, lowM = 0.0;
            for (int b = 0; b < n; ++b)
            {
                const auto hz = bands::centre (b);
                if (lost[(size_t) b])
                    continue;
                const auto keep = sideKeep (p, hz);
                withSide += h2[(size_t) b] * (s.mid[(size_t) b] + s.side[(size_t) b]);
                summed += h2[(size_t) b] * (s.mid[(size_t) b] + keep * s.side[(size_t) b]);
                if (hz < 150.0 && keep == 0.0)
                {
                    lowS += s.side[(size_t) b];
                    lowM += s.mid[(size_t) b];
                }
            }
            if (withSide > 0.0)
            {
                const auto loss = 10.0 * std::log10 (std::max (1.0e-12, summed) / withSide);
                result.monoLossDb = (float) loss;
                const auto pts = juce::jlimit (0.0, 20.0, (-loss - 1.0) * 8.0);
                if (pts >= 1.0)
                {
                    juce::String text = juce::String (p.stereo == Profile::Stereo::mono ? "Summed to mono" : "Summed to mono below " + hzText (p.monoBelowHz))
                                      + ": " + juce::String (-loss, 1) + " dB of level cancels";
                    if (lowM > 0.0 && lowS > 0.25 * lowM)
                        text << " (wide or out-of-phase low end)";
                    penalties.push_back ({ pts, text });
                }
            }
        }

        // 5. Bass limiter stress on small speakers.
        if (p.limiter)
        {
            double low = 0.0;
            for (int b = 0; b < n; ++b)
                if (bands::centre (b) < 200.0)
                    low += delivered[(size_t) b];
            const auto shareDb = powerToDb (low / totalDelivered);
            const auto pts = juce::jlimit (0.0, 12.0, (shareDb + 8.0) * 2.0);
            if (pts >= 1.0)
                penalties.push_back ({ pts, "Bass within its range is strong; the speaker's limiter will pump about "
                                                + juce::String (std::max (1.0, shareDb + 8.0), 0) + " dB" });
        }

        // 6. Vocal / lead intelligibility against background noise.
        if (p.noise == 1 || p.noise == 2 || p.noise == 3)
        {
            double sum = 0.0;
            int count = 0;
            for (int b = 0; b < n; ++b)
            {
                const auto hz = bands::centre (b);
                if (hz >= 1000.0 && hz <= 4000.0)
                {
                    sum += s.deviationDb[(size_t) b];
                    ++count;
                }
            }
            const auto dev = count > 0 ? sum / count : 0.0;
            const auto pts = juce::jlimit (0.0, 10.0, (-dev - 3.0) * 3.0);
            if (pts >= 1.0)
                penalties.push_back ({ pts, juce::String (juce::CharPointer_UTF8 ("1\xe2\x80\x93" "4 kHz sits ")) + juce::String (-dev, 1)
                                                + " dB under the curve; vocals and leads get lost in the background noise" });
        }

        // 7. Clipping risk after lossy encoding / device gain.
        if (s.truePeakDb > -1.0f)
        {
            const auto pts = p.lossyChain ? 6.0 : 3.0;
            penalties.push_back ({ pts, "True peak " + juce::String (s.truePeakDb, 1) + " dBTP: "
                                            + (p.lossyChain ? "lossy wireless encoding will clip" : "may clip after conversion") });
        }

        // 8. Width on headphones / earbuds.
        if (p.stereo == Profile::Stereo::full && p.noise != 2)
        {
            double m = 0.0, sd = 0.0;
            for (int b = 0; b < n; ++b)
            {
                m += s.mid[(size_t) b];
                sd += s.side[(size_t) b];
            }
            const auto sm = powerToDb (sd / std::max (1.0e-15, m));
            const auto pts = juce::jlimit (0.0, 12.0, (sm + 3.0) * 3.0);
            if (pts >= 1.0)
                penalties.push_back ({ pts, "Very wide (side " + juce::String (sm, 1) + " dB vs mid): elements sit outside the head and fatigue" });
        }

        std::sort (penalties.begin(), penalties.end(), [] (const Penalty& a, const Penalty& b) { return a.points > b.points; });
        double total = 0.0;
        for (const auto& pen : penalties)
        {
            total += pen.points;
            if (pen.points >= 2.0 && result.reasons.size() < 3)
                result.reasons.add (pen.reason);
        }
        result.score = juce::jlimit (0, 100, (int) std::lround (100.0 - total));
        if (result.reasons.isEmpty())
            result.reasons.add ("Translates well: nothing significant is lost");
        return result;
    }
}
