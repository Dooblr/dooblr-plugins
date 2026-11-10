#pragma once
// CGS constants and wall parameters, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

namespace dooblrthroat::dsp
{
    inline constexpr double RHO = 1.14e-3;       // g/cm^3
    inline constexpr double C_SOUND = 35000.0;   // cm/s
    inline constexpr double MU = 1.86e-4;        // poise
    inline constexpr double NU = MU / RHO;
    inline constexpr double RHO_C2 = RHO * C_SOUND * C_SOUND;
    inline constexpr double CM_H2O = 980.665;    // dyn/cm^2 per cmH2O
    inline constexpr double RE_CRIT = 1800.0;

    struct WallParams
    {
        double mass = 0.0;        // g/cm^2
        double resistance = 0.0;
        double stiffness = 0.0;
    };

    inline constexpr WallParams WALL_SOFT    { 1.5, 1600.0, 3.0e5 };
    inline constexpr WallParams WALL_TRACHEA { 0.6, 1000.0, 1.0e6 };
    inline constexpr WallParams WALL_NASAL   { 1.0, 3000.0, 1.0e6 };
}
