#pragma once
// 2D geometry helpers, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Math.h"
#include <cstddef>
#include <vector>

namespace dooblrthroat::dsp
{
    struct Vec2
    {
        double x = 0.0, y = 0.0;
    };

    inline constexpr Vec2 v (double x, double y) noexcept { return { x, y }; }
    inline Vec2 add (Vec2 a, Vec2 b) noexcept { return v (a.x + b.x, a.y + b.y); }
    inline Vec2 sub (Vec2 a, Vec2 b) noexcept { return v (a.x - b.x, a.y - b.y); }
    inline Vec2 scale (Vec2 a, double s) noexcept { return v (a.x * s, a.y * s); }
    inline double len (Vec2 a) noexcept { return hypot (a.x, a.y); }
    inline double dist (Vec2 a, Vec2 b) noexcept { return hypot (a.x - b.x, a.y - b.y); }

    inline Vec2 norm (Vec2 a) noexcept
    {
        auto l = hypot (a.x, a.y);
        if (l == 0.0 || is_nan (l)) l = 1.0;
        return v (a.x / l, a.y / l);
    }

    inline Vec2 lerp_v (Vec2 a, Vec2 b, double t) noexcept { return v (a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
    inline Vec2 polar (Vec2 c, double r, double ang) noexcept { return v (c.x + r * cos (ang), c.y + r * sin (ang)); }

    inline Vec2 rotate_about (Vec2 p, Vec2 c, double ang) noexcept
    {
        const auto s = sin (ang), co = cos (ang);
        const auto dx = p.x - c.x, dy = p.y - c.y;
        return v (c.x + dx * co - dy * s, c.y + dx * s + dy * co);
    }

    struct Polyline
    {
        std::vector<double> xs, ys;
        static Polyline with_capacity (size_t n)
        {
            Polyline p;
            p.xs.reserve (n);
            p.ys.reserve (n);
            return p;
        }
        Polyline& push (Vec2 p) { xs.push_back (p.x); ys.push_back (p.y); return *this; }
        Polyline& append (const std::vector<Vec2>& pts, bool skip_first)
        {
            const size_t start = skip_first && ! pts.empty() ? 1 : 0;
            for (size_t i = start; i < pts.size(); ++i) push (pts[i]);
            return *this;
        }
        size_t len() const noexcept { return xs.size(); }
        size_t size() const noexcept { return xs.size(); }
        bool is_empty() const noexcept { return xs.empty(); }
        Vec2 point (size_t i) const noexcept { return v (xs[i], ys[i]); }
        void to_points (std::vector<Vec2>& out) const
        {
            out.clear();
            out.reserve (xs.size());
            for (size_t i = 0; i < xs.size(); ++i) out.push_back (v (xs[i], ys[i]));
        }
        void clear() { xs.clear(); ys.clear(); }
    };

    size_t ray_polyline (double ox, double oy, double dx, double dy, const Polyline& pl,
                         double t_min, double t_max, double* out, size_t out_n);
    size_t ray_crossings (double ox, double oy, double dx, double dy, const Polyline& pl,
                          double t_min, double t_max, double* out_t, bool* exit, size_t out_n);
    void bezier3 (Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, size_t count, std::vector<Vec2>& out);
    void bezier2 (Vec2 p0, Vec2 p1, Vec2 p2, size_t count, std::vector<Vec2>& out);
    void arc (Vec2 c, double r, double a0, double a1, size_t count, std::vector<Vec2>& out);
    std::vector<Vec2> catmull_rom (const std::vector<Vec2>& pts, size_t per);
    bool tangent_angle (Vec2 c, double r, Vec2 p, bool upper, double& out);
}
