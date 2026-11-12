#include "HumanVoice.h"
#include "Physics.h"
#include "Phonetics/Inventory.h"
#include "Vowels.h"
#include <algorithm>

namespace dooblrthroat::dsp
{
    HumanVoice::HumanVoice (double fsOut, HumanBody body, size_t oversample)
        : fs_out (fsOut), fs (fsOut * double (oversample)), m (oversample),
          airway (fsOut * double (oversample), body),
          glottis (fsOut * double (oversample)),
          dec (oversample)
    {
        ctrl_every = int64_t (64 * m);
        f0 = DEFAULT_SOURCE.f0;
        dc_coef = exp ((-2.0 * PI * 25.0) / fs_out);
        lim_hold_n = int64_t (js_round (0.025 * fs_out));
        lim_release = exp (-1.0 / (0.2 * fs_out));
        const auto x = artic.x;
        airway.apply (x, 0.0);
        airway.net.snap_to_targets();
        const auto sc = airway.body.scale;
        output_gain *= sc >= 1.0 ? 1.0 : sc <= 0.9 ? 0.55 : 0.55 + 0.45 * (sc - 0.9) / 0.1;
        apply_source();
        schwaRef = vowel_patch (13, BodyMix {});
        lf.reset();
        klatt.setFs (fs_out);
        klatt.reset();
        klattPad = klattVowelFrame ("ə", BodyMix {});
        klattPadOn = true;
    }

