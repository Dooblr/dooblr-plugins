#include "Planner.h"
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
                           std::vector<Gesture>& out)
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
        const auto lead = closing ? (bodyOnly ? 0.034 : lipsOnly ? 0.019 : 0.02) : 0.0;
        const auto early = closing ? (bodyOnly ? 0.024 : lipsOnly ? 0.022 : 0.031) : 0.008;
        if (! dom.is_empty())
            out.push_back (Gesture::make (a - early, js_max (a + 0.01, b - lead), 0.03, bodyOnly ? 0.03 : 0.018, 40.0, Targets::fromArtic (dom))
                               .withSpeed (sp).withReleaseHold (0.045).withLabel (label));
        const bool alveolar = std::string_view (def.place) == "alveolar" && ! tg.get (ArticKey::TbX)
            && (def.manner == Manner::Stop || def.manner == Manner::Nasal || def.manner == Manner::Fricative
                || def.manner == Manner::Affricate || def.manner == Manner::Tap);
        if (alveolar)
            out.push_back (Gesture::make (a, b, 0.05, 0.05, 1.2,
                                          Targets::one (ArticKey::TbX, 3.2).set (ArticKey::TbY, 7.4))
                               .withLabel (label));
        if (! sec.is_empty())
        {
            auto place = bodyOnly ? sec.get (ArticKey::TbX) : std::optional<double>{};
            if (place) sec.values[artic_index (ArticKey::TbX)].reset();
            if (! sec.is_empty())
                out.push_back (Gesture::make (a, b, 0.05, 0.05, 2.2, Targets::fromArtic (sec)).withLabel (label));
            if (place)
                out.push_back (Gesture::make (a, b, 0.05, 0.05, 0.25, Targets::one (ArticKey::TbX, *place)).withLabel (label));
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
        const auto end = t;

        std::vector<size_t> vIdx;
        for (size_t i = 0; i < segs.size(); ++i)
            if (segs[i].vowel) vIdx.push_back (i);

        for (size_t n = 0; n < vIdx.size(); ++n)
        {
            const auto i = vIdx[n];
            const auto prev = n > 0 ? times[vIdx[n - 1]].t1 : times[i].t0 - 0.08;
            const auto next = n + 1 < vIdx.size() ? times[vIdx[n + 1]].t0 : times[i].t1 + 0.08;
            const auto rIn = js_max (0.03, 0.75 * (times[i].t0 - prev)) + 0.01;
            const auto rOut = js_max (0.03, 0.4 * (next - times[i].t1)) + 0.01;
            gestures.push_back (Gesture::make (times[i].t0, times[i].t1, rIn, rOut, 1.0, Targets::fromArtic (*segs[i].vowel))
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
            auto addOral = [&] (const ArticPatch& tg, double a, double b, double speed, std::string_view label)
            {
                oralGesturesInto (def, tg, a, b, speed, label, gestures);
            };
            switch (def.manner)
            {
                case Manner::Stop:
                    if (prePlacedBehindClosure (segs, i))
                        addOral (def.targets, t0 + js_min (PLACED_CLOSURE_DELAY, js_max (0.0, dur - 0.03)), t1, 1.55, def.ipa);
                    else
                        addOral (def.targets, t0, t1, 1.55, def.ipa);
                    break;
                case Manner::Nasal:
                    addOral (def.targets, t0, t1, 1.55, def.ipa);
                    break;
                case Manner::Tap:
                    addOral (def.targets, t0, t1, 2.8, def.ipa);
                    break;
                case Manner::Trill:
                {
                    const auto nTr = js_max (2.0, std::round (dur * 26.0));
                    for (double k = 0; k < nTr; k += 1.0)
                    {
                        const auto a = t0 + (k / nTr) * dur;
                        addOral (def.targets, a, a + 0.35 / 26.0, 3.2, def.ipa);
                    }
                    break;
                }
                case Manner::Affricate:
                {
                    const auto split = t0 + dur * 0.55;
                    addOral (def.targets, t0, split, 2.0, def.ipa);
                    if (def.release)
                    {
                        // temporary: use same def oral? should use release phoneme targets
                        if (auto rel = phoneme (def.release, body))
                            oralGesturesInto (*rel, rel->targets, split + 0.008, t1, 1.4, def.ipa, gestures);
                    }
                    break;
                }
                case Manner::Fricative:
                case Manner::Approximant:
                case Manner::Lateral:
                    addOral (def.targets, t0, t1, 1.3, def.ipa);
                    break;
                case Manner::Glottal:
                case Manner::Vowel:
                    break;
            }
            if (def.nasal)
                gestures.push_back (Gesture::make (t0 - 0.02, t1 - 0.02, 0.05, 0.03, 4.0, Targets::one (ArticKey::Velum, 0.45))
                                        .withLabel ("velum"));
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
                    const bool aspirated = nextVowel && ! (prevSeg && prevSeg->ipa == "s");
                    const auto vot = (aspirated ? 0.05 : 0.012) / rate;
                    gestures.push_back (Gesture::make (t0 + 0.25 * dur, t1 + vot * 0.4, 0.035, aspirated ? 0.045 : 0.02, 6.0,
                                                       Targets::one (SourceKey::Abduction, aspirated ? 0.36 : 0.22))
                                            .withLabel ("aspiration"));
                }
                else
                    gestures.push_back (Gesture::make (t0 - 0.015, t1 + 0.005, 0.03, 0.03, 6.0, Targets::one (SourceKey::Abduction, 0.3))
                                            .withLabel ("voiceless"));
            }
            else if (def.manner == Manner::Fricative)
                gestures.push_back (Gesture::make (t0, t1, 0.03, 0.03, 3.0, Targets::one (SourceKey::Abduction, 0.035))
                                        .withLabel ("voiced frication"));
        }

        const bool openEnd = o.openEnd;
        const bool sung = o.sung;
        const auto breathAt = sung ? BREATH_SUNG_AT : 0.04;
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
                const auto decl = 1.0 - 0.1 * prog;
                const bool isNuclear = ssize_t (i) == nuclear;
                const auto acc = s.stress == 1 ? 1.0 + (isNuclear ? accent * 1.8 : accent)
                                : s.stress == 2 ? 1.0 + accent * 0.5 : 1.0;
                target = f0 * decl * acc;
                const bool last = n + 1 == vIdx.size();
                if (last && o.intonation == Intonation::Question)
                    target = f0 * 1.35;
                if (last && o.intonation == Intonation::Statement)
                {
                    if (isNuclear && t1 - t0 > 0.08)
                    {
                        gestures.push_back (Gesture::make (t0, t0 + 0.35 * (t1 - t0), 0.05, 0.05, 1.0, Targets::one (SourceKey::F0, target))
                                                .withLabel ("pitch"));
                        gestures.push_back (Gesture::make (t0 + 0.55 * (t1 - t0), t1, 0.06, 0.06, 1.0, Targets::one (SourceKey::F0, f0 * 0.82))
                                                .withLabel ("pitch"));
                        continue;
                    }
                    target = f0 * 0.82;
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
