#include "Insights.h"

#include <algorithm>
#include <cmath>

namespace dooblrmeter::dsp
{
    namespace
    {
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

        juce::String db (double v, int decimals = 1)
        {
            auto s = juce::String (std::abs (v), decimals);
            return (v < 0.0 && s.getDoubleValue() != 0.0 ? minus : juce::String()) + s;
        }

        juce::String signedDb (double v) { return (v > 0.0 ? "+" : "") + db (v); }

        juce::String hz (double f)
        {
            if (f >= 1000.0)
                return juce::String (f / 1000.0, (std::abs (f / 1000.0 - std::round (f / 1000.0)) < 0.05) ? 0 : 1) + " kHz";
            return juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String hzRange (double lo, double hi)
        {
            auto loText = hz (lo), hiText = hz (hi);
            if (lo >= 1000.0 && hi >= 1000.0)
                loText = loText.upToFirstOccurrenceOf (" ", false, false);
            else if (lo < 1000.0 && hi < 1000.0)
                loText = loText.upToFirstOccurrenceOf (" ", false, false);
            return loText + dash + hiText;
        }

        struct Region
        {
            const char* key;
            const char* name;
            double lo, hi;
            bool excess;        // true: too much; false: too little
            double threshold;   // dB beyond the curve at Standard sensitivity
            const char* why;
            const char* fix;
        };

        const Region regions[] {
            { "rumble", "Sub rumble", 20.0, 35.0, true, 6.0,
              "inaudible on most systems but it eats headroom and makes limiters work",
              "High-pass the mix bus or the sub sources around 25-30 Hz" },
            { "boom", "Boomy low end", 56.0, 140.0, true, 3.0,
              "it will swell on car and club systems and mask the kick",
              "Tame 60-120 Hz with a low shelf or a dynamic cut on the bass" },
            { "thin", "Thin low end", 56.0, 140.0, false, 4.0,
              "the mix will sound small on full-range systems",
              "Add body to the kick and bass around 60-120 Hz, or check a high-pass set too high" },
            { "mud", "Muddiness", 180.0, 500.0, true, 3.0,
              "it clouds the mix and hides detail on every system",
              "Cut 2-3 dB around 250-400 Hz on pads, guitars or the bus" },
            { "harsh", "Harshness", 2240.0, 5600.0, true, 3.0,
              "earbuds and phones exaggerate this range; it fatigues at volume",
              "Use a 2-3 dB dynamic cut around 3 kHz on the bus or the brightest source" },
            { "dull", "Dull top end", 8900.0, 18000.0, false, 5.0,
              "it will sound dull next to other releases",
              "Lift the air band with a gentle high shelf from 10 kHz" },
        };

        double regionDeviation (const bands::Levels& dev, double lo, double hi)
        {
            double sum = 0.0;
            int count = 0;
            for (int b = 0; b < bands::count; ++b)
                if (bands::centre (b) >= lo && bands::centre (b) <= hi)
                {
                    sum += dev[(size_t) b];
                    ++count;
                }
            return count > 0 ? sum / count : 0.0;
        }

        double scale (int sensitivity) { return sensitivity <= 0 ? 1.5 : (sensitivity >= 2 ? 0.7 : 1.0); }
    }

