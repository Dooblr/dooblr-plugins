#include "KWeighting.h"

#include <cmath>

namespace dooblrmeter::dsp
{
    void KWeighting::prepare (double sr) noexcept
    {
        if (std::abs (sr - 48000.0) < 0.5)
        {
            shelf.b0 = 1.53512485958697; shelf.b1 = -2.69169618940638; shelf.b2 = 1.19839281085285;
            shelf.a1 = -1.69065929318241; shelf.a2 = 0.73248077421585;
            rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
            rlb.a1 = -1.99004745483398; rlb.a2 = 0.99007225036621;
        }
        else
        {
            // Analog prototype parameters that reproduce the 48 kHz coefficients
            // (the derivation used by most open implementations of BS.1770).
            constexpr double pi = 3.14159265358979323846;
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (pi * f0 / sr);
                const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelf.b1 = 2.0 * (K * K - Vh) / a0;
                shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelf.a1 = 2.0 * (K * K - 1.0) / a0;
                shelf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (pi * f0 / sr);
                const double a0 = 1.0 + K / Q + K * K;
                rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
                rlb.a1 = 2.0 * (K * K - 1.0) / a0;
                rlb.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
        reset();
    }
}
