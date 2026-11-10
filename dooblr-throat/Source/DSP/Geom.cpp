#include "Geom.h"
#include <limits>

namespace dooblrthroat::dsp
{
    static double crossing_t (double ox, double oy, double dx, double dy,
                              double ax, double ay, double bx, double by) noexcept
    {
        const bool sa = dx * (ay - oy) - dy * (ax - ox) >= 0.0;
        const bool sb = dx * (by - oy) - dy * (bx - ox) >= 0.0;
        if (sa == sb) return std::numeric_limits<double>::quiet_NaN();
        const auto ex = bx - ax, ey = by - ay;
        const auto den = dx * ey - dy * ex;
        if (abs (den) < 1e-15) return std::numeric_limits<double>::quiet_NaN();
        return ((ax - ox) * ey - (ay - oy) * ex) / den;
    }

    size_t ray_polyline (double ox, double oy, double dx, double dy, const Polyline& pl,
                         double t_min, double t_max, double* out, size_t out_n)
    {
        size_t n = 0;
        const auto& xs = pl.xs;
        const auto& ys = pl.ys;
        if (xs.size() < 2) return 0;
        for (size_t i = 0; i + 1 < xs.size(); ++i)
        {
            const auto t = crossing_t (ox, oy, dx, dy, xs[i], ys[i], xs[i + 1], ys[i + 1]);
            if (t >= t_min && t <= t_max && n < out_n)
                out[n++] = t;
        }
        return n;
    }

    size_t ray_crossings (double ox, double oy, double dx, double dy, const Polyline& pl,
                          double t_min, double t_max, double* out_t, bool* exit, size_t out_n)
    {
        size_t n = 0;
        const auto& xs = pl.xs;
        const auto& ys = pl.ys;
        if (xs.size() < 2) return 0;
        for (size_t i = 0; i + 1 < xs.size(); ++i)
        {
            const auto t = crossing_t (ox, oy, dx, dy, xs[i], ys[i], xs[i + 1], ys[i + 1]);
            if (t >= t_min && t <= t_max && n < out_n)
            {
                out_t[n] = t;
                exit[n] = dx * (ys[i + 1] - ys[i]) - dy * (xs[i + 1] - xs[i]) < 0.0;
                ++n;
            }
        }
        return n;
    }

    void bezier3 (Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, size_t count, std::vector<Vec2>& out)
    {
        out.clear();
        if (count == 0) return;
        for (size_t i = 0; i < count; ++i)
        {
            const auto t = double (i) / (double (count) - 1.0);
            const auto u = 1.0 - t;
            const auto a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
            out.push_back (v (a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                              a * p0.y + b * p1.y + c * p2.y + d * p3.y));
        }
    }

    void bezier2 (Vec2 p0, Vec2 p1, Vec2 p2, size_t count, std::vector<Vec2>& out)
    {
        out.clear();
        if (count == 0) return;
        for (size_t i = 0; i < count; ++i)
        {
            const auto t = double (i) / (double (count) - 1.0);
            const auto u = 1.0 - t;
            out.push_back (v (u * u * p0.x + 2.0 * u * t * p1.x + t * t * p2.x,
                              u * u * p0.y + 2.0 * u * t * p1.y + t * t * p2.y));
        }
    }

    void arc (Vec2 c, double r, double a0, double a1, size_t count, std::vector<Vec2>& out)
    {
        out.clear();
        if (count == 0) return;
        for (size_t i = 0; i < count; ++i)
            out.push_back (polar (c, r, a0 + ((a1 - a0) * double (i)) / (double (count) - 1.0)));
    }

    std::vector<Vec2> catmull_rom (const std::vector<Vec2>& pts, size_t per)
    {
        if (pts.size() < 3) return pts;
        std::vector<Vec2> out;
        out.reserve ((pts.size() - 1) * per + 1);
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const auto p0 = pts[i == 0 ? 0 : i - 1];
            const auto p1 = pts[i];
            const auto p2 = pts[i + 1];
            const auto p3 = pts[i + 2 < pts.size() ? i + 2 : pts.size() - 1];
            for (size_t k = 0; k < per; ++k)
            {
                const auto t = double (k) / double (per);
                const auto t2 = t * t, t3 = t2 * t;
                out.push_back (v (
                    0.5 * (2.0 * p1.x + (-p0.x + p2.x) * t + (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 + (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3),
                    0.5 * (2.0 * p1.y + (-p0.y + p2.y) * t + (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 + (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3)));
            }
        }
        out.push_back (pts.back());
        return out;
    }

    bool tangent_angle (Vec2 c, double r, Vec2 p, bool upper, double& out)
    {
        const auto d = dist (c, p);
        if (d <= r * 1.0001) return false;
        const auto base = atan2 (p.y - c.y, p.x - c.x);
        const auto off = acos (r / d);
        out = upper ? base + off : base - off;
        return true;
    }
}
