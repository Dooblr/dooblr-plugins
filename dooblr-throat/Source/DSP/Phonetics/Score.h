#pragma once
// Gestural score (articulatory-phonology style). Ported from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge). blendAll allocates nothing.

#include "../Articulation.h"
#include "../Math.h"
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace dooblrthroat::dsp::phonetics
{
    enum class SourceKey : uint8_t { Abduction, Closure, Breath, F0, Vibrato };

    inline constexpr size_t N_SOURCE = 5;
    inline constexpr size_t N_CONTROL = N_ARTIC + N_SOURCE;

    inline size_t sourceIndex (SourceKey k) noexcept { return N_ARTIC + size_t (k); }
    inline size_t articCtrlIndex (ArticKey k) noexcept { return artic_index (k); }

    struct Targets
    {
        std::array<double, N_CONTROL> values {};

        Targets() { values.fill (std::nan ("")); }

        std::optional<double> get (size_t i) const
        {
            const auto x = values[i];
            return std::isnan (x) ? std::nullopt : std::optional<double> (x);
        }
        Targets& set (size_t i, double x) { values[i] = x; return *this; }
        Targets& set (ArticKey k, double x) { return set (articCtrlIndex (k), x); }
        Targets& set (SourceKey k, double x) { return set (sourceIndex (k), x); }
        bool has (size_t i) const { return ! std::isnan (values[i]); }
        bool isEmpty() const
        {
            for (auto x : values) if (! std::isnan (x)) return false;
            return true;
        }
        static Targets fromArtic (const ArticPatch& p)
        {
            Targets t;
            for (size_t i = 0; i < N_ARTIC; ++i)
                if (auto v = p.values[i]) t.values[i] = *v;
            return t;
        }
        static Targets one (size_t i, double x) { Targets t; t.set (i, x); return t; }
        static Targets one (SourceKey k, double x) { return one (sourceIndex (k), x); }
        static Targets one (ArticKey k, double x) { return one (articCtrlIndex (k), x); }
    };

    struct Label
    {
        static constexpr size_t CAP = 24;
        uint8_t len = 0;
        char bytes[CAP] {};

        static Label make (std::string_view s)
        {
            Label l;
            auto n = s.size() < CAP ? s.size() : CAP;
            while (n > 0 && (static_cast<unsigned char> (s[n - 1]) & 0xc0) == 0x80)
                --n; // cut at a UTF-8 boundary
            l.len = uint8_t (n);
            if (n) std::memcpy (l.bytes, s.data(), n);
            return l;
        }
        std::string_view view() const { return { bytes, len }; }
        bool empty() const { return len == 0; }
    };

    struct Gesture
    {
        double t0 = 0, t1 = 0, rampIn = 0, rampOut = 0, strength = 1;
        Targets targets {};
        double speed = 0;        // 0 = unset
        double releaseHold = 0;
        Label label {};

        static Gesture make (double t0, double t1, double rampIn, double rampOut,
                             double strength, const Targets& targets)
        {
            Gesture g;
            g.t0 = t0; g.t1 = t1; g.rampIn = rampIn; g.rampOut = rampOut;
            g.strength = strength; g.targets = targets;
            return g;
        }
        Gesture& withLabel (std::string_view s) { label = Label::make (s); return *this; }
        Gesture& withSpeed (double s) { speed = s; return *this; }
        Gesture& withReleaseHold (double s) { releaseHold = s; return *this; }
        double end() const { return t1 + rampOut + releaseHold; }
    };

    struct Blended
    {
        double value = 0, weight = 0, speed = 1;
    };

    inline double activation (const Gesture& g, double t)
    {
        if (t <= g.t0 - g.rampIn || t >= g.t1 + g.rampOut)
            return 0.0;
        if (t < g.t0)
        {
            const auto u = (t - (g.t0 - g.rampIn)) / js_max (1e-6, g.rampIn);
            return 0.5 - 0.5 * std::cos (PI * u);
        }
        if (t > g.t1)
        {
            const auto u = (t - g.t1) / js_max (1e-6, g.rampOut);
            return 0.5 + 0.5 * std::cos (PI * u);
        }
        return 1.0;
    }

    /** Blend every control key at time t. Safe on the audio thread (no allocation). */
    inline void blendAll (const Gesture* gs, size_t n, double t, Blended out[N_CONTROL])
    {
        double wsum[N_CONTROL] {}, vsum[N_CONTROL] {}, speed[N_CONTROL];
        for (size_t i = 0; i < N_CONTROL; ++i) speed[i] = 1.0;
        for (size_t gi = 0; gi < n; ++gi)
        {
            const auto& g = gs[gi];
            const auto lo = g.t0 - g.rampIn;
            const bool inSpeed = g.speed != 0.0 && t >= lo && t <= g.end();
            const auto act = activation (g, t);
            if (act <= 0.0 && ! inSpeed) continue;
            const auto w = act * g.strength;
            for (size_t i = 0; i < N_CONTROL; ++i)
            {
                const auto target = g.targets.values[i];
                if (std::isnan (target)) continue;
                if (inSpeed && g.speed > speed[i]) speed[i] = g.speed;
                if (act <= 0.0) continue;
                wsum[i] += w;
                vsum[i] += w * target;
            }
        }
        for (size_t i = 0; i < N_CONTROL; ++i)
            out[i] = Blended { wsum[i] > 0.0 ? vsum[i] / wsum[i] : 0.0, wsum[i], speed[i] };
    }
}
