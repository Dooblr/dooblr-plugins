#include "Phrase.h"
#include "../Math.h"

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        std::vector<Segment> withConsonantDur (const std::vector<Segment>& segs, const BodyMix& body, double k)
        {
            std::vector<Segment> out;
            out.reserve (segs.size());
            for (const auto& c : segs)
            {
                Segment s = c;
                const auto d = phoneme (c.ipa, body);
                s.dur = (d ? d->dur : 0.07) * k;
                out.push_back (std::move (s));
            }
            return out;
        }

        std::vector<Segment> tailOf (const Syllable& syl, const BodyMix& body, double hz)
        {
            std::vector<Segment> out;
            if (auto parts = expandDiphthong (syl.nucleus.ipa); parts && parts[1])
            {
                Segment s;
                s.ipa = parts[1];
                s.dur = 0.07;
                s.stress = 1;
                s.f0 = hz;
                out.push_back (std::move (s));
            }
            auto coda = withConsonantDur (syl.coda, body, 0.7);
            for (auto& c : coda) c.f0 = hz;
            out.insert (out.end(), coda.begin(), coda.end());
            return out;
        }

        std::string ipaJoin (const Syllable& syl)
        {
            std::string s;
            auto add = [&] (const Segment& p) {
                if (! s.empty()) s += " ";
                s += p.ipa;
            };
            for (const auto& p : syl.onset) add (p);
            add (syl.nucleus);
            for (const auto& p : syl.coda) add (p);
            return s;
        }

        SyllablePlan planOne (const Syllable& syl, const BodyMix& body)
        {
            SyllablePlan sp;
            sp.text = syl.text;
            sp.nucleus = syl.nucleus.ipa;
            sp.readout = ipaJoin (syl);
            sp.wordEnd = syl.wordEnd;
            sp.phraseEnd = syl.phraseEnd;
            sp.melisma = syl.melisma;

            const double hz = kSpeakF0Ref;
            std::vector<Segment> holdSegs = withConsonantDur (syl.onset, body, 0.7);
            Segment nuc;
            auto parts = expandDiphthong (syl.nucleus.ipa);
            nuc.ipa = parts ? parts[0] : syl.nucleus.ipa;
            nuc.dur = kHoldVowel;
            nuc.f0 = hz;
            nuc.stress = 1;
            holdSegs.push_back (std::move (nuc));

            PlanOptions ho;
            ho.body = body;
            ho.t0 = kLiveT0;
            ho.f0 = hz;
            ho.intonation = Intonation::Flat;
            ho.openEnd = true;
            ho.sung = false;
            auto hold = planUtterance (holdSegs, ho);
            sp.hold = std::move (hold.gestures);
            sp.holdSegments = std::move (hold.segments);
            sp.holdEnd = hold.end;

            auto codaSegs = tailOf (syl, body, hz);
            if (! codaSegs.empty())
            {
                PlanOptions co = ho;
                co.openEnd = false;
                auto coda = planUtterance (codaSegs, co);
                sp.coda = std::move (coda.gestures);
                sp.codaSegments = std::move (coda.segments);
                sp.codaEnd = coda.end;
            }
            return sp;
        }
    }

    std::string segmentAt (const std::vector<PlannedSegment>& segs, double t)
    {
        for (const auto& s : segs)
            if (t >= s.t0 && t < s.t1) return s.ipa;
        if (! segs.empty() && t >= segs.back().t1) return segs.back().ipa;
        return {};
    }

    std::unique_ptr<PublishedPhrase> buildPhrase (const EnglishG2P& g2p, std::string_view text, const BodyMix& body)
    {
        auto out = std::make_unique<PublishedPhrase>();
        out->text = std::string (text);
        if (text.empty())
            return out;

        initInventory();
        out->syllables = lyricsToSyllables (text, g2p);
        out->plans.reserve (out->syllables.size());
        for (const auto& syl : out->syllables)
            out->plans.push_back (planOne (syl, body));

        const auto phrases = g2p.toPhrases (text);
        double t = 0;
        for (const auto& ph : phrases)
        {
            PlanOptions o;
            o.body = body;
            o.t0 = t;
            o.f0 = kSpeakF0Ref;
            o.intonation = ph.intonation;
            o.rate = 1.0;
            auto plan = planUtterance (ph.segments, o);
            out->speak.gestures.insert (out->speak.gestures.end(), plan.gestures.begin(), plan.gestures.end());
            out->speak.segments.insert (out->speak.segments.end(), plan.segments.begin(), plan.segments.end());
            t = plan.end + ph.pause;
            out->speak.end = plan.end;
        }
        out->speak.f0Ref = kSpeakF0Ref;
        return out;
    }
}
