#pragma once
// Branched staggered-grid FDTD airway, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Math.h"
#include "Physics.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace dooblrthroat::dsp
{
    struct Area
    {
        bool uniform = true;
        double value = 0.0;
        std::vector<double> profile;
        static Area Uniform (double a) { Area x; x.uniform = true; x.value = a; return x; }
        static Area Profile (std::vector<double> p) { Area x; x.uniform = false; x.profile = std::move (p); return x; }
        double at (size_t i) const { return uniform ? value : profile[i]; }
    };

    struct TubeSpec
    {
        std::string name;
        size_t n = 0;
        double length = 0.0;
        Area area = Area::Uniform (1.0);
        std::optional<WallParams> wall;
        double perimeter_factor = 1.0, viscous_factor = 1.0, wall_radiation = 0.0;

        static TubeSpec make (const char* name, size_t n, double length, Area area)
        {
            TubeSpec s;
            s.name = name; s.n = n; s.length = length; s.area = std::move (area);
            return s;
        }
        TubeSpec& set_wall (std::optional<WallParams> w) { wall = w; return *this; }
        TubeSpec& perimeter (double f) { perimeter_factor = f; return *this; }
        TubeSpec& viscous (double f) { viscous_factor = f; return *this; }
        TubeSpec& radiating (double f) { wall_radiation = f; return *this; }
    };

    struct Tube
    {
        size_t index = 0, n = 0, first = 0, branch_base = 0;
        size_t node (size_t i) const noexcept { return first + i; }
        size_t first_node() const noexcept { return first; }
        size_t last_node() const noexcept { return first + n - 1; }
        size_t branch (size_t i) const noexcept { return branch_base + i; }
    };

    inline constexpr uint8_t KIND_SERIES = 0, KIND_PORT = 1;

    struct PortSpec
    {
        double area = 0.0, length = 0.0, k_entrance = 0.4, k_exit = 1.0;
        double slit_width = 0.0, extra_resistance = 0.0;
        bool include_half_sections = true;
        static PortSpec make (double area, double length)
        {
            PortSpec s; s.area = area; s.length = length; return s;
        }
        PortSpec& kinetic (double ke, double kx) { k_entrance = ke; k_exit = kx; return *this; }
        PortSpec& slit (double w) { slit_width = w; return *this; }
        PortSpec& no_half_sections() { include_half_sections = false; return *this; }
        PortSpec& resistance (double r) { extra_resistance = r; return *this; }
    };

    struct NoiseSpec
    {
        size_t src = 0, fwd = 0, bwd = 0;
        double gain = 0.0, lowpass_hz = 7000.0, highpass_hz = 400.0;
        static NoiseSpec make (size_t src, size_t fwd, size_t bwd, double gain)
        {
            NoiseSpec s; s.src = src; s.fwd = fwd; s.bwd = bwd; s.gain = gain; return s;
        }
        NoiseSpec& band (double lp, double hp) { lowpass_hz = lp; highpass_hz = hp; return *this; }
    };

    struct NoiseSource
    {
        size_t src = 0, fwd = 0, bwd = 0;
        double gain = 0.0, hp = 0.0, lp1 = 0.0, lp2 = 0.0, lp_coef = 0.0, hp_coef = 0.0, level = 0.0;
    };

    struct AirwayNetwork
    {
        double fs = 48000.0, t = 0.0, inv_t = 0.0;
        std::vector<Tube> tubes;
        std::vector<std::string> tube_names;
        std::vector<double> tube_length;
        std::vector<TubeSpec> tube_specs;
        bool finalized = false;

        size_t n_nodes = 0;
        std::vector<double> p, area, area_target, area_step, amin, amin_target, amin_step;
        std::vector<char> is_junction;
        std::vector<double> inv_lsum, inv_amin;
        std::vector<size_t> nbr_start, nbr_list;
        std::vector<double> spill, vol_src1, vol_src2;
        double vol_lp_coef = 0.0, volume_source_hz = 60.0, squeeze_area = 0.12;
        bool enable_volume_source = true, enable_junction_floor = true, energy_safe = true, symmetric_kinetic = false;
        std::vector<double> dx, dx_target, dx_step, vol, inv_a, half_l, half_r, sqrt_a, flow_sum, ext_flow;
        std::vector<char> fixed;
        std::vector<double> wall_m, wall_r, wall_k, wall_perim, wall_rad, wall_y, wall_v, wall_u, visc_f;
        std::vector<char> has_wall;
        std::vector<size_t> node_tube;
        std::vector<int32_t> rad_of;

        size_t n_branches = 0;
        std::vector<size_t> b_a, b_b;
        std::vector<uint8_t> b_kind;
        std::vector<std::optional<PortSpec>> port_specs;
        std::vector<double> u, mom, l_prev, psrc, port_area, port_area_target, port_area_step;
        std::vector<double> port_len, port_kin, port_kex, port_slit, port_extra_r, branch_area;
        std::vector<char> port_half;

        std::vector<size_t> rad_nodes;
        std::vector<double> rad_ul, rad_l_prev, rad_u;
        double rad_prev_total = 0.0;

        std::vector<NoiseSource> noise, noise_next;
        Rng rng { 12345 };
        std::vector<size_t> pending_fixed;
        double closed_area = 2e-4, min_node_area = 0.02, viscous_scale = 1.0;
        double viscous_omega = 2.0 * PI * 700.0, noise_scale = 1.0, out = 0.0;

        explicit AirwayNetwork (double fs);
        Tube add_tube (TubeSpec spec);
        size_t connect (size_t a, size_t b);
        size_t add_port (size_t a, size_t b, PortSpec spec);
        size_t add_cavity (const char* name, double volume, std::optional<WallParams> wall);
        size_t add_pressure_node (const char* name);
        size_t add_radiation (size_t node);
        void finalize();
        double check_cfl() const;
        void set_tube_targets (Tube t, const double* areas, size_t n, const double* length /*nullable*/, const double* min_areas /*nullable*/);
        void set_node_area_target (size_t node, double area);
        void set_port_target (size_t branch, double area);
        void set_port_area (size_t branch, double area) noexcept;
        void set_fixed_pressure (size_t node, double p) noexcept;
        void snap_to_targets();
        void begin_block (size_t samples);
        void set_noise_sources (const NoiseSpec* list, size_t n);
        double step();
        double radiated_flow (size_t r) const { return rad_u[r]; }
        void reset();

    private:
        size_t push_branch (size_t a, size_t b, uint8_t kind, std::optional<PortSpec> spec);
        void flush_state();
        void compute_noise();
    };
}
