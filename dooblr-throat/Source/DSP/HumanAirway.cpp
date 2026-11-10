#include "HumanAirway.h"
#include "Physics.h"
#include <cmath>
#include <limits>

namespace dooblrthroat::dsp
{
    HumanAirway::HumanAirway (double fs, HumanBody bodyIn)
        : net (fs), tract (bodyIn), body (bodyIn)
    {
        const auto k = body.scale;
        const auto dx_min = (C_SOUND / fs) / 0.93;
        const auto ph_min_len = 8.7 * k * (0.55 + 0.45 * body.pharynx) - 1.6 * k;
        const auto or_min_len = 8.3 * k - 1.0 * k;
        n_ph = size_t (js_max (8.0, floor (ph_min_len / dx_min)));
        n_or = size_t (js_max (8.0, floor (or_min_len / dx_min)));

        const auto tr_len = 12.0 * k;
        trachea = net.add_tube (TubeSpec::make ("trachea", size_t (js_max (6.0, floor (tr_len / js_max (dx_min, 0.9 * k)))), tr_len,
                                                Area::Uniform (2.5 * k * k)).set_wall (WALL_TRACHEA));
        pharynx = net.add_tube (TubeSpec::make ("pharynx", n_ph, 8.8 * k, Area::Uniform (3.0 * k * k)).set_wall (WALL_SOFT).radiating (0.25).perimeter (1.25));
        oral = net.add_tube (TubeSpec::make ("oral", n_or, 8.3 * k, Area::Uniform (3.0 * k * k)).set_wall (WALL_SOFT).radiating (0.25).perimeter (1.5));
        const auto na_len = 11.4 * k;
        const auto n_na = size_t (js_max (8.0, floor (na_len / js_max (dx_min, 0.5 * k))));
        std::vector<double> na_areas (n_na);
        for (size_t i = 0; i < n_na; ++i)
        {
            const auto u = (double (i) + 0.5) / double (n_na);
            auto a = u < 0.18 ? 2.6 : u < 0.75 ? 2.0 - 0.6 * (u - 0.18) : u < 0.9 ? 1.4 : 1.0;
            na_areas[i] = a * k * k;
        }
        nasal = net.add_tube (TubeSpec::make ("nasal", n_na, na_len, Area::Profile (std::move (na_areas))).set_wall (WALL_NASAL).perimeter (3.2).viscous (2.5));
        fossa = net.add_tube (TubeSpec::make ("piriform", 3, 2.0 * k, Area::Uniform (0.9 * k * k)).set_wall (WALL_SOFT).perimeter (1.3));

        lungs = net.add_pressure_node ("lungs");
        const auto tr_a = 2.5 * k * k;
        net.add_port (lungs, trachea.first_node(), PortSpec::make (tr_a, 1.0).kinetic (0.0, 0.0).resistance ((0.5 * RHO * C_SOUND) / tr_a));
        glottis_port = net.add_port (trachea.last_node(), pharynx.first_node(), PortSpec::make (0.0, 0.3 * k).kinetic (0.37, 1.0).slit (1.4 * k));
        net.connect (pharynx.last_node(), oral.first_node());
        velum_port = net.add_port (pharynx.last_node(), nasal.first_node(), PortSpec::make (0.0, 1.0 * k).kinetic (0.3, 0.8));
        const auto fossa_node = pharynx.node (size_t (js_min (double (n_ph - 1), js_round ((2.6 / 8.8) * double (n_ph)))));
        net.connect (fossa_node, fossa.first_node());
        const auto sinus = net.add_cavity ("sinus", 14.0 * k * k * k, std::nullopt);
        net.add_port (nasal.node (size_t (js_round (double (n_na) * 0.45))), sinus, PortSpec::make (0.12 * k * k, 0.5 * k).kinetic (0.0, 0.0).no_half_sections());
        rad_lips = net.add_radiation (oral.last_node());
        rad_nose = net.add_radiation (nasal.last_node());
        net.finalize();

        ph_areas.assign (n_ph, 0.0); or_areas.assign (n_or, 0.0);
        ph_min.assign (n_ph, 0.0); or_min.assign (n_or, 0.0);
        frame = HumanFrame::with_capacity();
        noise_list.reserve (8);
    }

    size_t HumanAirway::branch_of (Tube t, int i) const
    {
        if (i < 0) i = 0;
        const auto maxv = int (t.n) - 2;
        if (i > maxv) i = maxv < 0 ? 0 : maxv;
        return t.branch (size_t (i));
    }

    const HumanFrame& HumanAirway::apply (const HumanArtic& a, double dt)
    {
        if (dt > 0.0)
        {
            if (! has_base)
            {
                base_ph.assign (n_ph, 0.0); base_or.assign (n_or, 0.0);
                base_ph_min.assign (n_ph, 0.0); base_or_min.assign (n_or, 0.0);
                has_base = true;
            }
            base_ph = ph_areas; base_or = or_areas; base_ph_min = ph_min; base_or_min = or_min;
        }
        base_dt = dt;
        return commit (a, dt);
    }

    const HumanFrame& HumanAirway::reapply (const HumanArtic& a) { return commit (a, base_dt); }

