#include "Articulation.h"
#include <cstring>
#include <cmath>

namespace dooblrthroat::dsp
{
    const BodyMix BodyMix::MALE   { 0.0, 0.0 };
    const BodyMix BodyMix::FEMALE { 1.0, 0.0 };
    const BodyMix BodyMix::CHILD  { 0.0, 1.0 };

    double HumanArtic::get (ArticKey k) const noexcept
    {
        switch (k)
        {
            case ArticKey::Jaw: return jaw; case ArticKey::TbX: return tb_x; case ArticKey::TbY: return tb_y;
            case ArticKey::TtX: return tt_x; case ArticKey::TtY: return tt_y; case ArticKey::TtCurl: return tt_curl;
            case ArticKey::Root: return root; case ArticKey::LipAp: return lip_ap; case ArticKey::LipProt: return lip_prot;
            case ArticKey::LipWidth: return lip_width; case ArticKey::Labiodental: return labiodental;
            case ArticKey::Velum: return velum; case ArticKey::Larynx: return larynx;
            case ArticKey::Epilarynx: return epilarynx; case ArticKey::Ventricular: return ventricular;
            case ArticKey::Lateral: return lateral; case ArticKey::Dorsum: return dorsum;
        }
        return 0.0;
    }

    double& HumanArtic::get_mut (ArticKey k) noexcept
    {
        switch (k)
        {
            case ArticKey::Jaw: return jaw; case ArticKey::TbX: return tb_x; case ArticKey::TbY: return tb_y;
            case ArticKey::TtX: return tt_x; case ArticKey::TtY: return tt_y; case ArticKey::TtCurl: return tt_curl;
            case ArticKey::Root: return root; case ArticKey::LipAp: return lip_ap; case ArticKey::LipProt: return lip_prot;
            case ArticKey::LipWidth: return lip_width; case ArticKey::Labiodental: return labiodental;
            case ArticKey::Velum: return velum; case ArticKey::Larynx: return larynx;
            case ArticKey::Epilarynx: return epilarynx; case ArticKey::Ventricular: return ventricular;
            case ArticKey::Lateral: return lateral; case ArticKey::Dorsum: return dorsum;
        }
        return jaw;
    }

    std::array<double, N_ARTIC> HumanArtic::to_array() const
    {
        std::array<double, N_ARTIC> a {};
        for (size_t i = 0; i < N_ARTIC; ++i) a[i] = get (ARTIC_ALL[i]);
        return a;
    }

    HumanArtic HumanArtic::from_array (const double* a, size_t n)
    {
        auto out = NEUTRAL;
        for (size_t i = 0; i < N_ARTIC && i < n; ++i) out.set (ARTIC_ALL[i], a[i]);
        return out;
    }

    HumanArtic ArticPatch::apply_to (const HumanArtic& base) const
    {
        auto out = base;
        for (size_t i = 0; i < N_ARTIC; ++i)
            if (values[i]) out.set (ARTIC_ALL[i], *values[i]);
        return out;
    }

    void ArticPatch::merge (const ArticPatch& other)
    {
        for (size_t i = 0; i < N_ARTIC; ++i)
            if (other.values[i]) values[i] = other.values[i];
    }

    bool ArticPatch::is_empty() const
    {
        for (auto& x : values) if (x) return false;
        return true;
    }