    void HumanVoice::reset()
    {
        artic.x = artic.target;
        if (artic.tip_free)
        {
            const auto tip = rest_tip (artic.x.jaw, artic.x.tb_x, artic.x.tb_y);
            artic.x.tt_x = artic.target.tt_x = tip.x;
            artic.x.tt_y = artic.target.tt_y = tip.y;
        }
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            artic.v[i] = 0.0;
            artic.z1[i] = artic.z2[i] = artic.x.get (ARTIC_ALL[i]);
        }
        airway.apply (artic.x, 0.0);
        airway.net.snap_to_targets();
        airway.net.reset();
        glottis.reset();
        gate = 0.0;
        gate_target = 0.0;
        p_lung = p_lung_now = p_lung_step = 0.0;
        dc_x = dc_y = 0.0;
        lim_env = 0.0;
        lim_hold = 0;
        ctrl_counter = 0;
        dec.value = 0.0;
        dec.pos = 0;
        dec.count = 0;
        std::fill (dec.buf.begin(), dec.buf.end(), 0.0);
        effort_body = 0.0;
        lf.reset();
        klatt.setFs (fs_out);
        klatt.reset();
        consX1 = consX2 = consY1 = consY2 = 0.0;
        burstEnv = 0.0;
        prevLip = 1.0;
        prevMinA = 1.0;
        consHpX = consHpY = 0.0;
        consFc = 0.0;
        clearScore();
    }

    void HumanVoice::set_articulation (const ArticPatch& values, bool tip_free)
    {
        artic.tip_free = tip_free;
        artic.set_target (values);
    }

    void HumanVoice::snap_articulation (const ArticPatch& values, bool tip_free)
    {
        artic.tip_free = tip_free;
        artic.snap (values);
    }

    void HumanVoice::apply_source()
    {
        const auto s = source;
        auto& g = glottis.params;
        const auto k2 = airway.body.scale * airway.body.scale;
        g.amplitude = 0.17 * k2 * (s.voiced ? 1.0 : 0.0);
        g.abduction = (s.voiced ? 0.1 * s.breathiness * s.breathiness : 0.2 + 0.1 * s.breathiness) * k2;
        base_abduction = g.abduction;
        base_closure = 0.0;
        g.chink = (0.008 + 0.03 * s.breathiness) * k2;
        g.open_quotient = 0.78 - 0.36 * s.tension;
        const auto bright = s.brightness - DEFAULT_SOURCE.brightness;
        const auto by_tension = 0.4 * s.tension + 1.375 * js_max (0.0, s.tension - 0.6);
        g.closure_sharpness = clamp (by_tension + 0.8 * bright, 0.0, 1.0);
        g.skew = 0.56 + 0.16 * s.tension + 0.2 * bright;
        g.fry = s.fry;
        g.jitter = js_min (0.005, 0.002 + 0.008 * s.roughness);
        g.shimmer = 0.02 + 0.22 * s.roughness;
        g.diplophonia = s.roughness > 0.5 ? (s.roughness - 0.5) * 1.4 : 0.0;
        g.vibrato_depth = s.vibrato_depth;
        g.vibrato_rate = s.vibrato_rate;
        g.drift = s.drift;
        g.effort = clamp ((abs (s.pressure) - 3.0) / 10.0, 0.0, 1.0);
    }

    void HumanVoice::applyScore (double dt)
    {
        using phonetics::N_CONTROL;
        using phonetics::SourceKey;
        using phonetics::sourceIndex;
        if (! scoreActive || scoreGestures == nullptr || scoreCount == 0)
        {
            for (auto& sp : artic.speed) sp = 1.0;
            return;
        }

        phonetics::Blended b[N_CONTROL];
        phonetics::blendAll (scoreGestures, scoreCount, scoreT, b);
        const auto rate = (std::isfinite (scoreRate) && scoreRate > 0.0) ? scoreRate : 1.0;
        scoreT += dt * rate;

        ArticPatch p;
        bool tipFree = true;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            artic.speed[i] = b[i].speed > 0.0 && std::isfinite (b[i].speed) ? b[i].speed : 1.0;
            if (b[i].weight > 0.0 && std::isfinite (b[i].value))
            {
                p.set (ARTIC_ALL[i], b[i].value);
                if (ARTIC_ALL[i] == ArticKey::TtX || ARTIC_ALL[i] == ArticKey::TtY)
                    tipFree = false;
            }
        }
        if (scoreNasal > 0.0f)
            p.set (ArticKey::Velum, scoreNasal);
        if (clarityMode == 1)
        {
            // Push vowel (and vowel-like) targets farther from schwa for F1/F2 contrast.
            constexpr ArticKey kContrast[] = {
                ArticKey::TbX, ArticKey::TbY, ArticKey::Jaw, ArticKey::LipAp,
                ArticKey::LipProt, ArticKey::LipWidth, ArticKey::Root, ArticKey::Larynx
            };
            const auto amt = 0.28;
            for (auto k : kContrast)
            {
                const auto pv = p.get (k);
                const auto sv = schwaRef.get (k);
                if (! pv || ! sv) continue;
                const auto lo = ARTIC_SPECS[artic_index (k)].min;
                const auto hi = ARTIC_SPECS[artic_index (k)].max;
                p.set (k, clamp (*sv + (*pv - *sv) * (1.0 + amt), lo, hi));
            }
        }
        set_articulation (p, tipFree);

        const auto k2 = airway.body.scale * airway.body.scale;
        const auto abd = b[sourceIndex (SourceKey::Abduction)];
        const auto clo = b[sourceIndex (SourceKey::Closure)];
        const auto br = b[sourceIndex (SourceKey::Breath)];
        const auto f0b = b[sourceIndex (SourceKey::F0)];
        const auto vib = b[sourceIndex (SourceKey::Vibrato)];

        auto abdVal = abd.weight > 0.0 ? abd.value * k2 : 0.0;
        if (clarityMode == 1 && abd.weight > 0.0)
            abdVal *= 1.4; // stronger place bursts / aspiration in the tract
        glottis.params.abduction = base_abduction + abdVal;
        glottis.params.closure = js_max (base_closure, clo.weight > 0.0 ? clo.value : 0.0);
        if (vib.weight > 0.0 && std::isfinite (vib.value))
            glottis.params.vibrato_depth = vib.value;

        if (scoreF0Override > 0.0 && std::isfinite (scoreF0Override))
            source.f0 = scoreF0Override;
        else if (f0b.weight > 0.0 && std::isfinite (f0b.value))
        {
            const auto scale = (std::isfinite (scoreF0Scale) && scoreF0Scale > 0.0) ? scoreF0Scale : 1.0;
            source.f0 = f0b.value * scale;
        }

        // Breath multiplier is applied in control() via a static-thread local? store on the object:
        // we stash it in p_lung after the gate multiply below; set a member? Use glide unused? 
        // Re-read control(): I'll apply breath after p_lung is computed by writing a member.
        // Use lim_env? No. Add nothing — control() calls applyScore first then multiplies.
        // We stash in p_lung_step temporarily? Better: score breath is read again in control.
        (void) br;
        (void) dt;
    }

    void HumanVoice::syncKlattFrame()
    {
        KlattFrame fr = klattPadOn ? klattPad : KlattFrame {};
        if (klattFrames != nullptr && klattCount > 0)
        {
            const auto hop = klattHop > 1e-5 ? klattHop : 0.005;
            auto u = (scoreT - klattT0) / hop;
            if (u <= 0.0)
                fr = klattFrames[0];
            else if (u >= double (klattCount - 1))
                fr = klattFrames[klattCount - 1];
            else
            {
                const auto i = size_t (u);
                fr = lerpKlatt (klattFrames[i], klattFrames[i + 1], u - double (i));
            }
        }
        klatt.setFrame (fr);
    }

    void HumanVoice::renderKlatt (float* out, size_t n)
    {
        const auto ctrl = (ctrl_every > 0) ? ctrl_every : int64_t (64);
        // Control at the output rate: ctrl_every is in oversampled samples.
        const auto os = m == 0 ? size_t (1) : m;
        const auto every = js_max (1.0, double (ctrl) / double (os));
        // Persist across 16-sample engine chunks; resetting to 0 here made
        // control() run every chunk with a 64-sample dt (~4x too fast).
        int64_t left = ctrl_counter;
        for (size_t o = 0; o < n; ++o)
        {
            if (left <= 0)
            {
                control();
                syncKlattFrame();
                left = int64_t (every);
            }
            --left;
            const auto f0Hz = (glottis.params.f0 > 1.0) ? glottis.params.f0 : this->f0;
            auto y = klatt.tick (f0Hz, gate);
            const auto hp = y - dc_x + dc_coef * dc_y;
            dc_x = y; dc_y = hp; y = hp;
            const auto av = abs (y);
            if (av >= lim_env) { lim_env = av; lim_hold = lim_hold_n; }
            else if (lim_hold > 0) --lim_hold;
            else lim_env *= lim_release;
            if (lim_env > 0.75) y *= 0.75 / lim_env;
            const auto al = abs (y);
            if (al > 0.85) y = js_sign (y) * (0.85 + 0.15 * tanh ((al - 0.85) / 0.15));
            out[o] = is_finite (y) ? float (y) : 0.0f;
        }
        ctrl_counter = left;
    }

    void HumanVoice::render (float* out, size_t n)
    {
        if (clarityMode >= 5 && clarityMode <= 7 && proven != nullptr && proven->engaged())
        {
            // set_gate() writes gate_target; the waveguide slews `gate` itself.
            // Proven playback must use the engine envelope directly.
            proven->render (out, n, gate_target);
            gate = gate_target;
            return;
        }
        if (clarityMode == 4)
        {
            renderKlatt (out, n);
            return;
        }
        const auto lungs = airway.lungs;
        const auto sub_node = airway.trachea.last_node();
        const auto supra = airway.pharynx.first_node();
        const auto gp = airway.glottis_port;
        const auto os = m == 0 ? size_t (1) : m;
        for (size_t o = 0; o < n; ++o)
        {
            out[o] = 0.0f;
            for (size_t s = 0; s < os; ++s)
            {
                if (ctrl_counter <= 0) control();
                --ctrl_counter;
                auto& net = airway.net;
                if (sub_node >= net.p.size() || supra >= net.p.size())
                    continue;
                p_lung_now += p_lung_step;
                if (! is_finite (p_lung_now)) p_lung_now = 0.0;
                net.set_fixed_pressure (lungs, p_lung_now);
                const auto dp = net.p[sub_node] - net.p[supra];
                const auto a = glottis.next (dp);
                if (clarityMode == 3)
                {
                    // LF pulse shapes the glottal *area* (same units as the parametric source).
                    // Inverting Bernoulli flow for area, or injecting Ug as ext_flow, rang the mesh.
                    const auto Rd = clamp (1.05 - 0.7 * glottis.params.effort, 0.35, 2.2);
                    const auto ugPeak = 1.0; // unit pulse; we rescale to amplitude below
                    const auto ug = lf.flow (glottis.cycle_phase(), glottis.f0_now, Rd, ugPeak);
                    const auto pulse = clamp (ug, 0.0, 1.6);
                    const auto chink = js_max (0.0, (glottis.params.abduction + glottis.params.chink)
                                                 * (1.0 - glottis.params.closure));
                    auto alf = glottis.params.amplitude * pulse * js_max (0.0, glottis.voicing) + chink;
                    if (! is_finite (alf)) alf = chink;
                    net.set_port_area (gp, js_max (0.0, alf));
                }
                else
                {
                    net.set_port_area (gp, is_finite (a) ? a : 0.0);
                }
                const auto y = net.step();
                if (dec.push (y))
                {
                    auto v = dec.value * output_gain;
                    const auto hp = v - dc_x + dc_coef * dc_y;
                    dc_x = v; dc_y = hp; v = hp;
                    const auto av = abs (v);
                    if (av >= lim_env) { lim_env = av; lim_hold = lim_hold_n; }
                    else if (lim_hold > 0) --lim_hold;
                    else lim_env *= lim_release;
                    if (lim_env > 0.75) v *= 0.75 / lim_env;
                    const auto al = abs (v);
                    if (al > 0.85) v = js_sign (v) * (0.85 + 0.15 * tanh ((al - 0.85) / 0.15));
                    if (clarityMode == 2)
                        v += hybridConsonant (1.0 / fs_out);
                    out[o] = is_finite (v) ? float (v) : 0.0f;
                }
            }
        }
    }

    void HumanVoice::control()
    {
        const auto dt = double (ctrl_every) / fs;
        double breathMul = 1.0;
        if (scoreActive && scoreGestures != nullptr && scoreCount > 0)
        {
            applyScore (dt);
            phonetics::Blended b[phonetics::N_CONTROL];
            // applyScore already advanced scoreT; blend at the pre-advance time would be better.
            // Re-blend at current (post-advance) is a half-step off and fine.
            phonetics::blendAll (scoreGestures, scoreCount, scoreT, b);
            const auto br = b[phonetics::sourceIndex (phonetics::SourceKey::Breath)];
            if (br.weight > 0.0 && std::isfinite (br.value))
                breathMul = br.value;
        }
        else
        {
            for (auto& sp : artic.speed) sp = 1.0;
            glottis.params.abduction = base_abduction;
            glottis.params.closure = base_closure;
        }

        // Articulatory effort scales the 2007 time constants (smaller tau = faster).
        // Default Effort 0.5 keeps the nominal Mermelstein/Birkholz rates.
        const auto userE = clamp (source.effort, 0.25, 1.0);
        artic.effortScale = 2.0 * userE * (clarityMode == 1 ? 1.15 : 1.0);
        if (clarityMode == 1)
        {
            airway.net.viscous_scale = 0.62;
            airway.noise_gain = { 3.4e-6, 4.0e-6, 5.2e-6 };
        }
        else
        {
            airway.net.viscous_scale = 1.0;
            airway.noise_gain = {};
        }
        artic.step (dt);
        if (! (scoreActive && scoreGestures != nullptr && scoreCount > 0))
        {
            glottis.params.abduction = base_abduction;
            glottis.params.closure = base_closure;
        }

        // Advance f0 first so formant tuning can use the current pitch.
        {
            const auto g = js_max (0.002, is_finite (source.glide) ? source.glide : 0.06);
            auto f0_target = is_finite (source.f0) ? source.f0 : 110.0;
            f0_target = clamp (f0_target, 1.0, 20000.0);
            if (f0 > 1e-6 && is_finite (f0))
                f0 *= pow (f0_target / f0, 1.0 - exp (-dt / g));
            else
                f0 = f0_target;
            if (! is_finite (f0)) f0 = f0_target;
            glottis.params.f0 = clamp (f0, 1.0, 20000.0);
        }
        const auto tune = clamp (source.formantTuning, 0.0, 1.0);
        if (tune > 1e-4)
        {
            const auto amount = tune * smoothstep (250.0, 550.0, f0);
            if (amount > 1e-4)
            {
                artic.x.jaw += amount * 0.32;
                artic.x.tb_y -= amount * 0.28;
                artic.x.larynx += amount * 0.18;
                artic.x.epilarynx += amount * clamp (source.effort, 0.0, 1.0) * 0.12;
            }
        }

        const auto x = artic.x;
        airway.apply (x, dt);
        if (enforce_contacts())
            airway.reapply (artic.x);
        airway.net.begin_block (size_t (ctrl_every));
        ctrl_counter = ctrl_every;
        const auto gate_goal = gate_target;
        const auto tau = gate_goal > gate ? 0.025 : 0.07;
        gate += (gate_goal - gate) * (1.0 - exp (-dt / tau));
        p_lung = source.pressure * CM_H2O * gate * breathMul;
        p_lung_step = (p_lung - p_lung_now) / double (ctrl_every);
        const auto pressureEffort = clamp ((abs (source.pressure * gate) - 3.0) / 10.0, 0.0, 1.0);
        const auto userEffort = clamp (source.effort, 0.0, 1.0);
        // Default userEffort 0.5 leaves the 0.2.0 pressure mapping unchanged.
        auto effort = clamp (pressureEffort + (userEffort - 0.5), 0.0, 1.0);
        if (gate > 0.75)
            effort_body = js_min (0.12, effort_body + dt * 0.08);
        else
            effort_body *= exp (-dt / 0.08);
        effort = clamp (effort + effort_body, 0.0, 1.0);
        glottis.params.effort = effort;

        // Source spectral tilt follows effort and pitch (brighter when louder or higher in the register).
        const auto s = source;
        const auto bright = s.brightness - DEFAULT_SOURCE.brightness;
        const auto by_tension = 0.4 * s.tension + 1.375 * js_max (0.0, s.tension - 0.6);
        const auto midF0 = 220.0 * airway.body.scale;
        const auto reg = clamp (log2 (js_max (40.0, f0) / js_max (40.0, midF0)), -1.5, 1.5);
        auto& gp = glottis.params;
        gp.open_quotient = clamp ((0.78 - 0.36 * s.tension) - 0.10 * (effort - 0.5) - 0.04 * js_max (0.0, reg), 0.25, 0.98);
        gp.closure_sharpness = clamp (by_tension + 0.8 * bright + 0.28 * (effort - 0.5) + 0.12 * reg, 0.0, 1.0);
        gp.skew = clamp (0.56 + 0.16 * s.tension + 0.2 * bright + 0.08 * (effort - 0.5), 0.3, 0.9);
    }

    bool HumanVoice::enforce_contacts()
    {
        const auto& tr = airway.tract;
        const auto k = tr.body_scale();
        const auto lim = -max_penetration * k;
        bool changed = false;
        auto fix = [&] (double d, Vec2 n, ArticKey kx, ArticKey ky) {
            if (! (d < lim)) return;
            changed = true;
            const auto depth = lim - d;
            const auto sx = (artic.x.get (kx) > 0.5 ? 1.0 / 0.86 : 1.0) / k;
            const auto sy = 1.0 / k;
            artic.x.get_mut (kx) += n.x * depth * sx;
            artic.x.get_mut (ky) += n.y * depth * sy;
            const auto ix = artic_index (kx), iy = artic_index (ky);
            const auto vn = artic.v[ix] * n.x / sx + artic.v[iy] * n.y / sy;
            if (vn < 0.0)
            {
                artic.v[ix] -= vn * n.x * sx;
                artic.v[iy] -= vn * n.y * sy;
            }
        };
        fix (tr.contact_tip, tr.contact_tip_n, ArticKey::TtX, ArticKey::TtY);
        fix (tr.contact_body, tr.contact_body_n, ArticKey::TbX, ArticKey::TbY);
        return changed;
    }

    void HumanVoice::setConsBand (double fc, double bw) noexcept
    {
        if (abs (fc - consFc) < 50.0 && consB0 != 0.0) return;
        consFc = fc;
        const auto fs = js_max (1000.0, fs_out);
        const auto q = clamp (fc / js_max (200.0, bw), 0.5, 8.0);
        const auto w = TAU * clamp (fc, 200.0, fs * 0.45) / fs;
        const auto cs = cos (w), sn = sin (w);
        const auto alpha = sn / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        consB0 = alpha / a0;
        consB1 = 0.0;
        consB2 = -alpha / a0;
        consA1 = (-2.0 * cs) / a0;
        consA2 = (1.0 - alpha) / a0;
    }

    double HumanVoice::hybridConsonant (double dt) noexcept
    {
        const auto lip = artic.x.lip_ap;
        double minA = 10.0;
        if (! airway.ph_min.empty() || ! airway.or_min.empty())
        {
            for (auto a : airway.ph_min) if (a > 0.0 && a < minA) minA = a;
            for (auto a : airway.or_min) if (a > 0.0 && a < minA) minA = a;
        }
        const auto k2 = airway.body.scale * airway.body.scale;
        const auto lipN = lip / js_max (1.0e-6, k2);
        const auto ttY = artic.x.tt_y;
        const auto tbY = artic.x.tb_y;
        const auto tbX = artic.x.tb_x;

        // Place from constriction: lips, tongue tip, or tongue body.
        double fc = 4000.0, bw = 800.0, placeGain = 1.0;
        if (lipN < 0.35)
        {
            fc = 5500.0; bw = 1800.0; placeGain = 1.7; // labial
        }
        else if (ttY > 8.6)
        {
            fc = 6200.0; bw = 900.0; placeGain = 1.35; // alveolar
        }
        else if (ttY > 8.0)
        {
            fc = 3200.0; bw = 1000.0; placeGain = 1.25; // postalveolar
        }
        else if (tbY > 7.4 && tbX < 2.8)
        {
            fc = 1800.0; bw = 550.0; placeGain = 1.1; // velar
        }
        setConsBand (fc, bw);

        const auto constr = js_max (1.0 - smoothstep (0.25 * k2, 0.95 * k2, minA),
                                    1.0 - smoothstep (0.20, 0.70, lipN));
        const auto abd = js_max (0.0, glottis.params.abduction);
        // Need a real constriction or abduction; do not hiss on a residual opening.
        auto env = constr * (0.05 + 1.8 * abd + 1.0 * constr);
        env *= clamp (gate, 0.0, 1.0);

        // Burst on a sudden opening (stop release).
        const auto dLip = lip - prevLip;
        const auto dA = minA - prevMinA;
        const auto wasClosed = prevLip < 0.55 || prevMinA < 0.85 * k2;
        prevLip = lip;
        prevMinA = minA;
        if (wasClosed && (dLip > 0.08 || dA > 0.08 * k2))
            burstEnv = js_max (burstEnv, 1.15 * placeGain * (0.6 + abd));
        burstEnv *= exp (-dt / 0.012);
        env = js_max (env, burstEnv * clamp (gate, 0.0, 1.0));

        if (env < 0.06)
            return 0.0;

        const auto noise = consRng.gauss();
        const auto y = consB0 * noise + consB1 * consX1 + consB2 * consX2 - consA1 * consY1 - consA2 * consY2;
        consX2 = consX1; consX1 = noise;
        consY2 = consY1; consY1 = is_finite (y) ? y : 0.0;
        // Mild highpass so the layer stays consonant-like and does not boom the vowels.
        const auto hp = consY1 - consHpX + 0.95 * consHpY;
        consHpX = consY1;
        consHpY = hp;
        // Mix is in output units (after output_gain / limiter).
        return 0.14 * placeGain * env * (is_finite (hp) ? hp : 0.0);
    }

}
