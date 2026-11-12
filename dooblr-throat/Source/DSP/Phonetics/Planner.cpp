#include "Planner.h"
#include "Coarticulation.h"
#include <sys/types.h>
#include "Durations.h"
#include "../Math.h"
#include <cmath>

namespace dooblrthroat::dsp::phonetics
{
    namespace
    {
        constexpr double PLACED_CLOSURE_DELAY = 0.025;
        constexpr double BREATH_SUNG_AT = 0.06;

        bool truthy (std::optional<double> x)
        {
            return x.has_value() && *x != 0.0 && ! std::isnan (*x);
        }

        struct Seg
        {
            std::string ipa;
            std::optional<double> dur;
            std::optional<uint8_t> stress;
            std::optional<double> f0;
            std::optional<PhonemeDef> def;
            std::optional<ArticPatch> vowel;
            bool glideFrom = false;
            bool wordEnd = false;
        };

        struct Span { double t0 = 0, t1 = 0; };

        int placeRank (std::string_view place)
        {
            if (place == "bilabial") return 0;
            if (place == "labiodental") return 1;
            if (place == "dental") return 2;
            if (place == "alveolar") return 3;
            if (place == "postalveolar") return 4;
            if (place == "retroflex") return 5;
            if (place == "palatal") return 6;
            if (place == "velar") return 7;
            if (place == "uvular") return 8;
            return -1;
        }

        bool closesInFront (std::string_view front, std::string_view back)
        {
            const auto f = placeRank (front), b = placeRank (back);
            return f >= 0 && b >= 0 && f < b;
        }

        bool prePlacedBehindClosure (const std::vector<Seg>& segs, size_t i)
        {
            if (i < 2 || ! segs[i].def || ! segs[i - 1].def) return false;
            const auto& def = *segs[i].def;
            const auto& prev = *segs[i - 1].def;
            if (def.manner != Manner::Stop || prev.manner != Manner::Stop
                || ! closesInFront (prev.place, def.place))
                return false;
            const auto& before = segs[i - 2];
            const ArticPatch* tg = nullptr;
            ArticPatch rhotic;
            if (before.def) tg = &before.def->targets;
            else if (before.vowel && before.vowel->get (ArticKey::TtCurl).value_or (0.0) > 0.05)
            {
                rhotic = *before.vowel;
                tg = &rhotic;
            }
            if (! tg) return false;
            for (size_t k = 0; k < def.nDominant; ++k)
                if (! tg->get (def.dominant[k])) return false;
            return true;
        }

        double pitchRamp (double dur)
        {
            return js_min (0.06, js_max (0.008, 0.5 * dur));
        }
    }