    juce::String formatTime (double s)
    {
        if (s < 0.0)
            return {};
        const auto total = (int) std::floor (s);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    juce::String formatSpan (double t0, double t1, double bpm, double beatsPerBar, bool songMode)
    {
        if (t0 < 0.0)
            return "whole programme";
        const auto times = t1 > t0 + 0.5 ? formatTime (t0) + dash + formatTime (t1) : formatTime (t0);
        if (songMode && bpm > 0.0 && beatsPerBar > 0.0)
        {
            const auto barOf = [&] (double t) { return 1 + (int) std::floor (t * bpm / 60.0 / beatsPerBar + 1.0e-6); };
            const auto b0 = barOf (t0), b1 = std::max (b0, barOf (std::max (t0, t1 - 0.001)));
            return (b1 > b0 ? "bars " + juce::String (b0) + dash + juce::String (b1) : "bar " + juce::String (b0)) + " (" + times + ")";
        }
        return times;
    }

    juce::String severityName (Finding::Severity s)
    {
        switch (s)
        {
            case Finding::Severity::fix:   return "FIX";
            case Finding::Severity::watch: return "WATCH";
            case Finding::Severity::info:  return "INFO";
        }
        return {};
    }

    std::vector<Finding> findInsights (const InsightInput& in)
    {
        std::vector<Finding> out;
        const auto k = scale (in.sensitivity);
        const auto span = [&] (double a, double b) { return formatSpan (a, b, in.bpm, in.beatsPerBar, in.songMode); };
        const bool enough = in.analysedSeconds >= 8.0;
        const juce::String targetName = juce::String (in.target.label).replace ("-", minus);

        // --- Peaks and clipping ------------------------------------------------------------
        if (in.truePeakMax > in.target.ceiling && in.truePeakMax > -100.0f)
        {
            Finding f;
            f.key = "truepeak";
            f.severity = in.truePeakMax > 0.0f ? Finding::Severity::fix : Finding::Severity::watch;
            f.title = in.truePeakMax > 0.0f ? "Inter-sample overs" : "True peak above the ceiling";
            f.detail = "True peak reaches " + signedDb (in.truePeakMax) + " dBTP (ceiling " + db (in.target.ceiling) + " dBTP for " + targetName
                     + "), crossing it in " + juce::String (std::max (1, in.overRuns)) + (in.overRuns == 1 ? " place" : " places")
                     + "; worst at " + formatTime (in.truePeakTime) + ". Lossy encodes and cheap converters will clip.";
            f.fix = "Set the limiter ceiling to " + db (in.target.ceiling) + " dBTP with true-peak detection, or lower its input by "
                  + juce::String (in.truePeakMax - in.target.ceiling, 1) + " dB.";
            f.t0 = in.truePeakTime;
            f.t1 = in.truePeakTime;
            out.push_back (f);
        }

        if (in.clipRuns > 0)
        {
            Finding f;
            f.key = "clip";
            f.severity = Finding::Severity::fix;
            f.title = "Clipped samples";
            f.detail = juce::String (in.clipRuns) + (in.clipRuns == 1 ? " run" : " runs") + " of 3 or more consecutive full-scale samples, first at "
                     + formatTime (in.firstClipTime) + ". Something before the meter is hard-clipping.";
            f.fix = "Find the stage that hits 0 dBFS (often the limiter output or a clipper) and lower its gain.";
            f.t0 = in.firstClipTime;
            f.t1 = in.firstClipTime;
            out.push_back (f);
        }

        // --- Loudness against the target ---------------------------------------------------
        if (in.hasIntegrated && enough && in.target.policy != DeliveryTarget::Policy::none)
        {
            const auto diff = in.integrated - in.target.lufs;
            const auto gain = targets::predictedGainDb (in.target, in.integrated, in.truePeakMax);
            if (in.target.policy == DeliveryTarget::Policy::both && std::abs (diff) > 1.0f)
            {
                Finding f;
                f.key = "loudness";
                f.severity = Finding::Severity::fix;
                f.title = std::abs (diff) > 0 && diff > 0 ? "Louder than the delivery spec" : "Quieter than the delivery spec";
                f.detail = "Integrated " + db (in.integrated) + " LUFS against " + db (in.target.lufs) + " LUFS for " + targetName + " ("
                         + signedDb (diff) + " LU). A delivery spec is usually checked to " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + "1 LU.";
                f.fix = diff > 0 ? "Lower the master by " + juce::String (diff, 1) + " dB (with the limiter after the gain change)."
                                 : "Raise the master by " + juce::String (-diff, 1) + " dB, watching the true-peak ceiling.";
                out.push_back (f);
            }
            else if (diff > 1.0f)
            {
                Finding f;
                f.key = "loudness";
                f.severity = Finding::Severity::watch;
                f.title = "Louder than " + targetName;
                f.detail = "Integrated " + db (in.integrated) + " LUFS is " + juce::String (diff, 1) + " dB above " + targetName
                         + "; playback turns it down " + juce::String (-gain, 1) + " dB, so limiting beyond that buys no loudness.";
                f.fix = "Back off the limiter until PLR is 8 dB or more, unless this level is intended for club play.";
                out.push_back (f);
            }
            else if (diff < -2.0f)
            {
                Finding f;
                f.key = "loudness";
                f.severity = Finding::Severity::info;
                f.title = "Quieter than " + targetName;
                f.detail = "Integrated " + db (in.integrated) + " LUFS is " + juce::String (-diff, 1) + " dB under " + targetName + "; playback "
                         + (gain > 0.05f ? "raises it only " + juce::String (gain, 1) + " dB." : "will not raise it.");
                f.fix = "Raise the master or the limiter drive if you want it to sit level with other releases.";
                out.push_back (f);
            }
        }

        // --- Dynamics ----------------------------------------------------------------------
        if (in.hasIntegrated && enough && in.plr > 0.0f && in.plr < 7.0f / (float) std::sqrt (k))
        {
            Finding f;
            f.key = "limited";
            f.severity = in.plr < 5.0f ? Finding::Severity::fix : Finding::Severity::watch;
            f.title = "Heavily limited";
            f.detail = "PLR " + juce::String (in.plr, 1) + " dB, median PSR " + juce::String (in.medianPsr, 1)
                     + " dB: transients are flattened and the mix will sound smaller after loudness normalisation.";
            f.fix = "Reduce limiting by 2-3 dB; most targets turn loud masters down anyway.";
            out.push_back (f);
        }

        if (in.hasLra && in.analysedSeconds >= 30.0 && in.lra > 15.0f * (float) k)
        {
            Finding f;
            f.key = "lra";
            f.severity = Finding::Severity::info;
            f.title = "Wide loudness range";
            f.detail = "LRA " + juce::String (in.lra, 1) + " LU: quiet passages may be lost on phones, in cars and in noisy places.";
            f.fix = "If that is not intended, ride the quiet sections up or compress the bus gently.";
            out.push_back (f);
        }

        // --- Tonal balance (whole programme, located by segments) --------------------------
        if (in.spectrumValid && enough)
        {
            for (const auto& r : regions)
            {
                const auto dev = regionDeviation (in.deviationDb, r.lo, r.hi);
                const auto threshold = r.threshold * k;
                const bool hit = r.excess ? dev > threshold : dev < -threshold;
                if (! hit)
                    continue;

                // Locate: the longest run of loud-enough segments over the threshold.
                double bestT0 = -1.0, bestT1 = -1.0, bestDev = dev, runT0 = -1.0, runMax = 0.0;
                double loudest = -150.0;
                for (const auto& s : in.segments)
                    loudest = std::max (loudest, (double) s.loudness);
                double runLen = 0.0, bestLen = 0.0;
                bool inLoudest = false;
                for (size_t i = 0; i <= in.segments.size(); ++i)
                {
                    bool flagged = false;
                    double d = 0.0;
                    if (i < in.segments.size())
                    {
                        const auto& s = in.segments[i];
                        if (s.spectrumValid && s.loudness > in.integrated - 10.0f)
                        {
                            d = regionDeviation (s.deviationDb, r.lo, r.hi);
                            flagged = r.excess ? d > threshold : d < -threshold;
                        }
                    }
                    if (flagged)
                    {
                        if (runT0 < 0.0)
                        {
                            runT0 = in.segments[i].t0;
                            runMax = d;
                            runLen = 0.0;
                        }
                        runLen += in.segments[i].t1 - in.segments[i].t0;
                        runMax = r.excess ? std::max (runMax, d) : std::min (runMax, d);
                    }
                    else if (runT0 >= 0.0)
                    {
                        if (runLen > bestLen)
                        {
                            bestLen = runLen;
                            bestT0 = runT0;
                            bestT1 = in.segments[i - 1].t1;
                            bestDev = runMax;
                        }
                        runT0 = -1.0;
                    }
                }
                if (bestT0 >= 0.0)
                    for (const auto& s : in.segments)
                        if (s.t0 >= bestT0 && s.t1 <= bestT1 && s.loudness >= loudest - 1.5)
                            inLoudest = true;

                const bool wholeSong = bestT0 < 0.0 || bestLen >= 0.8 * in.analysedSeconds;
                Finding f;
                f.key = r.key;
                f.severity = std::abs (bestDev) > 2.0 * threshold ? Finding::Severity::fix : Finding::Severity::watch;
                f.title = juce::String (r.name) + " " + hzRange (r.lo, r.hi);
                f.detail = hzRange (r.lo, r.hi) + " is " + juce::String (std::abs (wholeSong ? dev : bestDev), 1) + " dB "
                         + (r.excess ? "above" : "below") + " the reference curve "
                         + (wholeSong ? juce::String ("across the programme") : "in " + span (bestT0, bestT1) + (inLoudest ? ", the loudest section" : ""))
                         + "; " + r.why + ".";
                f.fix = juce::String (r.fix).replace ("-", dash) + ".";
                f.t0 = wholeSong ? -1.0 : bestT0;
                f.t1 = wholeSong ? -1.0 : bestT1;
                f.fLo = (float) r.lo;
                f.fHi = (float) r.hi;
                out.push_back (f);
            }

            // Narrow build-ups: one band well above both neighbours (and above the curve).
            for (int b = 2; b < bands::count - 2; ++b)
            {
                const auto d = in.deviationDb[(size_t) b];
                const auto side = std::max (in.deviationDb[(size_t) b - 1], in.deviationDb[(size_t) b + 1]);
                if (d > 3.0f * (float) k && d - side > 4.0f * (float) k && bands::centre (b) > 45.0 && bands::centre (b) < 12000.0)
                {
                    Finding f;
                    f.key = "resonance";
                    f.severity = Finding::Severity::watch;
                    f.title = "Build-up around " + hz (bands::centre (b));
                    f.detail = "The " + hz (bands::centre (b)) + " band is " + juce::String (d, 1) + " dB over the curve and "
                             + juce::String (d - side, 1) + " dB over its neighbours: a ringing note, resonance or one loud element.";
                    f.fix = "Sweep a narrow cut (Q 4-8) around " + hz (bands::centre (b)) + " on the source that rings.";
                    f.fix = f.fix.replace ("-", dash);
                    f.fLo = (float) bands::lowerEdge (b);
                    f.fHi = (float) bands::upperEdge (b);
                    out.push_back (f);
                }
            }
        }

        if (in.sibilancePerMinute > 20.0f * (float) k && enough)
        {
            Finding f;
            f.key = "sibilance";
            f.severity = in.sibilancePerMinute > 45.0f * (float) k ? Finding::Severity::watch : Finding::Severity::info;
            f.title = "Sibilant bursts 5" + dash + "10 kHz";
            f.detail = juce::String (juce::roundToInt (in.sibilancePerMinute)) + " sharp bursts per minute jump 6 dB or more over the 5" + dash
                     + "10 kHz average (esses, hats or clicks); earbuds make them sharper.";
            f.fix = "De-ess the vocal around 6" + dash + "8 kHz, or soften the hats.";
            f.fLo = 5000.0f;
            f.fHi = 10000.0f;
            out.push_back (f);
        }

        // --- Stereo, phase, mono -----------------------------------------------------------
        if (enough)
        {
            static const char* bandNames[3] = { "the low end (below 120 Hz)", "the mids (120 Hz to 2 kHz)", "the highs (above 2 kHz)" };
            for (int b = 0; b < 3; ++b)
            {
                if (in.bandCorrelation[(size_t) b] < 0.0f)
                {
                    Finding f;
                    f.key = "phase" + juce::String (b);
                    f.severity = Finding::Severity::fix;
                    f.title = "Out-of-phase " + juce::String (b == 0 ? "low end" : (b == 1 ? "mids" : "highs"));
                    f.detail = "Correlation in " + juce::String (bandNames[b]) + " averages " + juce::String (in.bandCorrelation[(size_t) b], 2)
                             + ": left and right cancel when summed, so it thins out or vanishes in mono.";
                    f.fix = b == 0 ? juce::String ("Check polarity of stereo bass / kick layers and mono the low end below 120 Hz.")
                                   : juce::String ("Check for a polarity flip, a stereo widener or a short delay between left and right.");
                    f.fLo = b == 0 ? 20.0f : (b == 1 ? 120.0f : 2000.0f);
                    f.fHi = b == 0 ? 120.0f : (b == 1 ? 2000.0f : 20000.0f);
                    out.push_back (f);
                }
            }

            if (in.monoLossDb < -1.5f * (float) k)
            {
                Finding f;
                f.key = "mono";
                f.severity = in.monoLossDb < -3.0f * (float) k ? Finding::Severity::fix : Finding::Severity::watch;
                f.title = "Loses level in mono";
                f.detail = "Summing to mono drops the loudness " + juce::String (-in.monoLossDb, 1)
                         + " dB; phones, Bluetooth speakers and many club systems play mono.";
                f.fix = "Narrow wide pads and effects, and check stereo wideners on central elements.";
                out.push_back (f);
            }

            if (in.sideLowToMidLowDb > -12.0f / (float) k && in.sideLowToMidLowDb > -100.0f)
            {
                Finding f;
                f.key = "widelow";
                f.severity = Finding::Severity::watch;
                f.title = "Stereo low end";
                f.detail = "Side energy below 120 Hz is only " + db (-in.sideLowToMidLowDb) + " dB under the mid; club systems sum the subs and the bass will shift or cancel.";
                f.detail = f.detail.replace (minus + minus, "");
                f.fix = "Mono the bass below 100" + dash + "120 Hz (a utility or mid/side EQ on the bus).";
                f.fLo = 20.0f;
                f.fHi = 120.0f;
                out.push_back (f);
            }

            if (std::abs (in.balanceDb) > 1.5f * (float) k)
            {
                Finding f;
                f.key = "balance";
                f.severity = Finding::Severity::watch;
                f.title = "Leans " + juce::String (in.balanceDb > 0 ? "left" : "right");
                f.detail = "The " + juce::String (in.balanceDb > 0 ? "left" : "right") + " channel is " + juce::String (std::abs (in.balanceDb), 1)
                         + " dB louder over the programme.";
                f.fix = "Check panning of the lead elements and any unbalanced stereo effect returns.";
                out.push_back (f);
            }

            if (in.sideToMidDb > 0.0f)
            {
                Finding f;
                f.key = "width";
                f.severity = Finding::Severity::watch;
                f.title = "Wider than centre";
                f.detail = "Side energy is " + signedDb (in.sideToMidDb) + " dB against the mid: the centre is weak and headphones sound hollow.";
                f.fix = "Bring the key elements to the centre or reduce stereo widening.";
                out.push_back (f);
            }

            const auto dc = std::max (in.dcLeftDb, in.dcRightDb);
            if (dc > -60.0f)
            {
                Finding f;
                f.key = "dc";
                f.severity = dc > -40.0f ? Finding::Severity::fix : Finding::Severity::watch;
                f.title = "DC offset";
                f.detail = "A constant offset of " + db (dc) + " dBFS on the " + juce::String (in.dcLeftDb >= in.dcRightDb ? "left" : "right")
                         + " channel wastes headroom and clicks at edits.";
                f.fix = "Insert a DC filter (or a 5 Hz high-pass) on the source or the bus.";
                out.push_back (f);
            }
        }

        // --- Translation -------------------------------------------------------------------
        if (in.translationValid && enough)
        {
            int worst = -1;
            for (int p = 0; p < profiles::count; ++p)
                if (worst < 0 || in.translation[(size_t) p].score < in.translation[(size_t) worst].score)
                    worst = p;
            if (worst >= 0 && in.translation[(size_t) worst].score < 50)
            {
                const auto& t = in.translation[(size_t) worst];
                Finding f;
                f.key = "translation";
                f.severity = Finding::Severity::watch;
                f.title = juce::String (profiles::get (worst).name) + " translation " + juce::String (t.score) + "/100";
                f.detail = t.reasons.isEmpty() ? juce::String() : t.reasons[0] + ".";
                f.fix = "Audition it with Listen set to " + juce::String (profiles::get (worst).shortName) + " and fix the top reason first.";
                out.push_back (f);
            }
        }

        std::stable_sort (out.begin(), out.end(), [] (const Finding& a, const Finding& b)
        {
            if (a.severity != b.severity)
                return (int) a.severity < (int) b.severity;
            return a.t0 < b.t0;
        });
        return out;
    }
}
