#include "RhythmEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace dooblrrhythm
{
    namespace
    {
        struct Rng
        {
            uint32_t s = 1;
            explicit Rng (uint32_t seed) : s (seed ? seed : 1u) {}
            uint32_t next()
            {
                s = s * 1664525u + 1013904223u;
                return s;
            }
            float unit() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
            bool chance (float p) { return unit() < p; }
        };

        uint32_t mix (uint32_t a, uint32_t b)
        {
            a ^= b + 0x9e3779b9u + (a << 6) + (a >> 2);
            return a;
        }

        float score01 (uint32_t seed, uint32_t a, uint32_t b, uint32_t c = 0)
        {
            Rng r (mix (mix (mix (seed, a), b), c));
            return r.unit();
        }

        int stepsPerBeat (TimeSignature ts)
        {
            const int spb = stepsPerBar (ts);
            const int beats = juce::jmax (1, beatsPerBar (ts));
            return juce::jmax (1, spb / beats);
        }

        bool onBeat (TimeSignature ts, int step)
        {
            const int spb = stepsPerBar (ts);
            return ((step % spb) % stepsPerBeat (ts)) == 0;
        }

        int wrapStep (int step, int total)
        {
            if (total <= 0) return 0;
            step %= total;
            if (step < 0) step += total;
            return step;
        }

        Role roleTable (Style style, Instrument i)
        {
            const auto claveI = Instrument::clave;
            const auto sambaBell = Instrument::agogo;

            switch (style)
            {
                case Style::sonSalsa:
                case Style::rumba:
                    if (i == claveI) return Role::claveKey;
                    if (i == Instrument::cowbell || i == Instrument::guiro || i == Instrument::maracas
                        || i == Instrument::timbales)
                        return Role::timekeeper;
                    if (i == Instrument::tumba || i == Instrument::udu) return Role::bassAnchor;
                    if (i == Instrument::quinto || i == Instrument::bongos) return Role::leadSolo;
                    return Role::interlockMid;

                case Style::cumbia:
                    if (i == claveI || i == Instrument::cowbell) return Role::claveKey;
                    if (i == Instrument::guiro || i == Instrument::shakers || i == Instrument::ganza)
                        return Role::timekeeper;
                    if (i == Instrument::tumba) return Role::bassAnchor;
                    if (i == Instrument::quinto) return Role::leadSolo;
                    return Role::interlockMid;

                case Style::sambaBatucada:
                case Style::baileFunk:
                    if (i == sambaBell || i == claveI) return Role::claveKey;
                    if (i == Instrument::ganza || i == Instrument::shakers || i == Instrument::cabasa)
                        return Role::timekeeper;
                    if (i == Instrument::tumba || i == Instrument::dundunba) return Role::bassAnchor;
                    if (i == Instrument::quinto || i == Instrument::timbales) return Role::leadSolo;
                    return Role::interlockMid;

                case Style::afroCuban68:
                    if (i == Instrument::gankogui || i == claveI) return Role::claveKey;
                    if (i == Instrument::shekere || i == Instrument::cowbell) return Role::timekeeper;
                    if (i == Instrument::udu || i == Instrument::tumba) return Role::bassAnchor;
                    if (i == Instrument::quinto || i == Instrument::djembe) return Role::leadSolo;
                    return Role::interlockMid;

                case Style::westAfrican:
                    if (i == Instrument::gankogui) return Role::claveKey;
                    if (i == Instrument::kenkeni || i == Instrument::shekere) return Role::timekeeper;
                    if (i == Instrument::dundunba) return Role::bassAnchor;
                    if (i == Instrument::djembe || i == Instrument::talkingDrum) return Role::leadSolo;
                    if (i == Instrument::sangban) return Role::interlockMid;
                    return Role::interlockMid;

                case Style::afrobeat:
                    if (i == claveI || i == Instrument::cowbell) return Role::claveKey;
                    if (i == Instrument::shekere || i == Instrument::shakers) return Role::timekeeper;
                    if (i == Instrument::tumba || i == Instrument::dundunba) return Role::bassAnchor;
                    if (i == Instrument::talkingDrum || i == Instrument::quinto) return Role::leadSolo;
                    return Role::interlockMid;

                case Style::afroHouse:
                case Style::amapianoPerc:
                    if (i == claveI) return Role::claveKey;
                    if (i == Instrument::shakers || i == Instrument::shekere || i == Instrument::cabasa
                        || i == Instrument::ganza)
                        return Role::timekeeper;
                    if (i == Instrument::udu || i == Instrument::tumba) return Role::bassAnchor;
                    if (i == Instrument::conga || i == Instrument::bongos) return Role::interlockMid;
                    return Role::interlockMid;

                case Style::worldFusion:
                case Style::count:
                    if (i == claveI || i == Instrument::gankogui) return Role::claveKey;
                    if (i == Instrument::shekere || i == Instrument::shakers) return Role::timekeeper;
                    if (i == Instrument::tumba || i == Instrument::dundunba || i == Instrument::udu)
                        return Role::bassAnchor;
                    if (i == Instrument::djembe || i == Instrument::quinto || i == Instrument::talkingDrum)
                        return Role::leadSolo;
                    return Role::interlockMid;
            }

            return Role::interlockMid;
        }
    }

    Role roleFor (Style style, Instrument instrument) noexcept
    {
        return roleTable (style, instrument);
    }

    bool inStyleEnsemble (Style style, Instrument instrument) noexcept
    {
        switch (style)
        {
            case Style::sonSalsa:
                return instrument == Instrument::clave || instrument == Instrument::cowbell
                    || instrument == Instrument::tumba || instrument == Instrument::conga
                    || instrument == Instrument::quinto || instrument == Instrument::bongos
                    || instrument == Instrument::timbales || instrument == Instrument::guiro
                    || instrument == Instrument::maracas;
            case Style::rumba:
                return instrument == Instrument::clave || instrument == Instrument::quinto
                    || instrument == Instrument::conga || instrument == Instrument::tumba
                    || instrument == Instrument::shekere || instrument == Instrument::guiro
                    || instrument == Instrument::udu;
            case Style::cumbia:
                return instrument == Instrument::guiro || instrument == Instrument::conga
                    || instrument == Instrument::tumba || instrument == Instrument::cowbell
                    || instrument == Instrument::shakers || instrument == Instrument::clave
                    || instrument == Instrument::ganza;
            case Style::sambaBatucada:
                return instrument == Instrument::agogo || instrument == Instrument::tumba
                    || instrument == Instrument::dundunba || instrument == Instrument::ganza
                    || instrument == Instrument::shakers || instrument == Instrument::quinto
                    || instrument == Instrument::timbales || instrument == Instrument::cabasa
                    || instrument == Instrument::shekere;
            case Style::afroCuban68:
                return instrument == Instrument::gankogui || instrument == Instrument::clave
                    || instrument == Instrument::conga || instrument == Instrument::tumba
                    || instrument == Instrument::shekere || instrument == Instrument::udu
                    || instrument == Instrument::quinto;
            case Style::westAfrican:
                return instrument == Instrument::gankogui || instrument == Instrument::kenkeni
                    || instrument == Instrument::sangban || instrument == Instrument::dundunba
                    || instrument == Instrument::djembe || instrument == Instrument::talkingDrum
                    || instrument == Instrument::shekere;
            case Style::afrobeat:
                return instrument == Instrument::clave || instrument == Instrument::shekere
                    || instrument == Instrument::conga || instrument == Instrument::tumba
                    || instrument == Instrument::cowbell || instrument == Instrument::talkingDrum
                    || instrument == Instrument::djembe || instrument == Instrument::shakers;
            case Style::afroHouse:
                return instrument == Instrument::shakers || instrument == Instrument::conga
                    || instrument == Instrument::tumba || instrument == Instrument::cowbell
                    || instrument == Instrument::shekere || instrument == Instrument::cabasa
                    || instrument == Instrument::clave || instrument == Instrument::udu;
            case Style::amapianoPerc:
                // Log-drum-free: shakers, congas, bongos, shekere, cowbell. No dundunba-as-log.
                return instrument == Instrument::shakers || instrument == Instrument::conga
                    || instrument == Instrument::bongos || instrument == Instrument::shekere
                    || instrument == Instrument::cowbell || instrument == Instrument::guiro
                    || instrument == Instrument::cabasa || instrument == Instrument::clave;
            case Style::baileFunk:
                return instrument == Instrument::quinto || instrument == Instrument::agogo
                    || instrument == Instrument::ganza || instrument == Instrument::tumba
                    || instrument == Instrument::shakers || instrument == Instrument::timbales
                    || instrument == Instrument::cabasa;
            case Style::worldFusion:
            case Style::count:
                return true;
        }
        return true;
    }

    namespace
    {
        Articulation defaultArtic (Instrument i, Role role, bool accent)
        {
            switch (i)
            {
                case Instrument::guiro:    return Articulation::scratch;
                case Instrument::shakers:
                case Instrument::maracas:
                case Instrument::shekere:
                case Instrument::cabasa:
                case Instrument::ganza:    return Articulation::shake;
                case Instrument::gankogui:
                case Instrument::cowbell:
                case Instrument::agogo:    return accent ? Articulation::bell : Articulation::open;
                case Instrument::timbales: return accent ? Articulation::cascara : Articulation::open;
                case Instrument::djembe:   return role == Role::bassAnchor ? Articulation::bass
                                                                           : (accent ? Articulation::slap : Articulation::tone);
                case Instrument::quinto:
                case Instrument::conga:
                case Instrument::tumba:
                case Instrument::bongos:   return accent ? Articulation::slap : Articulation::open;
                case Instrument::clave:
                case Instrument::talkingDrum:
                case Instrument::kenkeni:
                case Instrument::sangban:
                case Instrument::dundunba:
                case Instrument::udu:      return accent ? Articulation::slap : Articulation::open;
            }
            return accent ? Articulation::slap : Articulation::open;
        }

        float velocityFor (const InstrumentControls& c, bool accent, float human, Rng& rng)
        {
            const auto base = juce::jlimit (0.05f, 1.0f, c.velocity * 0.01f);
            const auto a = accent ? 1.0f : 0.72f;
            const auto n = (rng.unit() - 0.5f) * human * 0.25f;
            return juce::jlimit (0.05f, 1.0f, base * a + n);
        }

        struct Candidate
        {
            Hit hit;
            float keepScore = 0.0f; // 0..1, kept when keepScore * 100 < density (or complexity)
            bool ornament = false;
        };

        void pushHit (std::vector<Candidate>& out, uint32_t seed, int inst, int step, Articulation artic,
                      float vel, uint8_t structural, bool ornament)
        {
            Hit h;
            h.instrument = (uint8_t) inst;
            h.step = (uint16_t) step;
            h.articulation = (uint8_t) artic;
            h.velocity = vel;
            h.structural = structural;
            Candidate c;
            c.hit = h;
            c.ornament = ornament;
            c.keepScore = score01 (seed, (uint32_t) inst, (uint32_t) step, (uint32_t) artic + (ornament ? 17u : 0u));
            out.push_back (c);
        }

        int sameRegisterCount (const std::vector<Hit>& hits, int step, Register reg, int ignoreInst = -1)
        {
            int n = 0;
            for (const auto& h : hits)
                if ((int) h.step == step && (int) h.instrument != ignoreInst
                    && instrumentRegister ((Instrument) h.instrument) == reg)
                    ++n;
            return n;
        }

        bool occupied (const std::vector<Hit>& hits, int inst, int step)
        {
            for (const auto& h : hits)
                if ((int) h.instrument == inst && (int) h.step == step)
                    return true;
            return false;
        }

        void copyLocked (Pattern& dest, const Pattern* previous, const GenerateRequest& req)
        {
            if (previous == nullptr)
                return;

            const int total = dest.totalSteps();
            const int prevTotal = previous->totalSteps();
            if (total <= 0 || prevTotal <= 0)
                return;

            for (int i = 0; i < numInstruments; ++i)
            {
                if (! req.instruments[(size_t) i].locked)
                    continue;
                dest.hits.erase (std::remove_if (dest.hits.begin(), dest.hits.end(),
                                                 [i] (const Hit& h) { return (int) h.instrument == i; }),
                                 dest.hits.end());
                for (const auto& h : previous->hits)
                {
                    if ((int) h.instrument != i)
                        continue;
                    auto c = h;
                    c.step = (uint16_t) wrapStep ((int) h.step, total);
                    dest.add (c);
                }
            }
        }
    }

    void applyGroove (Pattern& pattern, const GrooveSettings& groove)
    {
        const int spb = juce::jmax (1, pattern.stepsPerBar);
        const auto ts = pattern.timeSignature;
        const int spBeat = stepsPerBeat (ts);
        Rng rng (mix (groove.seed, 0x60A1u));

        for (auto& h : pattern.hits)
        {
            const int inBar = (int) h.step % spb;
            const int inBeat = inBar % spBeat;
            const bool evenPulse = ! isCompound (ts) ? (inBeat == spBeat / 2)
                                                     : (inBeat == 1); // 6/8: delay the second pulse of the group
            const bool key = h.structural >= 2;

            float swing = 0.0f;
            if (! key && evenPulse)
                swing = groove.swing * 0.01f * 0.45f; // up to 45% of a step late

            float push = 0.0f;
            if (! key)
                push = groove.pushPull * 0.01f * 0.25f;

            float human = 0.0f;
            if (groove.humanize > 0.0f)
            {
                const auto amt = groove.humanize * 0.01f * (key ? 0.08f : 0.35f);
                human = (rng.unit() - 0.5f) * 2.0f * amt;
                h.velocity = juce::jlimit (0.05f, 1.0f, h.velocity + (rng.unit() - 0.5f) * amt * 0.4f);
            }

            // Style microtiming templates.
            float feel = 0.0f;
            if (! key)
            {
                if (groove.style == Style::sambaBatucada || groove.style == Style::baileFunk)
                    feel = evenPulse ? 0.08f : -0.03f; // slight late offbeats
                else if (isCompound (ts) || groove.style == Style::afroCuban68 || groove.style == Style::westAfrican)
                    feel = (inBeat == 0) ? -0.04f : 0.05f; // lean into the bell
            }

            h.timing = juce::jlimit (-1.0f, 1.0f, swing + push + human + feel);
        }
    }

    Pattern generate (const GenerateRequest& request)
    {
        Pattern p;
        p.style = request.style;
        p.timeSignature = request.timeSignature;
        p.clave = request.clave;
        p.section = request.section;
        p.bars = juce::jlimit (minBars, maxBars, request.bars);
        p.stepsPerBar = stepsPerBar (request.timeSignature);
        p.seed = request.seed;

        const int spb = p.stepsPerBar;
        const int total = p.totalSteps();
        const auto key = keyPatternFor (request.style, request.clave, request.timeSignature);
        const auto humanAmt = juce::jlimit (0.0f, 1.0f, request.humanize * 0.01f);
        Rng rng (request.seed ? request.seed : 1u);

        std::vector<Candidate> pool;

        auto active = [&] (int inst) -> bool
        {
            const auto& c = request.instruments[(size_t) inst];
            if (c.mute) return false;
            if (c.locked) return false; // filled later from previous
            if (c.density <= 0.0f) return false;
            return true;
        };

        auto densityOf = [&] (int inst) -> float
        {
            return juce::jlimit (0.0f, 100.0f, request.instruments[(size_t) inst].density);
        };

        auto complexityOf = [&] (int inst) -> float
        {
            return juce::jlimit (0.0f, 100.0f, request.instruments[(size_t) inst].complexity * 0.5f
                                                   + request.complexity * 0.5f);
        };

        auto syncOf = [&] (int inst) -> float
        {
            return juce::jlimit (0.0f, 100.0f, request.instruments[(size_t) inst].syncopation * 0.5f
                                                   + request.syncopation * 0.5f);
        };

        auto velOf = [&] (int inst, bool accent) -> float
        {
            return velocityFor (request.instruments[(size_t) inst], accent, humanAmt, rng);
        };

        // --- 1. Key pattern (clave / bell / partido alto). Never dropped. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst))
                continue;
            if (roleFor (request.style, (Instrument) inst) != Role::claveKey)
                continue;
            if (densityOf (inst) <= 0.0f)
                continue;

            for (int step = 0; step < total; ++step)
            {
                if (! isKeyStroke (key, step))
                    continue;
                const auto accent = keyStrokeIndex (key, step) == 0;
                const auto artic = defaultArtic ((Instrument) inst, Role::claveKey, accent);
                pushHit (pool, request.seed, inst, step, artic, velOf (inst, true), 2, false);
            }
        }

        // Always have a clave/bell instrument if the style needs one and the user hasn't muted it.
        auto ensureKey = [&] (Instrument fallback)
        {
            const bool have = std::any_of (pool.begin(), pool.end(), [] (const Candidate& c)
                                           { return c.hit.structural >= 2; });
            if (have)
                return;
            if (! active ((int) fallback) && request.instruments[(size_t) fallback].density <= 0.0f)
            {
                // Force the fallback onto the key pattern so clave styles always have a clave.
                for (int step = 0; step < total; ++step)
                {
                    if (! isKeyStroke (key, step))
                        continue;
                    pushHit (pool, request.seed, (int) fallback, step, Articulation::open,
                             0.9f, 2, false);
                }
            }
        };

        if (usesClave (request.style))
            ensureKey (Instrument::clave);
        else if (usesSixEightBell (request.style))
            ensureKey (Instrument::gankogui);
        else if (request.style == Style::sambaBatucada || request.style == Style::baileFunk)
            ensureKey (Instrument::agogo);

        // --- 2. Timekeepers: cascara / guiro / shaker grid interlocking around the clave. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst) || roleFor (request.style, (Instrument) inst) != Role::timekeeper)
                continue;

            const bool compound = isCompound (request.timeSignature);
            for (int step = 0; step < total; ++step)
            {
                const int inBar = step % spb;
                bool place = false;
                bool accent = false;

                if (inst == (int) Instrument::guiro || inst == (int) Instrument::ganza
                    || inst == (int) Instrument::shakers || inst == (int) Instrument::maracas
                    || inst == (int) Instrument::cabasa || inst == (int) Instrument::shekere)
                {
                    // Continuous pulse, skipping clave strokes so they don't mask the key.
                    if (compound)
                        place = (inBar % 2 == 0) && ! isKeyStroke (key, step);
                    else
                        place = (inBar % 2 == 0);
                    accent = onBeat (request.timeSignature, step);
                    if (isKeyStroke (key, step) && instrumentRegister ((Instrument) inst) == Register::high)
                        place = false;
                }
                else if (inst == (int) Instrument::cowbell || inst == (int) Instrument::timbales
                         || inst == (int) Instrument::kenkeni)
                {
                    // Cascara / bell: off the clave, on the remaining strong pulses.
                    if (compound)
                        place = (inBar == 0 || inBar == 3 || inBar == 6 || inBar == 9) && ! isKeyStroke (key, step);
                    else
                    {
                        // 2-3 / 3-2 cascara: 1, 2&, 3, 4& typically, minus clave strokes.
                        place = (inBar == 0 || inBar == 6 || inBar == 8 || inBar == 14);
                        if (isKeyStroke (key, step))
                            place = false;
                    }
                    accent = onBeat (request.timeSignature, step);
                }
                else
                {
                    place = onBeat (request.timeSignature, step) && ! isKeyStroke (key, step);
                    accent = inBar == 0;
                }

                if (! place)
                    continue;
                pushHit (pool, request.seed, inst, step,
                         defaultArtic ((Instrument) inst, Role::timekeeper, accent),
                         velOf (inst, accent), 1, false);
            }
        }

        // --- 3. Bass anchors (tumbao / dundunba / udu). Open tones on clave-consistent steps. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst) || roleFor (request.style, (Instrument) inst) != Role::bassAnchor)
                continue;

            for (int step = 0; step < total; ++step)
            {
                const int inBar = step % spb;
                const bool openPlace = isClaveConsistentOpen (request.style, request.clave,
                                                              request.timeSignature, step);
                bool slapPlace = false;
                if (! isCompound (request.timeSignature))
                    slapPlace = (inBar == 4 || inBar == 8); // 2 and 3 of the bar
                else
                    slapPlace = (inBar == 3 || inBar == 9);

                if (openPlace)
                {
                    pushHit (pool, request.seed, inst, step, Articulation::open,
                             velOf (inst, true), 2, false); // structural 2: tumbao opens stay put
                }
                else if (slapPlace)
                {
                    pushHit (pool, request.seed, inst, step, Articulation::slap,
                             velOf (inst, false), 1, false);
                }
            }
        }

        // --- 4. Interlocking mids: sit in the holes around clave and bass. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst) || roleFor (request.style, (Instrument) inst) != Role::interlockMid)
                continue;

            for (int step = 0; step < total; ++step)
            {
                if (isKeyStroke (key, step))
                    continue; // never mask the clave
                const int inBar = step % spb;
                bool place = false;
                Articulation artic = Articulation::open;

                if (inst == (int) Instrument::conga || inst == (int) Instrument::sangban)
                {
                    // Martillo / sangban: the offbeats the tumba leaves.
                    if (! isCompound (request.timeSignature))
                        place = (inBar == 2 || inBar == 6 || inBar == 10 || inBar == 14);
                    else
                        place = (inBar == 1 || inBar == 4 || inBar == 7 || inBar == 10);
                    artic = (inBar % 4 == 2) ? Articulation::slap : Articulation::open;
                }
                else if (inst == (int) Instrument::bongos)
                {
                    if (! isCompound (request.timeSignature))
                        place = (inBar % 2 == 1); // the 'e' and 'a'
                    else
                        place = (inBar % 3 == 1);
                    artic = Articulation::tip;
                }
                else
                {
                    place = ! onBeat (request.timeSignature, step) && (inBar % 2 == 1);
                    artic = Articulation::mute;
                }

                if (! place)
                    continue;
                pushHit (pool, request.seed, inst, step, artic, velOf (inst, false), 1, false);
            }
        }

        // --- 5. Lead / solo: call-and-response answering the 3-side of the clave. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst) || roleFor (request.style, (Instrument) inst) != Role::leadSolo)
                continue;

            for (int step = 0; step < total; ++step)
            {
                if (isKeyStroke (key, step))
                    continue;
                const int bar = (spb > 0) ? step / spb : 0;
                const int inBar = step % spb;
                const bool threeSide = request.clave == ClaveDirection::threeTwo ? (bar % 2 == 0) : (bar % 2 == 1);

                bool place = false;
                Articulation artic = Articulation::slap;

                if (isCompound (request.timeSignature))
                {
                    // Answer the bell: fill the gaps 1, 4, 8, 11 on the "call" half.
                    place = threeSide ? (inBar == 1 || inBar == 4) : (inBar == 8 || inBar == 11);
                }
                else
                {
                    // 3-side: play the holes after the 3-side clave (between 0, 6, 12).
                    if (threeSide)
                        place = (inBar == 2 || inBar == 8 || inBar == 10);
                    else
                        place = (inBar == 2 || inBar == 14); // sparser response on the 2-side
                }

                if (! place)
                    continue;
                if (inst == (int) Instrument::djembe)
                    artic = Articulation::slap;
                else if (inst == (int) Instrument::talkingDrum)
                    artic = Articulation::tone;
                pushHit (pool, request.seed, inst, step, artic, velOf (inst, true), 1, false);
            }
        }

        // --- 6. Ornaments from complexity: heel-toe, ghosts, extra shakes. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst))
                continue;
            const auto role = roleFor (request.style, (Instrument) inst);
            if (role == Role::claveKey)
                continue;

            for (int step = 0; step < total; ++step)
            {
                const int inBar = step % spb;
                bool ghost = false;
                Articulation artic = Articulation::mute;

                if ((Instrument) inst == Instrument::conga || (Instrument) inst == Instrument::quinto
                    || (Instrument) inst == Instrument::tumba || (Instrument) inst == Instrument::djembe)
                {
                    ghost = (inBar % 4 == 1); // heel on the e
                    artic = Articulation::heel;
                }
                else if ((Instrument) inst == Instrument::bongos)
                {
                    ghost = (inBar % 4 == 3);
                    artic = Articulation::toe;
                }
                else if (instrumentRegister ((Instrument) inst) == Register::high
                         && role == Role::timekeeper)
                {
                    ghost = ! onBeat (request.timeSignature, step) && (inBar % 2 == 1);
                    artic = Articulation::shake;
                }

                if (! ghost || isKeyStroke (key, step))
                    continue;
                pushHit (pool, request.seed, inst, step, artic, velOf (inst, false) * 0.45f, 0, true);
            }
        }

        // --- 7. Syncopation: extra off-beat candidates, kept by the syncopation threshold. ---
        for (int inst = 0; inst < numInstruments; ++inst)
        {
            if (! active (inst))
                continue;
            if (roleFor (request.style, (Instrument) inst) == Role::claveKey)
                continue;

            for (int step = 0; step < total; ++step)
            {
                if (onBeat (request.timeSignature, step) || isKeyStroke (key, step))
                    continue;
                const float s = score01 (request.seed, 0x51c0u, (uint32_t) inst, (uint32_t) step);
                if (s * 100.0f >= syncOf (inst))
                    continue;
                pushHit (pool, mix (request.seed, 0xA5u), inst, step, Articulation::mute,
                         velOf (inst, false) * 0.6f, 0, true);
            }
        }

        // --- 8. Filter by density / complexity. Structural 2 always kept. ---
        std::vector<Hit> kept;
        kept.reserve (pool.size());

        for (auto& c : pool)
        {
            const int inst = (int) c.hit.instrument;
            const float dens = densityOf (inst);
            const float comp = complexityOf (inst);
            const bool ensemble = inStyleEnsemble (request.style, (Instrument) inst);
            // Non-ensemble parts need a higher density to enter, so a default kit stays readable,
            // but the control still does something above ~55.
            const float gate = ensemble ? dens : juce::jmax (0.0f, dens - 55.0f) * (100.0f / 45.0f);

            if (c.hit.structural >= 2)
            {
                kept.push_back (c.hit);
                continue;
            }
            if (c.ornament)
            {
                if (c.keepScore * 100.0f < comp && (ensemble || gate > 20.0f))
                    kept.push_back (c.hit);
                continue;
            }
            if (c.keepScore * 100.0f < gate)
                kept.push_back (c.hit);
        }

        // --- 9. Masking: at most 2 onsets per register per step; clave/bass win. ---
        std::sort (kept.begin(), kept.end(), [] (const Hit& a, const Hit& b)
        {
            if (a.step != b.step) return a.step < b.step;
            if (a.structural != b.structural) return a.structural > b.structural;
            return a.instrument < b.instrument;
        });

        std::vector<Hit> resolved;
        resolved.reserve (kept.size());
        constexpr int maxSameRegister = 2;

        for (const auto& h : kept)
        {
            const auto reg = instrumentRegister ((Instrument) h.instrument);
            if (sameRegisterCount (resolved, (int) h.step, reg) >= maxSameRegister)
            {
                if (h.structural >= 2)
                    continue; // clave/bass already occupy this register; never stack a third
                // Try the neighbouring off-clave step rather than stacking.
                int shifted = (int) h.step + 1;
                if (shifted < total && ! isKeyStroke (key, shifted)
                    && sameRegisterCount (resolved, shifted, reg) < maxSameRegister
                    && ! occupied (resolved, (int) h.instrument, shifted))
                {
                    auto s = h;
                    s.step = (uint16_t) shifted;
                    resolved.push_back (s);
                }
                continue;
            }
            if (occupied (resolved, (int) h.instrument, (int) h.step))
                continue;
            resolved.push_back (h);
        }

        // --- 10. Sections: intro / break / outro thin; fill densifies the last bar and
        //     leaves the loop downbeat as the groove (no extra fill notes on step 0). ---
        if (request.section != Section::main && total > 0)
        {
            std::vector<Hit> shaped;
            shaped.reserve (resolved.size());
            const int lastBar = (p.bars - 1) * spb;

            for (const auto& h : resolved)
            {
                const int bar = spb > 0 ? (int) h.step / spb : 0;
                const int inBar = (int) h.step % spb;
                const auto role = roleFor (request.style, (Instrument) h.instrument);
                bool keep = true;

                if (request.section == Section::intro)
                {
                    if (bar == 0)
                        keep = role == Role::claveKey || role == Role::timekeeper;
                    else if (bar == 1)
                        keep = role != Role::leadSolo;
                }
                else if (request.section == Section::brk)
                {
                    keep = role == Role::claveKey || role == Role::timekeeper
                        || (role == Role::bassAnchor && inBar == 0);
                }
                else if (request.section == Section::outro)
                {
                    if (bar >= p.bars - 1)
                        keep = role == Role::claveKey || role == Role::timekeeper;
                    else if (bar >= p.bars - 2)
                        keep = role != Role::leadSolo;
                }
                else if (request.section == Section::fill)
                {
                    // Extra density is added below; here just keep the groove.
                    keep = true;
                }

                if (keep)
                    shaped.push_back (h);
            }

            if (request.section == Section::fill)
            {
                // Last beat of the last bar: extra mid/lead hits, never on the following downbeat
                // (step 0 of the loop). Fill stays inside the last bar.
                const int fillStart = juce::jmax (lastBar, total - juce::jmax (4, spb / 2));
                for (int step = fillStart; step < total; ++step)
                {
                    if (step == 0)
                        continue;
                    for (int inst = 0; inst < numInstruments; ++inst)
                    {
                        if (! active (inst))
                            continue;
                        const auto role = roleFor (request.style, (Instrument) inst);
                        if (role != Role::leadSolo && role != Role::interlockMid)
                            continue;
                        if (isKeyStroke (key, step))
                            continue;
                        if (occupied (shaped, inst, step))
                            continue;
                        if (sameRegisterCount (shaped, step, instrumentRegister ((Instrument) inst)) >= 3)
                            continue;
                        const float s = score01 (request.seed, 0xF11u, (uint32_t) inst, (uint32_t) step);
                        if (s > 0.55f)
                            continue;
                        Hit h;
                        h.instrument = (uint8_t) inst;
                        h.step = (uint16_t) step;
                        h.articulation = (uint8_t) Articulation::slap;
                        h.velocity = velOf (inst, true);
                        h.structural = 0;
                        shaped.push_back (h);
                    }
                }
            }

            resolved.swap (shaped);
        }

        p.hits.swap (resolved);
        p.used.fill (false);
        for (const auto& h : p.hits)
            p.used[(size_t) h.instrument] = true;

        GrooveSettings g;
        g.swing = request.swing;
        g.pushPull = request.pushPull;
        g.humanize = request.humanize;
        g.style = request.style;
        g.timeSignature = request.timeSignature;
        g.seed = request.seed;
        applyGroove (p, g);

        // Per-instrument timing offset (fraction of a step). Clave/key stays put.
        // Locked parts are copied after this so regenerate cannot rewrite them.
        for (auto& h : p.hits)
        {
            if (h.structural >= 2)
                continue;
            const auto off = request.instruments[(size_t) h.instrument].timingOffset * 0.01f; // -1..1
            h.timing = juce::jlimit (-1.0f, 1.0f, h.timing + off * 0.5f);
        }

        copyLocked (p, request.previous, request);

        const int lastBarStart = (p.bars - 1) * spb;
        auto capFor = [&] (int step) -> int
        {
            if (request.section == Section::fill && step >= lastBarStart)
                return 3;
            return maxSameRegister;
        };

        std::sort (p.hits.begin(), p.hits.end(), [] (const Hit& a, const Hit& b)
        {
            if (a.step != b.step) return a.step < b.step;
            if (a.structural != b.structural) return a.structural > b.structural;
            return a.instrument < b.instrument;
        });

        std::vector<Hit> capped;
        capped.reserve (p.hits.size());
        for (const auto& h : p.hits)
        {
            const auto reg = instrumentRegister ((Instrument) h.instrument);
            if (occupied (capped, (int) h.instrument, (int) h.step))
                continue;
            if (sameRegisterCount (capped, (int) h.step, reg) >= capFor ((int) h.step))
                continue;
            capped.push_back (h);
        }
        p.hits.swap (capped);

        p.used.fill (false);
        for (const auto& h : p.hits)
            p.used[(size_t) h.instrument] = true;

        std::sort (p.hits.begin(), p.hits.end(), [] (const Hit& a, const Hit& b)
        {
            if (a.step != b.step) return a.step < b.step;
            return a.instrument < b.instrument;
        });

        return p;
    }

    namespace
    {
        constexpr int ppq = 480;

        double ticksPerStep (const Pattern& pattern, int timeFormat)
        {
            const double quarters = (double) beatsPerBar (pattern.timeSignature)
                * (4.0 / (double) beatUnit (pattern.timeSignature));
            const double ticksPerBar = (double) juce::jmax (1, timeFormat) * quarters;
            return ticksPerBar / (double) juce::jmax (1, pattern.stepsPerBar);
        }
    }

    juce::MidiFile patternToMidi (const Pattern& pattern, double bpm, int instrumentOnly)
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        juce::MidiMessageSequence seq;
        const double tps = ticksPerStep (pattern, ppq);

        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60'000'000.0 / juce::jmax (20.0, bpm))));
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (beatsPerBar (pattern.timeSignature),
                                                                 beatUnit (pattern.timeSignature)));

        for (const auto& h : pattern.hits)
        {
            if (instrumentOnly >= 0 && (int) h.instrument != instrumentOnly)
                continue;
            const double tick = (double) h.step * tps;
            const int note = midiNoteFor ((Instrument) h.instrument, (Articulation) h.articulation);
            const juce::uint8 vel = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (h.velocity * 127.0f));
            seq.addEvent (juce::MidiMessage::noteOn (1, note, vel), tick);
            seq.addEvent (juce::MidiMessage::noteOff (1, note), tick + juce::jmax (1.0, tps * 0.4));
        }
        seq.updateMatchedPairs();
        seq.sort();
        file.addTrack (seq);
        return file;
    }

    juce::MidiFile patternToMidi (const Pattern& pattern, double bpm)
    {
        return patternToMidi (pattern, bpm, -1);
    }

    Pattern patternFromMidi (const juce::MidiFile& file, const Pattern& proto)
    {
        Pattern p = proto;
        p.hits.clear();
        p.used.fill (false);
        if (file.getNumTracks() <= 0)
            return p;

        const auto* seq = file.getTrack (0);
        if (seq == nullptr)
            return p;

        const int tpb = file.getTimeFormat() > 0 ? file.getTimeFormat() : ppq;
        const double tps = juce::jmax (1.0e-9, ticksPerStep (p, tpb));

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto* e = seq->getEventPointer (i);
            if (e == nullptr || ! e->message.isNoteOn())
                continue;
            Hit h;
            const int note = e->message.getNoteNumber();
            h.instrument = (uint8_t) instrumentFromMidiNote (note);
            h.articulation = (uint8_t) articulationFromMidiNote (note);
            const int step = (int) std::lround (e->message.getTimeStamp() / tps);
            h.step = (uint16_t) juce::jlimit (0, juce::jmax (0, p.totalSteps() - 1), step);
            h.velocity = e->message.getFloatVelocity();
            h.structural = 1;
            p.add (h);
        }
        return p;
    }
}
