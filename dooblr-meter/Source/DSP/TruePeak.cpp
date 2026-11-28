#include "TruePeak.h"

#include <cmath>

namespace dooblrmeter::dsp
{
    namespace
    {
        double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-12 * sum)
                    break;
            }
            return sum;
        }
    }

    void TruePeak::prepare (double sampleRate)
    {
        factor = sampleRate < 88000.0 ? 4 : (sampleRate < 176000.0 ? 2 : 1);
        const int n = factor * tapsPerPhase;
        phases.assign ((size_t) n, 0.0f);

        if (factor == 1)
        {
            phases.assign ((size_t) tapsPerPhase, 0.0f);
            phases[0] = 1.0f;
            reset();
            return;
        }

        // Kaiser-windowed sinc low-pass at the original Nyquist, designed at the upsampled rate.
        constexpr double pi = 3.14159265358979323846, beta = 6.0;
        std::vector<double> proto ((size_t) n);
        const double centre = 0.5 * (n - 1), fc = 0.5 / factor;
        for (int i = 0; i < n; ++i)
        {
            const double t = i - centre;
            const double sinc = std::abs (t) < 1.0e-9 ? 2.0 * fc : std::sin (2.0 * pi * fc * t) / (pi * t);
            const double r = t / centre;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            proto[(size_t) i] = sinc * w;
        }

        // Phase p uses taps p, p + L, p + 2L, ...; each phase is normalised to unity DC gain.
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                sum += proto[(size_t) (p + factor * k)];
            for (int k = 0; k < tapsPerPhase; ++k)
                phases[(size_t) (p * tapsPerPhase + k)] = (float) (proto[(size_t) (p + factor * k)] / sum);
        }
        reset();
    }

    void TruePeak::reset() noexcept
    {
        for (auto& s : state)
        {
            s.ring.fill (0.0f);
            s.pos = 0;
        }
    }
}
