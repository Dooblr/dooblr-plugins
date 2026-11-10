#pragma once
#include "Airway.h"
#include "Anatomy.h"

namespace dooblrthroat::dsp
{
    struct NoiseGains { double glottis = 3.2e-6, constriction = 2.2e-6, teeth = 3.0e-6; };

    struct HumanAirway
    {
        AirwayNetwork net;
        HumanVocalTract tract;
        HumanBody body {};
        Tube trachea {}, pharynx {}, oral {}, nasal {}, fossa {};
        size_t lungs = 0, glottis_port = 0, velum_port = 0, rad_lips = 0, rad_nose = 0;
        size_t n_ph = 0, n_or = 0;
        std::vector<double> ph_areas, or_areas, ph_min, or_min;
        HumanFrame frame;
        bool has_frame = false;
        double ph_len = 8.8, or_len = 8.3;
        NoiseGains noise_gain {};
        double max_area_rate = 45.0, area_rate_gain = 40.0, seal_rate = 5.0;
        double seal_lo = 0.03, seal_hi = 0.4, seal_ease = 0.03;
        std::vector<double> base_ph, base_or, base_ph_min, base_or_min;
        bool has_base = false;
        double base_dt = 0.0;
        std::vector<NoiseSpec> noise_list;

        HumanAirway (double fs, HumanBody body);
        size_t branch_of (Tube t, int i) const;
        const HumanFrame& apply (const HumanArtic& a, double dt);
        const HumanFrame& reapply (const HumanArtic& a);

    private:
        const HumanFrame& commit (const HumanArtic& a, double dt);
        void update_noise (const HumanFrame& f);
    };
}