    const HumanFrame& HumanAirway::commit (const HumanArtic& a, double dt)
    {
        tract.compute (a, frame);
        const auto lens = tract.resample (a, frame, ph_areas.data(), n_ph, or_areas.data(), n_or, ph_min.data(), or_min.data());
        if (dt > 0.0 && has_base)
        {
            const auto k2 = body.scale * body.scale;
            const auto open_r = max_area_rate * k2;
            const auto gain = area_rate_gain;
            const auto seal_r = seal_rate * k2;
            const auto lo = seal_lo * k2, hi = seal_hi * k2, ease = seal_ease * k2;
            auto close_r = [&] (double area) {
                if (area < ease) return seal_r * sqrt (js_max (0.0, area) / ease);
                const auto u = area <= lo ? 0.0 : area >= hi ? 1.0 : (area - lo) / (hi - lo);
                return seal_r + (open_r - seal_r) * u * u * (3.0 - 2.0 * u);
            };
            auto clamp_arr = [&] (std::vector<double>& cur, const std::vector<double>& old,
                                  std::vector<double>& mins, const std::vector<double>& old_mins) {
                for (size_t i = 0; i < cur.size(); ++i)
                {
                    const auto dlt = cur[i] - old[i];
                    if (dlt > 0.0) { const auto lim = (open_r + gain * cur[i]) * dt; if (dlt > lim) cur[i] = old[i] + lim; }
                    else { const auto lim = (close_r (old[i]) + gain * old[i]) * dt; if (dlt < -lim) cur[i] = old[i] - lim; }
                    const auto dm = mins[i] - old_mins[i];
                    if (dm > 0.0) { const auto lim_m = (open_r + gain * mins[i]) * dt; if (dm > lim_m) mins[i] = old_mins[i] + lim_m; }
                    else { const auto lim_m = (close_r (old_mins[i]) + gain * old_mins[i]) * dt; if (dm < -lim_m) mins[i] = old_mins[i] - lim_m; }
                    if (mins[i] > cur[i]) mins[i] = cur[i];
                    if (mins[i] < 0.0) mins[i] = 0.0;
                }
            };
            clamp_arr (ph_areas, base_ph, ph_min, base_ph_min);
            clamp_arr (or_areas, base_or, or_min, base_or_min);
        }
        ph_len = lens.first; or_len = lens.second;
        net.set_tube_targets (pharynx, ph_areas.data(), n_ph, &ph_len, ph_min.data());
        net.set_tube_targets (oral, or_areas.data(), n_or, &or_len, or_min.data());
        net.set_port_target (velum_port, frame.velum_port);
        update_noise (frame);
        has_frame = true;
        return frame;
    }

    void HumanAirway::update_noise (const HumanFrame& f)
    {
        const auto g = noise_gain;
        auto& list = noise_list;
        list.clear();
        list.push_back (NoiseSpec::make (glottis_port, branch_of (pharynx, 1), branch_of (trachea, int (trachea.n) - 2), g.glottis).band (3500.0, 300.0));
        auto min_a = std::numeric_limits<double>::infinity();
        int min_i = -1;
        for (size_t i = 0; i < n_ph + n_or; ++i)
        {
            const auto area = i < n_ph ? ph_min[i] : or_min[i - n_ph];
            if (area > 0.0 && area < min_a) { min_a = area; min_i = int (i); }
        }
        const auto k2 = body.scale * body.scale;
        const auto w_area = min_i >= 0 ? 1.0 - smoothstep (0.35 * k2, 0.9 * k2, min_a) : 0.0;
        if (w_area > 0.0)
        {
            const auto n_ph_i = int (n_ph), n_tot = int (n_ph + n_or);
            const auto dx_ph = ph_len / double (n_ph), dx_or = or_len / double (n_or);
            auto to_branch = [&] (int gi) -> int {
                if (gi < n_ph_i) return int (branch_of (pharynx, gi));
                if (gi == n_ph_i) return -1;
                return int (branch_of (oral, gi - n_ph_i));
            };
            const auto dx = min_i < n_ph_i ? dx_ph : dx_or;
            const auto off = int (js_max (1.0, js_round ((0.9 * body.scale) / dx)));
            const auto src = to_branch (std::min (min_i, n_tot - 2));
            const auto fwd = to_branch (std::min (n_tot - 2, min_i + off));
            const auto bwd = to_branch (std::max (0, min_i - off));
            if (src >= 0 && fwd >= 0 && bwd >= 0)
            {
                list.push_back (NoiseSpec::make (size_t (src), size_t (fwd), size_t (bwd), g.constriction * w_area).band (2600.0, 350.0));
                const auto s_min = min_i < n_ph_i ? (double (min_i) + 0.5) * dx_ph : ph_len + ((double (min_i - n_ph_i) + 0.5) * dx_or);
                const auto behind = (f.teeth_s - s_min) / body.scale;
                const auto w_dist = smoothstep (0.15, 0.5, behind) * (1.0 - smoothstep (2.6, 4.0, behind));
                const auto w_jet = 1.0 - smoothstep (0.2 * k2, 0.5 * k2, min_a);
                const auto wt = w_dist * w_jet;
                if (wt > 1e-3)
                {
                    const auto teeth_i = n_ph_i + int (js_min (double (n_or - 2), js_max (0.0, js_round ((f.teeth_s - ph_len) / dx_or))));
                    const auto tb = to_branch (teeth_i);
                    if (tb >= 0)
                        list.push_back (NoiseSpec::make (size_t (src), size_t (tb), size_t (bwd), g.teeth * wt).band (5000.0, 1200.0));
                }
            }
        }
        net.set_noise_sources (list.data(), list.size());
    }
}
