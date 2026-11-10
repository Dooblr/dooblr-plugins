#pragma once
// Human midsagittal articulatory model, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Articulation.h"
#include "Geom.h"
#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include <vector>

namespace dooblrthroat::dsp
{
    struct GridSample
    {
        Vec2 inner, outer, mid;
        double d = 0.0, area = 0.0, s = 0.0, ray = 0.0;
        uint8_t region = 0;
    };

    struct HumanFrame
    {
        std::vector<Vec2> outer, inner, tongue, velum, upper_lip, lower_lip;
        std::vector<Vec2> upper_teeth, lower_teeth, jaw_bone, epiglottis, nasal_cavity;
        Vec2 glottis {}, tongue_center {}, tip {};
        std::vector<GridSample> grid;
        double length = 0.0, pharynx_length = 0.0, teeth_s = 0.0, lip_s = 0.0;
        double velum_port = 0.0, lip_area = 0.0, tongue_radius = 0.0;
        int32_t split_index = -1;
        static HumanFrame with_capacity();
    };

    struct AreaLaw
    {
        std::pair<double, double> pharynx { 2.0, 1.4 }, oral { 2.6, 1.4 }, front { 2.0, 1.4 }, ahead { 1.6, 1.4 };
    };

    struct Measured { Vec2 inner, outer; double d = 0.0; };

    struct Scratch
    {
        std::array<double, 64> tmp {}, cross_t {};
        std::array<bool, 64> cross_exit {};
        std::vector<double> cum;
        std::vector<Vec2> vel, vel_oral, vel_nasal, upper_lip_in, lower_lip_in, lip_out;
        std::vector<Vec2> root, arc_pts, blade, under, back, tip_pts;
        Polyline outer, inner, roof, outline;
        static Scratch make();
    };

    struct HumanVocalTractView
    {
        double k = 1.0;
        HumanBody body {};
        Vec2 condyle {};
        Vec2 p (double x, double y) const;
    };

    struct HumanVocalTract
    {
        HumanBody body {};
        double k = 1.0;
        std::vector<Vec2> phar_wall, palate, nasal_cavity;
        Vec2 pns {}, condyle {}, polar_c {}, polar_c2 {};
        Scratch sc = Scratch::make();
        double contact_tip = 1.0, contact_body = 1.0;
        Vec2 contact_tip_n { 0.0, -1.0 }, contact_body_n { 0.0, -1.0 };
        AreaLaw area_law {};

        explicit HumanVocalTract (HumanBody body);
        Vec2 p (double x, double y) const;
        Vec2 un_p (double x, double y) const;
        double body_scale() const { return k; }
        const std::vector<Vec2>& palate_pts() const { return palate; }
        void compute (const HumanArtic& a, HumanFrame& f);
        HumanFrame compute_new (const HumanArtic& a);
        double epilarynx_area (const HumanArtic& a, double s, double epi_len) const;
        std::pair<double, double> resample (const HumanArtic& a, const HumanFrame& f,
                                            double* out_ph, size_t n_ph, double* out_or, size_t n_or,
                                            double* min_ph, double* min_or) const;
    };
}