    ArticPatch ArticPatch::blend (const std::pair<double, ArticPatch>* parts, size_t n)
    {
        ArticPatch out;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            double sum = 0.0, wsum = 0.0;
            for (size_t p = 0; p < n; ++p)
            {
                if (parts[p].first > 0.0 && parts[p].second.values[i])
                {
                    sum += parts[p].first * *parts[p].second.values[i];
                    wsum += parts[p].first;
                }
            }
            if (wsum > 0.0) out.values[i] = sum / wsum;
        }
        return out;
    }

    std::optional<ArticKey> artic_from_name (std::string_view s)
    {
        for (size_t i = 0; i < N_ARTIC; ++i)
            if (s == ARTIC_NAMES[i]) return ARTIC_ALL[i];
        return std::nullopt;
    }

    Vec2 rest_tip (double jaw, double tb_x, double tb_y)
    {
        const auto cx = -2.5, cy = 10.5, px = 6.45, py = 7.35;
        const auto ang = -jaw / 9.84;
        const auto dx = px - cx, dy = py - cy;
        const auto x = cx + dx * cos (ang) - dy * sin (ang);
        const auto y = cy + dx * sin (ang) + dy * cos (ang);
        return v (x + 0.3 * (tb_x - NEUTRAL.tb_x), y + 0.35 * (tb_y - NEUTRAL.tb_y));
    }

    BodyMix BodyMix::make (double female, double child)
    {
        auto c = [] (double x) { return is_finite (x) ? clamp (x, 0.0, 1.0) : 0.0; };
        return { c (female), c (child) };
    }

    void BodyMix::weights (double w[3]) const
    {
        const auto m = make (female, child);
        w[0] = (1.0 - m.child) * (1.0 - m.female);
        w[1] = (1.0 - m.child) * m.female;
        w[2] = m.child;
    }

    std::optional<BodyName> BodyMix::preset() const
    {
        double w[3]; weights (w);
        if (w[0] == 1.0) return BodyName::Male;
        if (w[1] == 1.0) return BodyName::Female;
        if (w[2] == 1.0) return BodyName::Child;
        return std::nullopt;
    }

    BodyName BodyMix::nearest() const
    {
        double w[3]; weights (w);
        size_t b = 0;
        if (w[1] > w[b]) b = 1;
        if (w[2] > w[b]) b = 2;
        return b == 0 ? BodyName::Male : b == 1 ? BodyName::Female : BodyName::Child;
    }

    HumanBody BodyMix::body() const
    {
        if (auto p = preset()) return body_of (*p);
        HumanBody h = body_of (nearest());
        h.scale = blend ([] (BodyName b) { return body_of (b).scale; });
        h.pharynx = blend ([] (BodyName b) { return body_of (b).pharynx; });
        h.tongue_radius = blend ([] (BodyName b) { return body_of (b).tongue_radius; });
        h.lip_width = blend ([] (BodyName b) { return body_of (b).lip_width; });
        return h;
    }

    ArticulatorBank::ArticulatorBank()
    {
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            speed[i] = 1.0;
            omega[i] = 2.0 * PI * ARTIC_SPECS[i].hz;
            z1[i] = z2[i] = x.get (ARTIC_ALL[i]);
        }
        const auto tip = rest_tip (NEUTRAL.jaw, NEUTRAL.tb_x, NEUTRAL.tb_y);
        x.tt_x = target.tt_x = tip.x;
        x.tt_y = target.tt_y = tip.y;
        z1[artic_index (ArticKey::TtX)] = z2[artic_index (ArticKey::TtX)] = tip.x;
        z1[artic_index (ArticKey::TtY)] = z2[artic_index (ArticKey::TtY)] = tip.y;
    }

    void ArticulatorBank::set_target (const ArticPatch& values)
    {
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            if (! values.values[i] || ! is_finite (*values.values[i])) continue;
            const auto s = ARTIC_SPECS[i];
            target.set (ARTIC_ALL[i], js_min (s.max, js_max (s.min, *values.values[i])));
        }
    }

    void ArticulatorBank::snap (const ArticPatch& values)
    {
        set_target (values);
        x = target;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            v[i] = 0.0;
            z1[i] = z2[i] = x.get (ARTIC_ALL[i]);
        }
    }

    void ArticulatorBank::step (double dt)
    {
        if (! (dt > 0.0) || ! is_finite (dt))
            return;
        if (tip_free)
        {
            const auto tip = rest_tip (x.jaw, x.tb_x, x.tb_y);
            target.tt_x = tip.x;
            target.tt_y = tip.y;
        }
        const auto e = (is_finite (effortScale) && effortScale > 1.0e-6) ? effortScale : 1.0;
        for (size_t i = 0; i < N_ARTIC; ++i)
        {
            const auto k = ARTIC_ALL[i];
            auto tgt = target.get (k);
            const auto s = ARTIC_SPECS[i];
            if (tgt < s.min) tgt = s.min;
            else if (tgt > s.max) tgt = s.max;
            effective.set (k, tgt);
            const auto sp = (speed[i] > 1.0e-6 && is_finite (speed[i])) ? speed[i] : 1.0;
            // Third-order cascade H(s) = 1/(1 + tau s)^3 (Birkholz 2007 eq. 2–3).
            // Exact per-stage update, no allocation, stable for any dt.
            const auto tau = js_max (0.003, ARTIC_TAU[i] / (sp * e));
            const auto decay = std::exp (-dt / tau);
            z1[i] = tgt + (z1[i] - tgt) * decay;
            z2[i] = z1[i] + (z2[i] - z1[i]) * decay;
            const auto prev = x.get (k);
            const auto next = z2[i] + (prev - z2[i]) * decay;
            v[i] = (next - prev) / dt;
            x.set (k, next);
        }
    }
}
