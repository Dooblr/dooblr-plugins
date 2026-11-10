#include "Anatomy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace dooblrthroat::dsp
{
    static constexpr double DEG = PI / 180.0;
    static constexpr double LAB_HANDOVER = 0.5;
    static constexpr double LAB_GAP = 0.3;
    static constexpr double LAB_FRONT_OPENS = 0.7;
    static constexpr double LAB_FRONT_RAMP = 0.1;
    static constexpr size_t ARC_POINTS = 32;
    static constexpr size_t MAX_CROSS = 64;
    static constexpr size_t POINTS = 256;

    Scratch Scratch::make()
    {
        Scratch s;
        auto reserve = [] (std::vector<Vec2>& v) { v.reserve (POINTS); };
        reserve (s.vel); reserve (s.vel_oral); reserve (s.vel_nasal);
        reserve (s.upper_lip_in); reserve (s.lower_lip_in); reserve (s.lip_out);
        reserve (s.root); reserve (s.arc_pts); reserve (s.blade); reserve (s.under);
        reserve (s.back); reserve (s.tip_pts);
        s.outer = Polyline::with_capacity (POINTS);
        s.inner = Polyline::with_capacity (POINTS);
        s.roof = Polyline::with_capacity (POINTS);
        s.outline = Polyline::with_capacity (POINTS);
        s.cum.reserve (POINTS);
        return s;
    }

    HumanFrame HumanFrame::with_capacity()
    {
        HumanFrame f;
        auto reserve = [] (std::vector<Vec2>& v) { v.reserve (POINTS); };
        reserve (f.outer); reserve (f.inner); reserve (f.tongue); reserve (f.velum);
        reserve (f.upper_lip); reserve (f.lower_lip); reserve (f.upper_teeth);
        reserve (f.lower_teeth); reserve (f.jaw_bone); reserve (f.epiglottis);
        reserve (f.nasal_cavity);
        f.grid.reserve (64);
        return f;
    }

    HumanVocalTract::HumanVocalTract (HumanBody bodyIn) : body (bodyIn), k (bodyIn.scale)
    {
        auto pt = [this] (double x, double y) { return p (x, y); };
        phar_wall = catmull_rom ({ pt (-1.0, -1.5), pt (-1.1, 1.5), pt (-1.2, 3.6), pt (-1.3, 5.6),
                                   pt (-1.38, 7.5), pt (-1.33, 8.9), pt (-1.05, 10.1), pt (-0.45, 10.9), pt (0.4, 11.3) }, 4);
        pns = pt (1.0, 9.75);
        palate = catmull_rom ({ pt (1.0, 9.6), pt (1.8, 9.92), pt (2.7, 10.18), pt (3.6, 10.3), pt (4.4, 10.25),
                                pt (5.1, 10.05), pt (5.7, 9.72), pt (6.15, 9.36), pt (6.5, 9.05), pt (6.78, 8.7),
                                pt (6.98, 8.3), pt (7.12, 7.95) }, 3);
        condyle = pt (-2.5, 10.5);
        polar_c = pt (2.6, 6.95);
        polar_c2 = pt (5.0, 6.85);
        nasal_cavity = catmull_rom ({ pns, pt (2.5, 10.8), pt (5.0, 11.2), pt (7.3, 10.6), pt (8.0, 10.1) }, 4);
    }

    Vec2 HumanVocalTract::p (double x, double y) const
    {
        const auto ph = body.pharynx;
        auto xx = x > 0.5 ? 0.5 + (x - 0.5) * 0.86 : x;
        auto yy = y < 7.0 ? 7.0 - (7.0 - y) * ph : y;
        return v (xx * k, yy * k);
    }

    Vec2 HumanVocalTractView::p (double x, double y) const
    {
        const auto ph = body.pharynx;
        auto xx = x > 0.5 ? 0.5 + (x - 0.5) * 0.86 : x;
        auto yy = y < 7.0 ? 7.0 - (7.0 - y) * ph : y;
        return v (xx * k, yy * k);
    }

    Vec2 HumanVocalTract::un_p (double x, double y) const
    {
        const auto ph = body.pharynx;
        auto xx = x / k, yy = y / k;
        if (xx > 0.5) xx = 0.5 + (xx - 0.5) / 0.86;
        if (yy < 7.0) yy = 7.0 - (7.0 - yy) / ph;
        return v (xx, yy);
    }

    HumanFrame HumanVocalTract::compute_new (const HumanArtic& a)
    {
        auto f = HumanFrame::with_capacity();
        compute (a, f);
        return f;
    }

    double HumanVocalTract::epilarynx_area (const HumanArtic& a, double s, double epi_len) const
    {
        const auto u = s / epi_len;
        const auto vent = 0.95 * (1.0 - 0.75 * clamp (a.ventricular, 0.0, 1.0));
        const auto top = lerp (1.9, 0.35, clamp (a.epilarynx, 0.0, 1.0));
        auto farea = [&] (double x) { if (x < 0.3) return 1.3; if (x < 0.5) return vent; return lerp (1.2, top, (x - 0.5) / 0.5); };
        const auto r = 0.03;
        auto area = farea (u);
        for (double u0 : { 0.3, 0.5 })
            if (abs (u - u0) < r)
                area = lerp (farea (u0 - r), farea (u0 + r), (u - (u0 - r)) / (2.0 * r));
        return area * k * k;
    }

    struct Projection { double s = 0, dist = 0, signed_d = 0, x = 0, y = 0; size_t seg = 0; };

    static Projection project_on (const Polyline& pl, const std::vector<double>& cum, double px, double py)
    {
        auto best = std::numeric_limits<double>::infinity();
        double bs = 0, bx = 0, by = 0, sign = 1; size_t bseg = 0;
        for (size_t i = 0; i + 1 < pl.xs.size(); ++i)
        {
            const auto ax = pl.xs[i], ay = pl.ys[i];
            const auto ex = pl.xs[i + 1] - ax, ey = pl.ys[i + 1] - ay;
            const auto l2 = ex * ex + ey * ey;
            if (l2 < 1e-12) continue;
            auto t = ((px - ax) * ex + (py - ay) * ey) / l2;
            t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
            const auto qx = ax + ex * t, qy = ay + ey * t;
            const auto d = hypot (px - qx, py - qy);
            if (d < best)
            {
                best = d; bs = cum[i] + t * sqrt (l2); bx = qx; by = qy;
                const auto cross = ex * (py - ay) - ey * (px - ax);
                sign = cross < 0.0 ? 1.0 : -1.0; bseg = i;
            }
        }
        return { bs, best, sign * best, bx, by, bseg };
    }

    static Vec2 point_along (const Polyline& pl, const std::vector<double>& cum, double s)
    {
        size_t i = 0;
        while (i + 2 < cum.size() && cum[i + 1] < s) ++i;
        const auto l = cum[i + 1] - cum[i];
        const auto u = l > 1e-9 ? clamp ((s - cum[i]) / l, 0.0, 1.0) : 0.0;
        return v (pl.xs[i] + (pl.xs[i + 1] - pl.xs[i]) * u, pl.ys[i] + (pl.ys[i + 1] - pl.ys[i]) * u);
    }

    static double signed_gap (const Polyline& pl, double px, double py)
    {
        auto best = std::numeric_limits<double>::infinity();
        bool inside = false;
        const auto& xs = pl.xs; const auto& ys = pl.ys;
        for (size_t i = 0; i + 1 < xs.size(); ++i)
        {
            const auto ax = xs[i], ay = ys[i], bx = xs[i + 1], by = ys[i + 1];
            const auto ex = bx - ax, ey = by - ay;
            const auto l2 = ex * ex + ey * ey;
            auto t = l2 > 1e-12 ? ((px - ax) * ex + (py - ay) * ey) / l2 : 0.0;
            t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
            const auto dx = px - ax - ex * t, dy = py - ay - ey * t;
            const auto d2 = dx * dx + dy * dy;
            if (d2 < best) best = d2;
            if ((ay > py) != (by > py) && px < ax + ((py - ay) * ex) / ey) inside = ! inside;
        }
        const auto d = sqrt (best);
        return inside ? -d : d;
    }

    static std::pair<double, Vec2> closest_approach (const Polyline& pl, const std::vector<double>& cum, const std::vector<Vec2>& pts)
    {
        auto d = std::numeric_limits<double>::infinity();
        size_t seg = 0;
        for (const auto& p : pts)
        {
            const auto pr = project_on (pl, cum, p.x, p.y);
            if (pr.signed_d < d) { d = pr.signed_d; seg = pr.seg; }
        }
        const auto ex = pl.xs[seg + 1] - pl.xs[seg], ey = pl.ys[seg + 1] - pl.ys[seg];
        auto l = hypot (ex, ey);
        if (l == 0.0 || is_nan (l)) l = 1.0;
        return { d, v (ey / l, -ex / l) };
    }

    std::pair<double, double> HumanVocalTract::resample (const HumanArtic& a, const HumanFrame& f,
        double* out_ph, size_t n_ph, double* out_or, size_t n_or, double* min_ph, double* min_or) const
    {
        const auto epi_len = 2.4 * k * body.pharynx;
        const auto& g = f.grid;
        const auto flare = 0.1 * epi_len;
        auto area_at = [&] (double s) -> double {
            if (s <= epi_len - flare) return epilarynx_area (a, s, epi_len);
            if (s <= epi_len) return lerp (epilarynx_area (a, s, epi_len), g[0].area, (s - (epi_len - flare)) / flare);
            if (s <= g[0].s) return g[0].area;
            size_t lo = 0, hi = g.size() - 1;
            if (s >= g[hi].s) return g[hi].area;
            while (hi - lo > 1)
            {
                const auto m = (lo + hi) >> 1;
                if (g[m].s <= s) lo = m; else hi = m;
            }
            const auto t = (s - g[lo].s) / js_max (1e-9, g[hi].s - g[lo].s);
            return lerp (g[lo].area, g[hi].area, t);
        };
        auto fill = [&] (double s0, double s1, double* out, size_t n, double* mins) {
            const auto d = (s1 - s0) / double (n);
            const int sub = 8;
            size_t gi = 0;
            for (size_t i = 0; i < n; ++i)
            {
                auto inv = 0.0; bool closed = false;
                auto mn = std::numeric_limits<double>::infinity();
                for (int jj = 0; jj < sub; ++jj)
                {
                    const auto area = area_at (s0 + d * (double (i) + (double (jj) + 0.5) / double (sub)));
                    if (area < mn) mn = area;
                    if (area <= 1e-5) { closed = true; break; }
                    inv += 1.0 / area;
                }
                const auto a0 = s0 + d * double (i);
                const auto a1 = s0 + d * (double (i) + 1.0) + (i == n - 1 ? 1e-6 : 0.0);
                while (gi < g.size() && g[gi].s < a0) ++gi;
                auto q = gi;
                while (q < g.size() && g[q].s <= a1)
                {
                    if (g[q].area < mn) mn = g[q].area;
                    if (g[q].area <= 1e-5) closed = true;
                    ++q;
                }
                out[i] = closed ? 0.0 : double (sub) / inv;
                if (mins) mins[i] = closed ? 0.0 : js_min (mn, out[i]);
            }
        };
        const auto sp = f.pharynx_length;
        fill (0.0, sp, out_ph, n_ph, min_ph);
        fill (sp, f.lip_s, out_or, n_or, min_or);
        return { sp, f.lip_s - sp };
    }

    void HumanVocalTract::compute (const HumanArtic& a, HumanFrame& f)
{
        const auto k = this->k;
        auto& sc = this->sc;
        const HumanVocalTractView view { k, this->body, this->condyle };
        const auto lar = a.larynx * k;
        const auto glottis = add(view.p(0.0, 0.0), v(0.0, lar));

        // ---- jaw (rotation about the condyle)
        const auto jaw_ref = view.p(6.95, 7.75);
        const auto jaw_r = dist(this->condyle, jaw_ref);
        const auto phi = -clamp(a.jaw, -0.2, 3.0) * k / jaw_r;
        auto j = [&] (double x, double y) { return rotate_about (view.p (x, y), view.condyle, phi); };

        // ---- velum flap from PNS; angle from port area
        const auto v_open = clamp(a.velum / 1.2, 0.0, 1.0);
        const auto v_ang = (194.0 + 46.0 * v_open) * DEG;
        const auto v_len = 2.8 * k;
        sc.vel.clear();
        sc.vel_oral.clear();
        sc.vel_nasal.clear();
        for (int i = 0; i <= 10; ++i) {
            const auto s = i / 10.0;
            const auto sag = sin(s * PI) * k * (0.3 * v_open - 0.12);
            const auto c = add(polar(this->pns, v_len * s, v_ang), v(0.0, -sag));
            const auto nrm = v(-sin(v_ang), cos(v_ang)); // oral-side normal (forward-down)
            const auto th = (0.48 - 0.2 * s) * k;
            sc.vel_oral.push_back(add(c, scale(nrm, th)));
            sc.vel_nasal.push_back(sub(c, scale(nrm, th)));
            sc.vel.push_back(c);
        }
        // the velum's oral surface runs into the hard palate's: its root sat below the palate's edge,
        // folding the roof back on itself there, and rays crossed the fold as the velum turned
        const auto pal0 = this->palate[0];
        for (size_t i = 0; i < sc.vel_oral.size(); ++i) {
            const auto u = i / 10.0 / 0.35;
            if (u < 1.0) {
                sc.vel_oral[i] = lerp_v(sc.vel_oral[i], pal0, (1.0 - u) * (1.0 - u));
            }
        }

        // ---- outer contour: pharyngeal wall up to just below the uvula, then velum oral surface
        // (tip→PNS), palate, incisors, upper lip
        auto& outer = sc.outer;
        outer.clear();
        // The port window's lid runs from the posterior wall up to the uvula's tip. It must slope
        // up: a lid leaving the wall above the tip left rays just under the tip to hit the wall far
        // behind, so the back of the oral tract jumped as the velum moved.
        const auto uv_y = sc.vel_oral[sc.vel_oral.size() - 1].y - 0.3 * k;
        for (size_t i = 0; i < this->phar_wall.size(); ++i) {
            const auto p = this->phar_wall[i];
            if (p.y < uv_y) {
                outer.push(p);
                continue;
            }
            // end exactly at that height: whole wall points popping in and out made the port
            // window, and every ray through it, jump as the velum moved
            if (i > 0) {
                const auto aa = this->phar_wall[i - 1];
                outer.push(lerp_v(aa, p, clamp((uv_y - aa.y) / js_max(1e-9, p.y - aa.y), 0.0, 1.0)));
            }
            break;
        }
        // bridge to the uvula tip (velopharyngeal port window)
        const auto roof_start = outer.len();
        for (int i = int (sc.vel_oral.size()) - 1; i >= 0; --i) {
            outer.push (sc.vel_oral[size_t (i)]);
        }
        outer.append(this->palate, true);
        const auto roof_end = outer.len() - 1;
        // upper lip
        const auto prot = clamp(a.lip_prot, -0.2, 1.6) * k;
        const auto spread_w = clamp(a.lip_width, 0.0, 1.0);
        const auto y_mid = (7.85 - 0.3 * a.jaw) * k;
        const auto lab = clamp(a.labiodental, 0.0, 1.0);
        const auto ap = a.lip_ap * k;
        const auto lip_front_x = view.p(8.15 + 0.12 * (1.0 - spread_w), 0.0).x + prot * 0.95;
        auto ul_edge = v(lip_front_x - 0.15 * k, y_mid + ap * 0.35 + lab * 0.35 * k);
        auto ll_edge = v(lip_front_x - 0.25 * k, y_mid - ap * 0.65);
        if (lab > 0.0) {
            // lower lip tucks under the upper incisor edge for /f v/
            const auto incisor = this->palate[this->palate.size() - 1];
            const auto target = v(incisor.x + 0.05 * k, incisor.y - js_max(a.lip_ap * k, -0.1));
            ll_edge = lerp_v(ll_edge, target, lab);
            ul_edge = lerp_v(ul_edge, v(incisor.x + 0.55 * k, incisor.y + 0.45 * k), lab * 0.7);
        }
        const auto ul_base = view.p(7.32, 8.35);
        bezier3(ul_base, add(ul_base, v(0.3 * k + prot * 0.4, 0.05 * k)), add(ul_edge, v(-0.35 * k, 0.12 * k)), ul_edge, 7, sc.upper_lip_in);
        outer.append(sc.upper_lip_in, false);

        // ---- tongue
        const auto r = this->body.tongue_radius * k;
        const auto b = view.p(a.tb_x, a.tb_y);
        const auto b0 = view.p(NEUTRAL.tb_x, NEUTRAL.tb_y);
        const auto vallecula = add(view.p(0.75, 3.3), v(0.45 * (b.x - b0.x) + a.root * k, 0.3 * (b.y - b0.y) + 0.55 * lar));
        const auto epi_base = add(view.p(0.3, 2.4), v(0.0, lar));
        const auto epi_tip = add(view.p(0.0, 4.5), v(0.35 * (vallecula.x - 0.75 * k), lar * 0.8 + 0.15 * (b.y - b0.y)));
        const auto th_b = 200.0 * DEG;
        const auto p_b = polar(b, r, th_b);
        const auto tan_b = v(sin(th_b), -cos(th_b));
        const auto h_r = dist(vallecula, p_b) / 3.0;
        bezier3(vallecula, add(vallecula, v(0.08 * k, h_r)), sub(p_b, scale(tan_b, h_r)), p_b, 9, sc.root);
        auto tip = view.p(a.tt_x, a.tt_y);
        // keep tip outside the body circle
        const auto d_t = dist(tip, b);
        if (d_t < r * 1.05) {
            tip = add(b, scale(norm(sub(tip, b)), r * 1.05));
        }
        double th_f;
        if (! tangent_angle (b, r, tip, true, th_f))
            th_f = atan2 (tip.y - b.y, tip.x - b.x);
        // arc runs from thB (200°) decreasing to thF
        while (th_f > th_b) {
            th_f -= 2.0 * PI;
        }
        while (th_f < th_b - 2.0 * PI) {
            th_f += 2.0 * PI;
        }
        // a fixed number of points: a count that changed with the angle re-spaced every vertex at once
        arc(b, r, th_b, th_f, ARC_POINTS, sc.arc_pts);
        const auto p_f = sc.arc_pts[sc.arc_pts.size() - 1];
        const auto tan_f = v(sin(th_f), -cos(th_f)); // direction of decreasing angle
        // blade: quadratic from the tangent point to the tip; curl lifts the tip and turns it back
        const auto bl = dist(p_f, tip);
        const auto curl = clamp(a.tt_curl, 0.0, 1.0);
        const auto ctrl = add(add(p_f, scale(tan_f, bl * 0.5)), v(0.0, (0.18 + 0.5 * curl) * k));
        // dorsum depression (clicks): hollow the tongue between the back (velar) contact at the top
        // of the body and the tip, leaving both contacts in place, so a sealed pocket expands
        bezier2(p_f, ctrl, tip, 7, sc.blade);
        if (a.dorsum > 0.0) {
            const auto x0 = b.x, x1 = tip.x;
            auto dip = [&] (Vec2 p) -> Vec2 {
                const auto u = (p.x - x0) / js_max(1e-6, x1 - x0);
                if (u <= 0.0 || u >= 1.0) {
                    return p;
                }
                const auto w = sin(PI * pow(u, 0.8));
                return add(p, v(0.0, -a.dorsum * 0.9 * k * w * w));
            };
            for (auto& q : sc.arc_pts) {
                q = dip(q);
            }
            const auto nb = sc.blade.size();
            for (size_t qi = 1; qi + 1 < nb; ++qi) { auto& q = sc.blade[qi];
                q = dip(q);
            }
        }
        // underside back to the frenulum / floor of mouth (jaw-carried)
        const auto fren = j(5.7, 6.15);
        bezier2(tip, add(tip, v(-0.3 * k - curl * 0.4 * k, -0.6 * k)), fren, 6, sc.under);
        const std::vector<Vec2> floor { fren, j (6.2, 6.25), j (6.45, 6.55) };
        // lower incisors (jaw)
        const auto li_tip = j(6.95, 7.75);
        const std::vector<Vec2> lower_teeth_pts { j (6.45, 6.55), j (6.72, 7.15), li_tip, j (7.12, 7.45) };
        // lower lip: from labial base of lower incisor to the edge
        const auto ll_base = j(7.12, 7.45);
        bezier3(ll_base, add(ll_base, v(0.25 * k + prot * 0.3, -0.05 * k)), add(ll_edge, v(-0.3 * k, -0.12 * k)), ll_edge, 7, sc.lower_lip_in);

        // acoustic inner wall: epiglottis (laryngeal surface) then the tongue root *above* the
        // epiglottis tip — the vallecula pocket is omitted so the wall is single-valued along the
        // pharyngeal rays (a pocket makes ray crossings flip discontinuously)
        auto& inner = sc.inner;
        inner.clear();
        inner.push (epi_base); inner.push (epi_tip);
        // The wall leaves the epiglottis tip upward, for where the tongue's back (root, then the
        // dorsum arc) first rises above it: with the tongue low and the larynx high the whole root
        // can sit below the tip, and a wall coming back down from it made the tip a peak that a
        // horizontal ray measured past (to the root) or not (to the epiglottis) — a jump.
        const auto y_root = epi_tip.y + 0.05 * k;
        sc.back.clear();
        sc.back.insert (sc.back.end(), sc.root.begin(), sc.root.end());
        if (sc.arc_pts.size() > 1) sc.back.insert (sc.back.end(), sc.arc_pts.begin() + 1, sc.arc_pts.end());
        const auto& back = sc.back;
        const auto root_len = sc.root.size();
        size_t start = sc.back.size() - 1;
        {
            size_t pos = sc.back.size();
            for (size_t qi = 0; qi < sc.back.size(); ++qi)
                if (sc.back[qi].y > y_root) { pos = qi; break; }
            if (pos != sc.back.size())
            {
                start = pos;
                if (pos > 0)
                {
                    const auto aa = back[pos - 1], bb = back[pos];
                    inner.push (lerp_v (aa, bb, clamp ((y_root - aa.y) / js_max (1e-9, bb.y - aa.y), 0.0, 1.0)));
                }
            }
        }
        if (start < root_len) {
            for (size_t i = start; i < root_len; ++i) {
                inner.push(sc.root[i]);
            }
            inner.append(sc.arc_pts, true);
        } else {
            {
                        std::vector<Vec2> rest;
                        if (start + 1 >= root_len)
                            rest.assign (sc.arc_pts.begin() + int (start - root_len + 1), sc.arc_pts.end());
                        inner.append (rest, true);
                    }
        }
        inner.append(sc.blade, true);
        inner.append(sc.under, true);
        inner.append(floor, true);
        inner.append(lower_teeth_pts, true);
        inner.append(sc.lower_lip_in, false);

        // ---- grid
        auto& grid = f.grid;
        grid.clear();
        const auto y_epi_top = glottis.y + 2.4 * k * this->body.pharynx;
        const auto y_phar_top = this->polar_c.y;
        const size_t n_ph = 12;
        int32_t split_index = -1;
        size_t teeth_idx = 0;
        {
            auto& outer = sc.outer;
            auto& inner = sc.inner;
            auto& tmp = sc.tmp;
            auto& cross_t = sc.cross_t;
            auto& cross_exit = sc.cross_exit;
            // A fan ray's inner surface. The ray starts in the tongue: the gap begins where it leaves it
            // (or, if (the tongue has moved off the origin, at the surface behind it: a fixed fallback
            // gap jumped as the surface crossed the origin). The gap is the air along the ray up to the
            // roof: tissue the ray passes through on the way (a curled tip with air beneath it) is taken
            // out by its thickness, so a layer that vanishes as the ray grazes it changes nothing.
            auto fan_inner = [&] (Vec2 o, Vec2 dir, double t_out, double* tmp, size_t tmpn) -> double {
                const auto n = ray_crossings (o.x, o.y, dir.x, dir.y, inner, 0.0, t_out + 1.5 * k, cross_t.data(), cross_exit.data(), cross_t.size());
                // sort the crossings along the ray (there are only a few)
                for (size_t i = 1; i < n; ++i) {
                    const auto t = cross_t[i]; const auto e = cross_exit[i]; (void) e;
                    auto jj = int (i) - 1;
                    while (jj >= 0 && cross_t[jj] > t) {
                        cross_t[jj + 1] = cross_t[jj];
                        cross_exit[jj + 1] = cross_exit[jj];
                        jj -= 1;
                    }
                    cross_t[(jj + 1)] = t;
                    cross_exit[(jj + 1)] = e;
                }
                double t_in;
                size_t i = 0;
                if (n > 0 && cross_exit[0]) {
                    t_in = cross_t[0];
                    i = 1;
                } else {
                    const auto back = 2.5 * k;
                    const auto n_back = ray_polyline(o.x, o.y, -dir.x, -dir.y, inner, 0.0, back, tmp, tmpn);
                    auto tb = back;
                    for (size_t q = 0; q < n_back; ++q) {
                        tb = js_min(tb, tmp[q]);
                    }
                    t_in = -tb;
                }
                auto tissue = 0.0;
                while (i < n && cross_t[i] < t_out) {
                    if (cross_exit[i]) {
                        i += 1;
                        continue; // a surface without its entry: nothing to take out
                    }
                    const auto bb = (i + 1 < n) ? cross_t[i + 1] : t_out + 1.5 * k;
                    tissue += js_min(bb, t_out) - cross_t[i];
                    i += 2; // skip the layer's exit
                }
                return t_in + tissue;
            };
            // `fan`: a ray of the oral fan, whose origin the tongue can move off (the others start below
            // or in front of all tissue they measure)
            auto measure = [&] (Vec2 o, Vec2 dir, double max_len, bool fan) -> std::optional<Measured> {
                // walk from o along dir: last inner crossing before the first outer crossing
                const auto n_out = ray_polyline (o.x, o.y, dir.x, dir.y, outer, 0.0, max_len, tmp.data(), tmp.size());
                if (n_out == 0) {
                    return std::nullopt;
                }
                auto t_out = std::numeric_limits<double>::infinity();
                for (size_t i = 0; i < n_out; ++i) {
                    t_out = js_min(t_out, tmp[i]);
                }
                double t_in;
                if (fan) {
                    t_in = fan_inner (o, dir, t_out, tmp.data(), tmp.size());
                } else {
                    const auto n_in = ray_polyline (o.x, o.y, dir.x, dir.y, inner, 0.0, t_out + 1.5 * k, tmp.data(), tmp.size());
                    t_in = -std::numeric_limits<double>::infinity();
                    for (size_t i = 0; i < n_in; ++i) {
                        if (tmp[i] > t_in && tmp[i] < t_out + 1.5 * k) {
                            t_in = tmp[i];
                        }
                    }
                    if (t_in == -std::numeric_limits<double>::infinity()) {
                        t_in = js_max(0.0, t_out - 3.5 * k);
                    }
                }
                // if (the tongue crosses the wall, the outermost inner crossing lies beyond tOut → closed
                const auto d = js_max(0.0, t_out - t_in);
                const auto p_in = add(o, scale(dir, js_min(t_in, t_out)));
                const auto p_out = add(o, scale(dir, t_out));
                return Measured { p_in, p_out, d };
            };
            auto sample = [&] (Measured m, uint8_t region) { GridSample g {}; g.inner = m.inner; g.outer = m.outer; g.mid = lerp_v (m.inner, m.outer, 0.5); g.d = m.d; g.area = 0.0; g.s = 0.0; g.region = region; g.ray = m.d; return g; };
            // pharyngeal horizontals (origin in front, looking back)
            for (size_t i = 0; i < n_ph; ++i) {
                const auto y = lerp(y_epi_top + 0.12 * k, y_phar_top, double(i) / double(n_ph));
                if (auto m = measure (v (4.6 * k, y), v (-1.0, 0.0), 9.0 * k, false)) {
                    grid.push_back (sample (*m, 1));
                }
            }
            // oral family: rays from a centre that glides from C1 (mid-oral) to C2 (behind the incisors)
            // while (the angle sweeps back → up → forward; consecutive rays never cross inside the airway.
            const auto n_rad = 30;
            for (int i = 0; i <= int (n_rad); ++i) {
                const auto t = double(i) / double(n_rad);
                const auto ang = lerp(180.0, 36.0, t) * DEG;
                const auto w = (t < 0.45) ? 0.0 : (t - 0.45) / 0.55;
                const auto c = lerp_v(this->polar_c, this->polar_c2, w * w * (3.0 - 2.0 * w));
                const auto dir = v(cos(ang), sin(ang));
                auto m = measure (c, dir, 8.0 * k, true); if (! m) continue;
                if (split_index < 0 && ang <= 150.0 * DEG) {
                    split_index = grid.size();
                }
                // rays behind the velum still measure the (laterally wide) oropharynx
                grid.push_back (sample (*m, split_index < 0 ? 1 : 2));
            }
            // front verticals (upper incisor edge → lip opening), looking up
            const auto incisor_tip = this->palate[this->palate.size() - 1];
            const auto x_start = incisor_tip.x + 0.03 * k;
            const auto x_end = js_max(x_start + 0.3 * k, js_min(ul_edge.x, ll_edge.x) - 0.04 * k);
            const auto n_fr = 6;
            teeth_idx = grid.size();
            for (size_t i = 0; i < n_fr; ++i) {
                const auto x = lerp(x_start, x_end, i / (n_fr - 1.0));
                if (auto m = measure (v (x, 4.0 * k), v (0.0, 1.0), 8.0 * k, false)) {
                    // in front of the lower lip's edge (tucked under the incisors for /f v/) the ray
                    // meets no lower lip, and the fallback floor 3.5 cm down jumped the gap by ~2 cm
                    // and the tract's length by ~1 cm as the edge passed it: a click on every /f/.
                    // The floor there starts as the lip's outer face, leaving the edge down and
                    // forward as its outline does (tangent 0.3 : −0.4), and opens to the old depth
                    // as the tuck completes, so a held /f/ keeps its open front. The opening grows
                    // from nothing at the edge itself, so a ray the edge passes late in the tuck
                    // doesn't jump from the lip to the open floor (up to 1.3 cm² in one step).
                    if (x > ll_edge.x) {
                        const auto face = js_min (ll_edge.y - (x - ll_edge.x) * 4.0 / 3.0, m->outer.y);
                        const auto open = smoothstep (LAB_FRONT_OPENS, 1.0, lab) * smoothstep (0.0, LAB_FRONT_RAMP * k, x - ll_edge.x);
                        const auto y_in = lerp (face, m->outer.y - 3.5 * k, open);
                        m->inner = v (x, y_in);
                        m->d = m->outer.y - y_in;
                    }
                    grid.push_back (sample (*m, 3));
                }
            }
        }
        f.split_index = split_index;
        const auto lip_w = (1.1 + (this->body.lip_width - 1.1) * spread_w) * k;

        // ---- soft-tissue contact model: near the oral roof, the gap at each roof point is its
        // distance to the tongue surface itself (dorsum arc, blade, tip and underside), negative
        // where the tongue is pressed into the roof. The raw ray crossings of the tissue polygon can
        // leave sealed pockets beside a contact, and a jet released into such a pocket water-hammers;
        // the surface distance has none, gives seals whose length grows with contact pressure, and
        // handles two contacts at once (a single contact point hopped between them, jumping areas).
        auto& roof = sc.roof;
        roof.clear();
        roof.xs.insert (roof.xs.end(), sc.outer.xs.begin() + roof_start, sc.outer.xs.begin() + roof_end + 1);
        roof.ys.insert (roof.ys.end(), sc.outer.ys.begin() + roof_start, sc.outer.ys.begin() + roof_end + 1);
        auto& cum = sc.cum;
        cum.clear();
        cum.resize(roof.xs.size(), 0.0);
        for (size_t i = 1; i < roof.xs.size(); ++i) {
            cum[i] = cum[i - 1] + hypot(roof.xs[i] - roof.xs[i - 1], roof.ys[i] - roof.ys[i - 1]);
        }
        sc.tip_pts.clear();
        sc.tip_pts.insert (sc.tip_pts.end(), sc.blade.begin() + int (2), sc.blade.end());
        if (sc.under.size() > 1) sc.tip_pts.insert (sc.tip_pts.end(), sc.under.begin() + 1, sc.under.begin() + std::min<size_t> (3, sc.under.size()));
        const auto tip_c = closest_approach(roof, cum, sc.tip_pts);
        const auto body_c = closest_approach(roof, cum, sc.arc_pts);
        this->contact_tip = tip_c.first;
        this->contact_body = body_c.first;
        this->contact_tip_n = tip_c.second;
        this->contact_body_n = body_c.second;
        // No outer gate: each ray's own weight fades the contact in. A gate on the sampled
        // tongue-to-roof distance could open with the per-ray correction already part-way on.
        {
            // the whole tongue outline, closed: distance to its surface, signed by whether the roof
            // point is inside it (so the sign only changes where the distance is zero)
            auto& outl = sc.outline;
            outl.clear();
            outl.append(sc.root, false);
            outl.append(sc.arc_pts, true);
            outl.append(sc.blade, true);
            outl.append(sc.under, true);
            outl.append(floor, true);
            outl.push(sc.root[0]);
            auto tongue_gap = [&] (double x, double y) { return signed_gap (outl, x, y); };
            // Each roof ray takes the narrowest gap along its own stretch of roof: ±1 mm, so the rays
            // near a contact seal together and open together (a sharp burst), widening to ±2 mm while
            // the tongue is pressed in, because the tip's contact on the alveolar ridge is narrower
            // than the ~4 mm between rays there and could otherwise fall between two and never seal.
            const auto press = 1.0 - smoothstep(-0.06 * k, 0.0, js_min(tip_c.first, body_c.first));
            const auto half = 0.1 * k * (1.0 + press);
            const auto roof_len = cum[cum.size() - 1];
            for (auto& g : grid) {
                if (g.region == 0) {
                    continue;
                }
                // only rays ending on the oral roof, fading out (not switching off) as they leave it
                const auto pr = project_on(roof, cum, g.outer.x, g.outer.y);
                const auto on_roof = 1.0 - smoothstep(0.15 * k, 0.25 * k, pr.dist);
                if (on_roof <= 0.0) {
                    continue;
                }
                auto dc = tongue_gap(g.outer.x, g.outer.y);
                // the gap changes no faster than the distance moved, so a neighbour within `half`
                // along the roof can be closer only by `half`, plus how far off the roof this ray
                // ends: rays ending on the velopharyngeal port window, just behind the uvula's tip,
                // are still faded in by `on_roof`, and a bound without that distance switched the
                // search on with a neighbour already well inside the contact range (an area jump)
                if (dc - half - pr.dist <= 0.6 * k) {
                    for (double fr : { -1.0, -0.5, 0.5, 1.0 }) {
                        const auto q = point_along(roof, cum, clamp(pr.s + fr * half, 0.0, roof_len));
                        dc = js_min(dc, tongue_gap(q.x, q.y));
                    }
                }
                // the surface distance takes over as the tongue comes within ~6 mm of this roof point,
                // fully from 1.5 mm; it also caps the ray's gap from 4.5 mm in
                const auto w = (1.0 - smoothstep(0.15 * k, 0.6 * k, dc)) * on_roof;
                const auto cap = (1.0 - smoothstep(0.45 * k, 0.6 * k, dc)) * on_roof;
                if (w <= 0.0 && cap <= 0.0) {
                    continue;
                }
                const auto dp = js_max(0.0, dc);
                const auto dm = g.d + (js_min(g.d, dp) - g.d) * cap;
                g.d = dm + (dp - dm) * w;
            }
        }

        // lip contact: between the lips the gap follows aperture + Δx²/2R back from the lip edge, so
        // compressed lips (aperture < 0) seal over a length that grows with compression
        {
            const auto edge_x = js_min(ul_edge.x, ll_edge.x);
            const auto rl = 0.6 * k;
            // labiodental uses its own geometry: as the lower lip tucks under the incisors it leaves
            // the upper lip, so the lip-to-lip gap stops constraining. It hands over gradually: a
            // switch at lab 0.5 opened the near-closed lips of an /f/ in one control step, a [b]-like
            // release before every /f/ and /v/
            const auto lab_open = smoothstep(0.0, LAB_HANDOVER, lab);
            const auto ap_eff = (lab_open >= 1.0) ? std::numeric_limits<double>::infinity() : ap + LAB_GAP * k * lab_open / (1.0 - lab_open);
            const auto px = this->palate[this->palate.size() - 1].x;
            for (auto& g : grid) {
                if (g.region != 3 || g.mid.x < px) {
                    continue;
                }
                const auto dxl = js_max(0.0, edge_x - g.mid.x);
                g.d = js_min(g.d, js_max(0.0, ap_eff + (dxl * dxl) / (2.0 * rl)));
            }
        }

        // ---- areas
        const auto a_p = this->area_law.pharynx.first, b_p = this->area_law.pharynx.second;
        const auto a_o = this->area_law.oral.first, b_o = this->area_law.oral.second;
        const auto a_f = this->area_law.front.first, b_f = this->area_law.front.second;
        const auto a_h = this->area_law.ahead.first, b_h = this->area_law.ahead.second;
        const auto teeth_x = this->palate[this->palate.size() - 1].x;
        for (auto& g : grid) {
            if (g.region == 1) {
                g.area = a_p * pow(g.d, b_p);
            } else if (g.region == 2) {
                const auto ahead = smoothstep(0.0, 0.4 * k, g.mid.x - tip.x);
                g.area = lerp(a_o * pow(g.d, b_o), a_h * pow(g.d, b_h), ahead);
            } else {
                // behind the incisors: dental arch; in front: elliptical lip slit of width lipW
                const auto u = clamp((g.mid.x - teeth_x) / (0.45 * k), 0.0, 1.0);
                const auto dental = a_f * pow(g.d, b_f);
                const auto lip = (PI / 4.0) * g.d * lip_w;
                g.area = lerp(dental, lip, u);
            }
        }
        // suppress isolated spikes (rays grazing the port window / tooth corners): clamp each open
        // sample to 1.35× the larger of its neighbours
        for (size_t i = 1; i + 1 < grid.size(); ++i) {
            const auto lim = 1.35 * js_max(grid[i - 1].area, grid[i + 1].area);
            if (grid[i].area > lim) {
                grid[i].area = lim;
            }
        }
        // lateral channel (for /l/): keeps a side passage open around a midline tip closure
        if (a.lateral > 0.0) {
            for (auto& g : grid) {
                if (g.region >= 2 && dist(g.mid, tip) < 1.4 * k) {
                    g.area = js_max(g.area, a.lateral * 0.35 * k * k);
                }
            }
        }

        // ---- centreline s
        const auto epi_len = 2.4 * k * this->body.pharynx;
        auto s = epi_len;
        for (size_t i = 0; i < grid.size(); ++i) {
            if (i > 0) {
                s += dist(grid[i].mid, grid[i - 1].mid);
            }
            grid[i].s = s;
        }
        // teeth position along s (first front line sits at the upper incisor edge)
        const auto last = grid.size() - 1;
        f.teeth_s = (teeth_idx < grid.size()) ? grid[teeth_idx].s : grid[last].s;
        f.lip_s = grid[last].s;
        f.pharynx_length = (split_index > 0) ? grid[size_t (split_index)].s : grid[n_ph].s;
        f.length = f.lip_s;
        f.lip_area = grid[last].area;

        // ---- drawing parts
        f.tongue.clear();
        f.tongue.insert (f.tongue.end(), sc.root.begin(), sc.root.end());
        f.tongue.insert (f.tongue.end(), sc.arc_pts.begin() + int (1), sc.arc_pts.end());
        f.tongue.insert (f.tongue.end(), sc.blade.begin() + int (1), sc.blade.end());
        f.tongue.insert (f.tongue.end(), sc.under.begin() + int (1), sc.under.end());
        if (floor.size() > 1) f.tongue.insert (f.tongue.end(), floor.begin() + 1, floor.end());
        f.upper_lip.clear();
        f.upper_lip.insert (f.upper_lip.end(), sc.upper_lip_in.begin(), sc.upper_lip_in.end());
        bezier3(ul_edge, add(ul_edge, v(0.25 * k, 0.35 * k)), view.p(8.3 + 0.0, 9.6), view.p(7.9, 10.4), 6, sc.lip_out);
        f.upper_lip.insert (f.upper_lip.end(), sc.lip_out.begin() + int (1), sc.lip_out.end());
        f.lower_lip.clear();
        f.lower_lip.insert (f.lower_lip.end(), sc.lower_lip_in.begin(), sc.lower_lip_in.end());
        bezier3(ll_edge, add(ll_edge, v(0.3 * k, -0.4 * k)), j(8.1, 6.3), j(7.6, 5.6), 6, sc.lip_out);
        f.lower_lip.insert (f.lower_lip.end(), sc.lip_out.begin() + int (1), sc.lip_out.end());
        f.jaw_bone.clear();
        f.jaw_bone = { j(7.25, 6.75), j(7.4, 5.4), j(6.4, 4.6), j(3.5, 4.4), j(1.2, 5.2), j(-0.6, 7.4), j(-1.9, 9.6) };
        sc.outer.to_points(f.outer);
        sc.inner.to_points(f.inner);
        f.velum.clear();
        f.velum.insert (f.velum.end(), sc.vel_nasal.begin(), sc.vel_nasal.end());
        f.velum.insert (f.velum.end(), sc.vel_oral.rbegin(), sc.vel_oral.rend());
        f.upper_teeth.clear();
        f.upper_teeth = { view.p(6.15, 9.36), view.p(6.78, 8.7), view.p(7.12, 7.95), view.p(7.38, 8.7), view.p(7.4, 9.3) };
        f.lower_teeth.clear();
        f.lower_teeth = lower_teeth_pts;
        f.epiglottis.clear();
        f.epiglottis = { epi_base, epi_tip, vallecula };
        f.nasal_cavity.clear();
        f.nasal_cavity = this->nasal_cavity;
        f.glottis = glottis;
        f.velum_port = js_max(0.0, a.velum) * k * k;
        f.tongue_center = b;
        f.tongue_radius = r;
        f.tip = tip;
    }
} // namespace