    void oralGesturesInto (const PhonemeDef& def, const ArticPatch& tg,
                           double a, double b, double speed, std::string_view label,
                           std::vector<Gesture>& out, bool holdClosed)
    {
        ArticPatch dom, sec;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            if (! tg.values[i]) continue;
            const auto k = ARTIC_ALL[i];
            if (def.isDominant (k)) dom.set (k, *tg.values[i]);
            else sec.set (k, *tg.values[i]);
        }
        auto isBody = [] (ArticKey k) {
            return k == ArticKey::TbX || k == ArticKey::TbY || k == ArticKey::Root;
        };
        bool bodyOnly = def.nDominant > 0;
        for (size_t i = 0; i < def.nDominant; ++i)
            if (! isBody (def.dominant[i])) bodyOnly = false;
        const auto sp = bodyOnly ? 1.0 + (speed - 1.0) * 0.35 : speed;
        const bool closing = def.manner == Manner::Stop || def.manner == Manner::Nasal || def.manner == Manner::Affricate;
        bool lipsOnly = def.nDominant > 0;
        for (size_t i = 0; i < def.nDominant; ++i)
            if (! (def.dominant[i] == ArticKey::LipAp || def.dominant[i] == ArticKey::Labiodental))
                lipsOnly = false;
        const auto lead = holdClosed ? 0.0 : (closing ? (bodyOnly ? 0.034 : lipsOnly ? 0.019 : 0.02) : 0.0);
        const auto early = holdClosed ? 0.012 : (closing ? (bodyOnly ? 0.024 : lipsOnly ? 0.022 : 0.031) : 0.008);
        const auto hold = holdClosed ? 0.08 : 0.045;
        if (! dom.is_empty())
            out.push_back (Gesture::make (a - early, js_max (a + 0.01, b - lead), 0.03, bodyOnly ? 0.03 : 0.018, 40.0, Targets::fromArtic (dom))
                               .withSpeed (sp).withReleaseHold (hold).withLabel (label));
        // Secondary keys (including context-coloured tongue body and lips) share
        // one gesture. The third-order tracker, not a linear ramp or a weak
        // isolated TbX, shapes the movement.
        if (! sec.is_empty())
            out.push_back (Gesture::make (a, b, 0.012, 0.012, 2.2, Targets::fromArtic (sec)).withLabel (label));
    }


    namespace
    {
        bool samePlaceFamily (std::string_view a, std::string_view b)
        {
            auto family = [] (std::string_view p) -> int {
                if (p == "bilabial" || p == "labiodental") return 1;
                if (p == "dental" || p == "alveolar" || p == "postalveolar" || p == "retroflex") return 2;
                if (p == "palatal" || p == "velar" || p == "uvular") return 3;
                return 0;
            };
            const auto fa = family (a), fb = family (b);
            return fa != 0 && fa == fb;
        }

        bool isOralObstruent (const PhonemeDef* d)
        {
            if (d == nullptr) return false;
            return ! d->nasal && (d->manner == Manner::Stop || d->manner == Manner::Fricative
                                  || d->manner == Manner::Affricate);
        }

        ArticPatch releaseOpening (const PhonemeDef& def)
        {
            ArticPatch p;
            const auto place = std::string_view (def.place);
            if (place == "bilabial" || place == "labiodental")
            {
                p.set (ArticKey::LipAp, 0.42);
                if (def.targets.get (ArticKey::Labiodental))
                    p.set (ArticKey::Labiodental, 0.15);
            }
            else if (place == "dental" || place == "alveolar" || place == "postalveolar" || place == "retroflex")
            {
                const auto y = def.targets.get (ArticKey::TtY).value_or (9.6);
                p.set (ArticKey::TtY, y - 1.25);
            }
            else
            {
                const auto y = def.targets.get (ArticKey::TbY).value_or (8.3);
                p.set (ArticKey::TbY, y - 1.35);
            }
            return p;
        }
    }

    Plan planUtterance (const std::vector<Segment>& input, const PlanOptions& o)
    {
        const auto rate = o.rate > 0.0 && std::isfinite (o.rate) ? o.rate : 1.0;
        const auto body = o.body;

        std::vector<SegInfo> infos (input.size());
        for (size_t i = 0; i < input.size(); ++i)
        {
            if (isVowelIpa (input[i].ipa, body))
            {
                infos[i].vowel = true;
                continue;
            }
            auto d = phoneme (input[i].ipa, body);
            infos[i].vowel = false;
            if (d)
            {
                infos[i].manner = d->manner;
                infos[i].voiced = d->voicing == Voicing::Voiced;
                infos[i].dur = d->dur;
            }
        }
        const auto durs = klattDurations (input, infos);

        std::vector<Seg> segs;
        segs.reserve (input.size() + 4);
        for (size_t si = 0; si < input.size(); ++si)
        {
            if (auto parts = expandDiphthong (input[si].ipa))
            {
                const auto base = durs[si];
                int nParts = parts[1] ? 2 : 1;
                for (int i = 0; i < nParts; ++i)
                {
                    Seg s;
                    s.ipa = parts[i];
                    s.dur = nParts > 1 ? base * (i == 0 ? 0.55 : 0.45) : base;
                    s.stress = input[si].stress;
                    s.f0 = input[si].f0;
                    s.glideFrom = i > 0;
                    s.wordEnd = input[si].wordEnd && i == nParts - 1;
                    segs.push_back (std::move (s));
                }
            }
            else
            {
                Seg s;
                s.ipa = input[si].ipa;
                s.dur = durs[si];
                s.stress = input[si].stress;
                s.f0 = input[si].f0;
                s.wordEnd = input[si].wordEnd;
                segs.push_back (std::move (s));
            }
        }
        for (auto& s : segs)
        {
            if (auto v = vowelArtic (s.ipa, body)) s.vowel = v;
            else s.def = phoneme (s.ipa, body);
        }

        std::vector<Gesture> gestures;
        std::vector<PlannedSegment> planned;
        auto t = o.t0 + 0.06;
        std::vector<Span> times;
        times.reserve (segs.size());
        for (const auto& s : segs)
        {
            double d;
            if (s.dur) d = *s.dur;
            else if (s.vowel)
                d = s.stress == 1 ? 0.17 : s.stress == 2 ? 0.13 : 0.095;
            else
                d = s.def ? s.def->dur : 0.08;
            d /= rate;
            times.push_back ({ t, t + d });
            t += d;
        }

        std::vector<size_t> vIdx;
        for (size_t i = 0; i < segs.size(); ++i)
            if (segs[i].vowel) vIdx.push_back (i);

        for (size_t n = 0; n < vIdx.size(); ++n)
        {
            const auto i = vIdx[n];
            // Anticipatory coarticulation (Birkholz 2007/2013): the upcoming
            // vowel target starts during the preceding consonant. Short ramps
            // so the third-order tracker, not linear interpolation, approaches it.
            auto vStart = times[i].t0;
            if (i > 0 && ! segs[i - 1].vowel)
            {
                const auto c0 = times[i - 1].t0, c1 = times[i - 1].t1;
                vStart = c0 + 0.45 * js_max (0.0, c1 - c0);
            }
            const auto rIn = 0.008;
            const auto rOut = 0.012;
            gestures.push_back (Gesture::make (vStart, times[i].t1, rIn, rOut, 1.0, Targets::fromArtic (*segs[i].vowel))
                                    .withLabel (segs[i].ipa));
        }

        for (size_t i = 0; i < segs.size(); ++i)
        {
            const auto t0 = times[i].t0, t1 = times[i].t1, dur = t1 - t0;
            PlannedSegment ps;
            ps.ipa = segs[i].ipa;
            ps.t0 = t0; ps.t1 = t1;
            if (segs[i].vowel) ps.stress = segs[i].stress.value_or (0);
            ps.kind = segs[i].vowel ? "vowel" : (segs[i].def ? mannerName (segs[i].def->manner) : "?");
            planned.push_back (std::move (ps));
            if (! segs[i].def) continue;
            const auto& def = *segs[i].def;
            const bool nextVowel = i + 1 < segs.size() && segs[i + 1].vowel.has_value();
            const Seg* prevSeg = i > 0 ? &segs[i - 1] : nullptr;
            const Seg* nextSeg = i + 1 < segs.size() ? &segs[i + 1] : nullptr;
            const ArticPatch* neighbor = nullptr;
            for (size_t j = i + 1; j < segs.size() && neighbor == nullptr; ++j)
                if (segs[j].vowel) neighbor = &*segs[j].vowel;
            if (neighbor == nullptr)
            {
                for (size_t j = i; j-- > 0 && neighbor == nullptr; )
                    if (segs[j].vowel) neighbor = &*segs[j].vowel;
            }
            if (neighbor == nullptr && o.contextVowel)
                neighbor = &*o.contextVowel;
            const auto ctx = contextConsonantTarget (def, neighbor, body);
            auto addOral = [&] (const ArticPatch& tg, double a, double b, double speed, std::string_view label, bool holdClosed = false)
            {
                oralGesturesInto (def, tg, a, b, speed, label, gestures, holdClosed);
            };
            const bool nextHomorganicStop = nextSeg && nextSeg->def
                && (nextSeg->def->manner == Manner::Stop || nextSeg->def->manner == Manner::Affricate)
                && samePlaceFamily (def.place, nextSeg->def->place);
            const bool holdIntoNext = def.manner == Manner::Nasal && nextHomorganicStop;
            const double oralEnd = holdIntoNext ? times[i + 1].t1 : t1;
            switch (def.manner)
            {
                case Manner::Stop:
                    if (prePlacedBehindClosure (segs, i))
                        addOral (ctx, t0 + js_min (PLACED_CLOSURE_DELAY, js_max (0.0, dur - 0.03)), t1, 1.55, def.ipa);
                    else
                        addOral (ctx, t0, t1, 1.55, def.ipa);
                    break;
                case Manner::Nasal:
                    addOral (ctx, t0, oralEnd, 1.55, def.ipa, holdIntoNext);
                    break;
                case Manner::Tap:
                    addOral (ctx, t0, t1, 2.8, def.ipa);
                    break;
                case Manner::Trill:
                {
                    const auto nTr = js_max (2.0, std::round (dur * 26.0));
                    for (double k = 0; k < nTr; k += 1.0)
                    {
                        const auto a = t0 + (k / nTr) * dur;
                        addOral (ctx, a, a + 0.35 / 26.0, 3.2, def.ipa);
                    }
                    break;
                }
                case Manner::Affricate:
                {
                    const auto split = t0 + dur * 0.55;
                    addOral (ctx, t0, split, 2.0, def.ipa);
                    if (def.release)
                    {
                        if (auto rel = phoneme (def.release, body))
                        {
                            const auto relCtx = contextConsonantTarget (*rel, neighbor, body);
                            oralGesturesInto (*rel, relCtx, split + 0.008, t1, 1.4, def.ipa, gestures);
                        }
                    }
                    break;
                }
                case Manner::Fricative:
                case Manner::Approximant:
                case Manner::Lateral:
                    addOral (ctx, t0, t1, 1.3, def.ipa);
                    break;
                case Manner::Glottal:
                case Manner::Vowel:
                    break;
            }
            if (def.nasal)
            {
                // Anticipatory lowering through the preceding vowel; close at a following oral obstruent.
                auto velStart = t0 - 0.075;
                if (prevSeg && prevSeg->vowel)
                    velStart = js_min (velStart, times[i - 1].t0 + 0.35 * (times[i - 1].t1 - times[i - 1].t0));
                auto velEnd = t1 + 0.012;
                if (nextSeg && nextSeg->def && isOralObstruent (&*nextSeg->def))
                    velEnd = times[i + 1].t0;
                gestures.push_back (Gesture::make (velStart, velEnd, 0.045, 0.022, 5.0, Targets::one (ArticKey::Velum, 0.62))
                                        .withLabel ("velum"));
            }
            else if (prevSeg && prevSeg->def && prevSeg->def->nasal && isOralObstruent (&def))
            {
                // Oral consonant after a nasal: velum firmly closed for an oral burst.
                gestures.push_back (Gesture::make (t0 - 0.01, t1 + 0.02, 0.018, 0.03, 6.0, Targets::one (ArticKey::Velum, 0.0))
                                        .withLabel ("velum-close"));
            }
            if (def.glottal == GlottalKind::Open)
                gestures.push_back (Gesture::make (t0 - 0.01, t1, 0.03, 0.035, 5.0, Targets::one (SourceKey::Abduction, 0.33))
                                        .withLabel ("h"));
            else if (def.glottal == GlottalKind::Breathy)
                gestures.push_back (Gesture::make (t0, t1, 0.03, 0.03, 5.0, Targets::one (SourceKey::Abduction, 0.1))
                                        .withLabel ("ɦ"));
            else if (def.glottal == GlottalKind::Stop)
                gestures.push_back (Gesture::make (t0, t1, 0.012, 0.015, 8.0, Targets::one (SourceKey::Closure, 1.0))
                                        .withLabel ("ʔ"));
            else if (def.voicing == Voicing::Voiceless)
            {
                if (def.manner == Manner::Stop || def.manner == Manner::Affricate)
                {
                    const bool afterS = prevSeg && (prevSeg->ipa == "s" || prevSeg->ipa == "ʃ");
                    const bool aspirated = nextVowel && ! afterS;
                    const auto vot = (aspirated ? 0.05 : 0.012) / rate;
                    gestures.push_back (Gesture::make (t0 + 0.25 * dur, t1 + vot * 0.4, 0.035, aspirated ? 0.045 : 0.02, 6.0,
                                                       Targets::one (SourceKey::Abduction, aspirated ? 0.36 : 0.22))
                                            .withLabel ("aspiration"));
                }
                else
                    gestures.push_back (Gesture::make (t0 - 0.015, t1 + 0.008, 0.03, 0.03, 6.0, Targets::one (SourceKey::Abduction, 0.44))
                                            .withLabel ("voiceless"));
            }
            else if (def.manner == Manner::Fricative)
                gestures.push_back (Gesture::make (t0, t1, 0.03, 0.03, 3.0, Targets::one (SourceKey::Abduction, 0.035))
                                        .withLabel ("voiced frication"));

            // Word-final / pre-fricative stop release and burst.
            if (def.manner == Manner::Stop || def.manner == Manner::Affricate)
            {
                const bool nextFricative = nextSeg && nextSeg->def && nextSeg->def->manner == Manner::Fricative;
                const bool nextSameStop = nextHomorganicStop;
                const bool phraseFinal = (i + 1 == segs.size()) || segs[i].wordEnd;
                const bool needsRelease = ! nextVowel && ! nextSameStop;
                if (needsRelease)
                {
                    for (auto& g : gestures)
                        if (g.label.view() == def.ipa)
                            g.releaseHold = 0.0;
                    const auto burst = (phraseFinal && ! nextFricative) ? 0.05 : 0.024;
                    const auto openAt = js_max (t0 + 0.5 * dur, t1 - 0.012);
                    const auto open = releaseOpening (def);
                    if (! open.is_empty())
                        gestures.push_back (Gesture::make (openAt, openAt + burst, 0.005, 0.018, 10.0, Targets::fromArtic (open))
                                                .withSpeed (2.4).withLabel ("release"));
                    const auto abd = def.voicing == Voicing::Voiceless ? 0.48 : 0.18;
                    gestures.push_back (Gesture::make (openAt, openAt + burst + 0.016, 0.003, 0.02, 7.0,
                                                       Targets::one (SourceKey::Abduction, abd))
                                            .withLabel ("burst"));
                    t = js_max (t, openAt + burst + 0.03);
                }
            }
        }

        const bool openEnd = o.openEnd;
        const bool sung = o.sung;
        const auto breathAt = sung ? BREATH_SUNG_AT : 0.04;
        const auto end = js_max (t, times.empty() ? o.t0 + 0.06 : times.back().t1);
        gestures.push_back (Gesture::make (o.t0 + breathAt, end, 0.04, openEnd ? 0.07 : 0.05, 1.0, Targets::one (SourceKey::Breath, 1.0))
                                .withLabel ("breath"));
        if (! openEnd)
        {
            const bool sonorant = ! segs.empty() && (segs.back().vowel
                || (segs.back().def && (segs.back().def->manner == Manner::Nasal
                    || segs.back().def->manner == Manner::Approximant
                    || segs.back().def->manner == Manner::Lateral
                    || segs.back().def->manner == Manner::Tap
                    || segs.back().def->manner == Manner::Trill)));
            const auto at = end + (sonorant ? 0.045 : 0.015);
            gestures.push_back (Gesture::make (at, end + 0.1, 0.03, 0.06, 6.0, Targets::one (SourceKey::Abduction, 0.22))
                                    .withLabel ("exhale"));
        }

        const auto f0 = o.f0 > 0 && std::isfinite (o.f0) ? o.f0 : 110.0;
        const auto accent = o.accent;
        const auto total = js_max (0.2, end - o.t0);
        ssize_t nuclear = -1;
        for (auto i : vIdx)
            if (segs[i].stress == 1 && ! segs[i].glideFrom)
                nuclear = ssize_t (i);

        for (size_t n = 0; n < vIdx.size(); ++n)
        {
            const auto i = vIdx[n];
            const auto& s = segs[i];
            const auto t0 = times[i].t0, t1 = times[i].t1;
            const auto hold = n + 1 < vIdx.size() ? js_max (t1, times[vIdx[n + 1]].t0 - 0.03) : t1;
            const bool hasF0 = truthy (s.f0);
            double target;
            if (hasF0)
                target = *s.f0;
            else
            {
                const auto prog = (t0 - o.t0) / total;
                const auto decl = 1.0 - 0.18 * prog;
                const bool isNuclear = ssize_t (i) == nuclear;
                const auto acc = s.stress == 1 ? 1.0 + (isNuclear ? accent * 2.1 : accent * 1.15)
                                : s.stress == 2 ? 1.0 + accent * 0.55 : 1.0 - accent * 0.15;
                target = f0 * decl * acc;
                const bool last = n + 1 == vIdx.size();
                if (last && o.intonation == Intonation::Question)
                    target = f0 * 1.35;
                if (last && o.intonation == Intonation::Statement)
                {
                    const auto fall = f0 * 0.76;
                    if (t1 - t0 > 0.06)
                    {
                        gestures.push_back (Gesture::make (t0, t0 + 0.35 * (t1 - t0), 0.05, 0.05, 1.0, Targets::one (SourceKey::F0, target))
                                                .withLabel ("pitch"));
                        gestures.push_back (Gesture::make (t0 + 0.55 * (t1 - t0), t1, 0.06, 0.08, 1.0, Targets::one (SourceKey::F0, fall))
                                                .withLabel ("pitch"));
                        continue;
                    }
                    target = fall;
                }
            }
            const auto r = hasF0 && sung ? pitchRamp (t1 - t0) : 0.06;
            gestures.push_back (Gesture::make (t0, hasF0 ? t1 : hold, r, r, hasF0 ? 6.0 : 1.0, Targets::one (SourceKey::F0, target))
                                    .withLabel ("pitch"));
        }
        for (size_t i = 0; i < segs.size(); ++i)
        {
            if (sung && ! segs[i].vowel && truthy (segs[i].f0))
            {
                const auto t0 = times[i].t0, t1 = times[i].t1;
                const auto r = pitchRamp (t1 - t0);
                gestures.push_back (Gesture::make (t0, t1, r, r, 6.0, Targets::one (SourceKey::F0, *segs[i].f0))
                                        .withLabel ("pitch"));
            }
        }
                return Plan { std::move (gestures), std::move (planned), end };
    }
}
