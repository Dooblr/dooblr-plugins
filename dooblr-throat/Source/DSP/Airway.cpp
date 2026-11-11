#include "Airway.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dooblrthroat::dsp
{
    AirwayNetwork::AirwayNetwork (double fsIn)
        : fs (fsIn), t (1.0 / fsIn), inv_t (fsIn)
    {
        noise.reserve (16);
        noise_next.reserve (16);
    }

    Tube AirwayNetwork::add_tube (TubeSpec spec)
    {
        Tube t { tubes.size(), spec.n, n_nodes, b_a.size() };
        tubes.push_back (t);
        tube_names.push_back (spec.name);
        tube_length.push_back (spec.length);
        n_nodes += spec.n;
        for (size_t i = 0; spec.n > 0 && i + 1 < spec.n; ++i)
            push_branch (t.node (i), t.node (i + 1), KIND_SERIES, std::nullopt);
        tube_specs.push_back (std::move (spec));
        return t;
    }

    size_t AirwayNetwork::connect (size_t a, size_t b) { return push_branch (a, b, KIND_SERIES, std::nullopt); }
    size_t AirwayNetwork::add_port (size_t a, size_t b, PortSpec spec) { return push_branch (a, b, KIND_PORT, spec); }

    size_t AirwayNetwork::add_cavity (const char* name, double volume, std::optional<WallParams> wall)
    {
        const auto len = cbrt (volume);
        const auto t = add_tube (TubeSpec::make (name, 1, len, Area::Uniform (volume / len)).set_wall (wall).perimeter (1.2));
        return t.first_node();
    }

    size_t AirwayNetwork::add_pressure_node (const char* name)
    {
        const auto t = add_tube (TubeSpec::make (name, 1, 1.0, Area::Uniform (100.0)));
        pending_fixed.push_back (t.first_node());
        return t.first_node();
    }

    size_t AirwayNetwork::add_radiation (size_t node)
    {
        rad_nodes.push_back (node);
        return rad_nodes.size() - 1;
    }

    size_t AirwayNetwork::push_branch (size_t a, size_t b, uint8_t kind, std::optional<PortSpec> spec)
    {
        b_a.push_back (a); b_b.push_back (b); b_kind.push_back (kind); port_specs.push_back (std::move (spec));
        return b_a.size() - 1;
    }

    void AirwayNetwork::finalize()
    {
        if (finalized) return;
        finalized = true;
        const auto n = n_nodes;
        const auto b = b_a.size();
        n_branches = b;
        auto zf = [] (size_t len) { return std::vector<double> (len, 0.0); };
        p = zf (n); area = zf (n); area_target = zf (n); area_step = zf (n);
        amin = zf (n); amin_target = zf (n); amin_step = zf (n);
        is_junction.assign (n, 0); inv_lsum = zf (n); inv_amin = zf (n);
        dx = zf (n); dx_target = zf (n); dx_step = zf (n); vol = zf (n); inv_a = zf (n);
        half_l = zf (n); half_r = zf (n); sqrt_a = zf (n); flow_sum = zf (n); ext_flow = zf (n);
        fixed.assign (n, 0); wall_m = zf (n); wall_r = zf (n); wall_k = zf (n); wall_perim = zf (n);
        wall_rad = zf (n); has_wall.assign (n, 0); wall_y = zf (n); wall_v = zf (n); wall_u = zf (n);
        node_tube.assign (n, 0); visc_f = zf (n); rad_of.assign (n, -1);
        for (size_t ti = 0; ti < tubes.size(); ++ti)
        {
            const auto t = tubes[ti];
            const auto& spec = tube_specs[ti];
            const auto dxv = spec.length / double (spec.n);
            for (size_t i = 0; i < t.n; ++i)
            {
                const auto k = t.node (i);
                const auto a = spec.area.at (i);
                area[k] = area_target[k] = amin[k] = amin_target[k] = a;
                dx[k] = dx_target[k] = dxv;
                node_tube[k] = t.index;
                visc_f[k] = spec.viscous_factor;
                if (spec.wall)
                {
                    has_wall[k] = 1;
                    wall_m[k] = spec.wall->mass;
                    wall_r[k] = spec.wall->resistance;
                    wall_k[k] = spec.wall->stiffness;
                }
                wall_perim[k] = spec.perimeter_factor * 2.0 * sqrt (PI);
                wall_rad[k] = spec.wall_radiation;
            }
        }
        for (auto k : pending_fixed) fixed[k] = 1;
        u = zf (b); mom = zf (b); l_prev = zf (b);
        std::vector<size_t> degree (n, 0);
        for (size_t j = 0; j < b; ++j) { degree[b_a[j]]++; degree[b_b[j]]++; }
        for (size_t k = 0; k < n; ++k) is_junction[k] = degree[k] >= 3 ? 1 : 0;
        nbr_start.assign (n + 1, 0);
        for (size_t k = 0; k < n; ++k) nbr_start[k + 1] = nbr_start[k] + degree[k];
        nbr_list.assign (nbr_start[n], 0);
        auto fill_n = nbr_start;
        fill_n.resize (n);
        for (size_t j = 0; j < b; ++j)
        {
            const auto a = b_a[j], bb = b_b[j];
            nbr_list[fill_n[a]++] = bb;
            nbr_list[fill_n[bb]++] = a;
        }
        spill = zf (n); vol_src1 = zf (n); vol_src2 = zf (n);
        vol_lp_coef = 1.0 - exp ((-2.0 * PI * volume_source_hz) / fs);
        psrc = zf (b); port_area = zf (b); port_area_target = zf (b); port_area_step = zf (b);
        port_len = zf (b); port_kin = zf (b); port_kex = zf (b); port_slit = zf (b);
        port_half.assign (b, 0); port_extra_r = zf (b); branch_area = zf (b);
        for (size_t j = 0; j < b; ++j)
        {
            if (port_specs[j])
            {
                const auto& s = *port_specs[j];
                port_area[j] = port_area_target[j] = s.area;
                port_len[j] = s.length; port_kin[j] = s.k_entrance; port_kex[j] = s.k_exit;
                port_slit[j] = s.slit_width; port_half[j] = s.include_half_sections ? 1 : 0;
                port_extra_r[j] = s.extra_resistance;
            }
        }
        const auto nr = rad_nodes.size();
        rad_ul = zf (nr); rad_l_prev = zf (nr); rad_u = zf (nr);
        for (size_t r = 0; r < nr; ++r)
            if (rad_nodes[r] < rad_of.size())
                rad_of[rad_nodes[r]] = int32_t (r);
        for (size_t k = 0; k < n; ++k)
        {
            const auto a = js_max (area[k], min_node_area);
            vol[k] = a * dx[k];
            inv_a[k] = 1.0 / js_max (area[k], 1e-6);
        }
        check_cfl();
    }

    double AirwayNetwork::check_cfl() const
    {
        auto min_dx = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < n_nodes; ++k)
            if (! fixed[k]) min_dx = js_min (min_dx, dx_target[k]);
        return (C_SOUND * t) / min_dx;
    }

    void AirwayNetwork::set_tube_targets (Tube tu, const double* areas, size_t, const double* length, const double* min_areas)
    {
        for (size_t i = 0; i < tu.n; ++i)
        {
            const auto a = js_max (0.0, areas[i]);
            area_target[tu.node (i)] = a;
            amin_target[tu.node (i)] = min_areas ? js_max (0.0, js_min (a, min_areas[i])) : a;
        }
        if (length)
        {
            tube_length[tu.index] = *length;
            const auto d = *length / double (tu.n);
            for (size_t i = 0; i < tu.n; ++i) dx_target[tu.node (i)] = d;
        }
    }

    void AirwayNetwork::set_node_area_target (size_t node, double a)
    {
        if (node >= area_target.size() || node >= amin_target.size()) return;
        area_target[node] = amin_target[node] = js_max (0.0, a);
    }

    void AirwayNetwork::set_port_target (size_t branch, double a)
    {
        if (branch < port_area_target.size())
            port_area_target[branch] = js_max (0.0, a);
    }

    void AirwayNetwork::set_port_area (size_t branch, double a) noexcept
    {
        if (branch >= port_area.size()) return;
        port_area[branch] = a < 0.0 ? 0.0 : a;
        if (branch < port_area_target.size()) port_area_target[branch] = port_area[branch];
        if (branch < port_area_step.size()) port_area_step[branch] = 0.0;
    }

    void AirwayNetwork::set_fixed_pressure (size_t node, double pv) noexcept
    {
        if (node < p.size()) p[node] = is_finite (pv) ? pv : 0.0;
    }

    void AirwayNetwork::snap_to_targets()
    {
        for (size_t k = 0; k < n_nodes; ++k)
        {
            area[k] = area_target[k]; amin[k] = amin_target[k]; dx[k] = dx_target[k];
            vol[k] = js_max (area[k], min_node_area) * dx[k];
            inv_a[k] = 1.0 / js_max (area[k], 1e-6);
            area_step[k] = amin_step[k] = dx_step[k] = 0.0;
        }
        for (size_t j = 0; j < n_branches; ++j)
            if (b_kind[j] == KIND_PORT) { port_area[j] = port_area_target[j]; port_area_step[j] = 0.0; }
    }

    void AirwayNetwork::begin_block (size_t samples)
    {
        const auto inv = 1.0 / js_max (1.0, double (samples));
        double max_p = 0.0;
        for (size_t k = 0; k < n_nodes; ++k)
        {
            area_step[k] = (area_target[k] - area[k]) * inv;
            amin_step[k] = (amin_target[k] - amin[k]) * inv;
            dx_step[k] = (dx_target[k] - dx[k]) * inv;
            if (! fixed[k]) { const auto ap = abs (p[k]); if (ap > max_p) max_p = ap; }
        }
        if (max_p < 1e-9)
        {
            double max_u = 0.0;
            for (size_t j = 0; j < n_branches; ++j) max_u = js_max (max_u, abs (u[j]));
            if (max_u < 1e-9) flush_state();
        }
        for (size_t j = 0; j < n_branches; ++j)
            if (b_kind[j] == KIND_PORT) port_area_step[j] = (port_area_target[j] - port_area[j]) * inv;
    }

    void AirwayNetwork::set_noise_sources (const NoiseSpec* list, size_t n)
    {
        noise_next.clear();
        for (size_t i = 0; i < n; ++i)
        {
            const auto& s = list[i];
            const NoiseSource* prev = nullptr;
            for (auto& o : noise) if (o.src == s.src && o.fwd == s.fwd) { prev = &o; break; }
            NoiseSource ns;
            ns.src = s.src; ns.fwd = s.fwd; ns.bwd = s.bwd; ns.gain = s.gain;
            ns.hp = prev ? prev->hp : 0.0; ns.lp1 = prev ? prev->lp1 : 0.0; ns.lp2 = prev ? prev->lp2 : 0.0;
            ns.lp_coef = 1.0 - exp ((-2.0 * PI * s.lowpass_hz) / fs);
            ns.hp_coef = exp ((-2.0 * PI * s.highpass_hz) / fs);
            noise_next.push_back (ns);
        }
        noise.swap (noise_next);
    }

    void AirwayNetwork::flush_state()
    {
        for (size_t k = 0; k < n_nodes; ++k)
        {
            if (! fixed[k]) p[k] = 0.0;
            wall_v[k] = 0.0;
            if (abs (wall_y[k]) < 1e-12) wall_y[k] = 0.0;
        }
        std::fill (u.begin(), u.end(), 0.0);
        std::fill (mom.begin(), mom.end(), 0.0);
        std::fill (rad_ul.begin(), rad_ul.end(), 0.0);
    }

    void AirwayNetwork::reset()
    {
        std::fill (p.begin(), p.end(), 0.0);
        std::fill (u.begin(), u.end(), 0.0);
        std::fill (mom.begin(), mom.end(), 0.0);
        std::fill (l_prev.begin(), l_prev.end(), 0.0);
        std::fill (wall_y.begin(), wall_y.end(), 0.0);
        std::fill (wall_v.begin(), wall_v.end(), 0.0);
        std::fill (rad_ul.begin(), rad_ul.end(), 0.0);
        std::fill (rad_l_prev.begin(), rad_l_prev.end(), 0.0);
        rad_prev_total = 0.0;
    }

    void AirwayNetwork::compute_noise()
    {
        const auto scale = noise_scale * sqrt (96000.0 / fs);
        const auto sat = 12000.0 * 12000.0 - RE_CRIT * RE_CRIT;
        for (auto& s : noise)
        {
            if (s.src >= branch_area.size() || s.src >= u.size()) continue;
            const auto a = branch_area[s.src];
            const auto uu = u[s.src];
            auto amp = 0.0;
            if (a > closed_area)
            {
                const auto re = (abs (uu) * 2.0) / (NU * sqrt (PI * a));
                if (re > RE_CRIT)
                {
                    const auto x = re * re - RE_CRIT * RE_CRIT;
                    amp = (s.gain * scale * x) / (1.0 + x / sat);
                }
            }
            const auto w = rng.bipolar() * 1.732;
            s.hp += (1.0 - s.hp_coef) * (w - s.hp);
            const auto hp_out = w - s.hp;
            s.lp1 += s.lp_coef * (hp_out - s.lp1);
            s.lp2 += s.lp_coef * (s.lp1 - s.lp2);
            s.level = amp;
            if (amp == 0.0) continue;
            const auto v = amp * s.lp2;
            if (uu >= 0.0)
            {
                if (s.fwd < psrc.size()) psrc[s.fwd] += v;
            }
            else if (s.bwd < psrc.size())
                psrc[s.bwd] -= v;
        }
    }

    double AirwayNetwork::step()
    {
        // Refuse to run if the graph arrays disagree — a torn rebuild or a
        // missed finalize would otherwise turn a node index into a wild pointer.
        if (p.size() < n_nodes || area.size() < n_nodes || area_step.size() < n_nodes
            || amin.size() < n_nodes || amin_step.size() < n_nodes || amin_target.size() < n_nodes
            || dx.size() < n_nodes || dx_step.size() < n_nodes || inv_a.size() < n_nodes
            || inv_amin.size() < n_nodes || sqrt_a.size() < n_nodes || half_l.size() < n_nodes
            || half_r.size() < n_nodes || wall_perim.size() < n_nodes || visc_f.size() < n_nodes
            || vol.size() < n_nodes || inv_lsum.size() < n_nodes || flow_sum.size() < n_nodes
            || ext_flow.size() < n_nodes || rad_of.size() < n_nodes || fixed.size() < n_nodes
            || has_wall.size() < n_nodes || b_a.size() < n_branches || b_b.size() < n_branches
            || b_kind.size() < n_branches || u.size() < n_branches || mom.size() < n_branches
            || l_prev.size() < n_branches || psrc.size() < n_branches
            || branch_area.size() < n_branches)
            return 0.0;

        const auto n = n_nodes, nb = n_branches;
        const auto tt = t, invt = inv_t, min_a = min_node_area;
        const auto sq = squeeze_area;
        const auto vol_on = enable_volume_source;
        const auto rad_slots = std::min ({ rad_ul.size(), rad_l_prev.size(), rad_u.size() });
        auto valid_rad = [rad_slots] (int32_t r) noexcept -> int32_t
        {
            return (r >= 0 && size_t (r) < rad_slots) ? r : int32_t (-1);
        };
        if (vol_on) std::fill (spill.begin(), spill.end(), 0.0);

        for (size_t k = 0; k < n; ++k)
        {
            auto a = area[k] + area_step[k]; if (a < 0.0) a = 0.0; area[k] = a;
            auto am = amin[k] + amin_step[k]; if (am < 0.0) am = 0.0;
            const auto amk = am > a ? a : am; amin[k] = amk;
            dx[k] += dx_step[k];
            const auto d = dx[k];
            const auto ia = 1.0 / (a > 1e-6 ? a : 1e-6);
            inv_a[k] = ia;
            inv_amin[k] = 1.0 / (amk > 1e-6 ? amk : 1e-6);
            const auto sa = sqrt (a);
            sqrt_a[k] = sa;
            half_l[k] = d * ia;
            half_r[k] = wall_perim[k] * d * sa * ia * ia * visc_f[k];
            const auto v = (a > min_a ? a : min_a) * d;
            if (vol_on)
            {
                const auto raw = -(v - vol[k]) * invt;
                const auto ae = amin[k] < a ? amin[k] : a;
                if (raw != 0.0 && ae < sq)
                {
                    const auto tu = ae / sq;
                    const auto keep = tu * tu * (3.0 - 2.0 * tu);
                    spill[k] += raw * keep;
                    const auto sp = raw * (1.0 - keep);
                    const auto s0 = (k < nbr_start.size()) ? nbr_start[k] : 0;
                    const auto s1raw = (k + 1 < nbr_start.size()) ? nbr_start[k + 1] : s0;
                    const auto s1 = s1raw <= nbr_list.size() ? s1raw : nbr_list.size();
                    size_t n_open = 0;
                    for (auto e = s0; e < s1; ++e)
                    {
                        const auto m = nbr_list[e];
                        if (m >= n) continue;
                        if (js_min (area[m], amin[m]) >= sq || amin_target[m] >= sq) ++n_open;
                    }
                    if (n_open > 0)
                    {
                        const auto share = sp / double (n_open);
                        for (auto e = s0; e < s1; ++e)
                        {
                            const auto m = nbr_list[e];
                            if (m >= n) continue;
                            if (js_min (area[m], amin[m]) >= sq || amin_target[m] >= sq) spill[m] += share;
                        }
                    }
                }
                else spill[k] += raw;
            }
            vol[k] = v;
            inv_lsum[k] = 0.0;
            flow_sum[k] = ext_flow[k];
            ext_flow[k] = 0.0;
        }

        if (vol_on)
        {
            const auto c = vol_lp_coef;
            for (size_t k = 0; k < n; ++k)
            {
                vol_src1[k] += c * (spill[k] - vol_src1[k]);
                const auto s1 = vol_src1[k];
                vol_src2[k] += c * (s1 - vol_src2[k]);
                flow_sum[k] += vol_src2[k];
            }
        }

        std::fill (psrc.begin(), psrc.end(), 0.0);
        if (! noise.empty()) compute_noise();

        const auto visc = sqrt ((RHO * MU * viscous_omega) / 2.0) * viscous_scale;
        const auto closed = closed_area;
        const auto half_rho = 0.5 * RHO;
        const auto enable_energy_safe = energy_safe;
        const auto symmetric = symmetric_kinetic;

        for (size_t j = 0; j < nb; ++j)
        {
            const auto a = b_a[j], b = b_b[j];
            if (a >= n || b >= n) continue;
            double l, r, kf, kb, a_eff;
            if (b_kind[j] == KIND_SERIES)
            {
                const auto aa = area[a], ab = area[b], ma = amin[a], mb = amin[b];
                a_eff = ma < mb ? ma : mb;
                if ((aa < ab ? aa : ab) < closed || a_eff < closed)
                {
                    u[j] = 0.0; mom[j] = 0.0; branch_area[j] = a_eff; continue;
                }
                const auto ia = inv_a[a], ib = inv_a[b];
                l = half_rho * (half_l[a] + half_l[b]);
                r = 0.5 * visc * (half_r[a] + half_r[b]);
                const auto ima = inv_amin[a], imb = inv_amin[b];
                const auto ef = ima - ib, eb = imb - ia;
                kf = ef > 0.0 ? half_rho * ef * ef : 0.0;
                kb = eb > 0.0 ? half_rho * eb * eb : 0.0;
                const auto cf = imb - ia, cb = ima - ib;
                if (cf > 0.0) kf += 0.1 * half_rho * cf * cf;
                if (cb > 0.0) kb += 0.1 * half_rho * cb * cb;
            }
            else
            {
                port_area[j] += port_area_step[j];
                auto ap = port_area[j];
                if (ap < 0.0) { port_area[j] = 0.0; ap = 0.0; }
                a_eff = ap;
                if (ap < closed) { u[j] = 0.0; mom[j] = 0.0; branch_area[j] = ap; continue; }
                const auto ip = 1.0 / ap;
                const auto len = port_len[j];
                auto ll = RHO * len * ip;
                if (port_half[j]) ll += half_rho * (half_l[a] + half_l[b]);
                l = ll;
                const auto w = port_slit[j];
                auto rr = w > 0.0 ? (12.0 * MU * len * w * w) * ip * ip * ip
                                  : visc * 2.0 * sqrt (PI) * len * sqrt (ap) * ip * ip;
                rr += port_extra_r[j];
                r = rr;
                const auto ex_f = ip - inv_a[b], ex_b = ip - inv_a[a];
                const auto kin = port_kin[j] * half_rho * ip * ip;
                kf = kin + port_kex[j] * half_rho * (ex_f > 0.0 ? ex_f * ex_f : 0.0);
                kb = kin + port_kex[j] * half_rho * (ex_b > 0.0 ? ex_b * ex_b : 0.0);
            }
            branch_area[j] = a_eff;
            if (symmetric)
            {
                const auto km = kf > kb ? kf : kb;
                kf = kb = km;
            }
            const auto lp = l_prev[j];
            const auto m0 = (enable_energy_safe && l < lp) ? l * u[j] : mom[j];
            const auto c = p[a] - p[b] + psrc[j] + m0 * invt;
            const auto bq = l * invt + r;
            double uu;
            if (c >= 0.0)
                uu = kf > 0.0 ? (2.0 * c) / (bq + sqrt (bq * bq + 4.0 * kf * c)) : c / bq;
            else if (kb > 0.0)
                uu = (2.0 * c) / (bq + sqrt (bq * bq - 4.0 * kb * c));
            else
                uu = c / bq;
            u[j] = uu; mom[j] = l * uu; l_prev[j] = l;
            flow_sum[a] -= uu; flow_sum[b] += uu;
            if (is_junction[a] || is_junction[b])
            {
                const auto il = 1.0 / l;
                inv_lsum[a] += il; inv_lsum[b] += il;
            }
        }

        auto wall_rad_sum = 0.0;
        for (size_t k = 0; k < n; ++k)
        {
            if (! has_wall[k]) continue;
            const auto m = wall_m[k], r = wall_r[k];
            const auto vv = ((m * invt - 0.5 * r) * wall_v[k] + p[k] - wall_k[k] * wall_y[k]) / (m * invt + 0.5 * r);
            wall_v[k] = vv;
            wall_y[k] += tt * vv;
            const auto sa = area[k] > min_a ? sqrt_a[k] : sqrt (min_a);
            const auto uw = wall_perim[k] * sa * dx[k] * vv;
            wall_u[k] = uw;
            flow_sum[k] -= uw;
            wall_rad_sum += uw * wall_rad[k];
        }

        const auto t2 = tt * tt;
        auto rad_total = wall_rad_sum;
        const auto cl = closed_area;
        for (size_t k = 0; k < n; ++k)
        {
            if (fixed[k]) continue;
            if (area[k] < cl || amin[k] < cl)
            {
                const auto rr = valid_rad (rad_of[k]);
                if (rr >= 0) p[k] *= 0.98;
                else
                {
                    auto sum = 0.0; size_t cnt = 0;
                    const auto s0 = (k < nbr_start.size()) ? nbr_start[k] : 0;
                    const auto s1raw = (k + 1 < nbr_start.size()) ? nbr_start[k + 1] : s0;
                    const auto s1 = s1raw <= nbr_list.size() ? s1raw : nbr_list.size();
                    for (auto e = s0; e < s1; ++e)
                    {
                        const auto m = nbr_list[e];
                        if (m >= n) continue;
                        if (area[m] >= cl && amin[m] >= cl) { sum += p[m]; ++cnt; }
                    }
                    if (cnt > 0) p[k] += 0.02 * (sum / double (cnt) - p[k]);
                }
                if (rr >= 0)
                {
                    const auto r = size_t (rr);
                    rad_ul[r] *= 0.995;
                    rad_u[r] = rad_ul[r];
                    rad_total += rad_ul[r];
                }
                continue;
            }
            auto c = vol[k] / RHO_C2;
            if (is_junction[k] && enable_junction_floor)
            {
                const auto c_min = (t2 * inv_lsum[k]) / 2.4;
                if (c < c_min) c = c_min;
            }
            const auto r = valid_rad (rad_of[k]);
            if (r < 0)
                p[k] += (tt / c) * flow_sum[k];
            else
            {
                const auto ri = size_t (r);
                const auto aa = area[k] > 1e-4 ? area[k] : 1e-4;
                const auto lr = (8.0 * RHO) / (3.0 * PI * sqrt (PI * aa));
                const auto rr = (128.0 * RHO * C_SOUND) / (9.0 * PI * PI * aa);
                const auto p_old = p[k];
                const auto lp = rad_l_prev[ri];
                auto ul_prev = rad_ul[ri];
                if (lp > 0.0 && lr > lp) ul_prev *= lp / lr;
                rad_l_prev[ri] = lr;
                const auto h = tt / (2.0 * lr);
                const auto g = 0.5 / rr;
                const auto p_new = (p_old * (c * invt - 0.5 * h - g) + flow_sum[k] - ul_prev) / (c * invt + 0.5 * h + g);
                p[k] = p_new;
                const auto ul = ul_prev + h * (p_old + p_new);
                rad_ul[ri] = ul;
                const auto ur = 0.5 * (ul_prev + ul) + g * (p_new + p_old);
                rad_u[ri] = ur;
                rad_total += ur;
            }
        }
        const auto outv = (rad_total - rad_prev_total) * invt;
        rad_prev_total = rad_total;
        out = outv;
        return outv;
    }
}
