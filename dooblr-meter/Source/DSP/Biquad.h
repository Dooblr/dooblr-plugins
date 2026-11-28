#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace dooblrmeter::dsp
{
    /** Direct form II transposed biquad (double precision state, a0 normalised to 1). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        inline double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0; }

        /** |H(e^jw)| in dB at frequency hz for sample rate sr. */
        double magnitudeDb (double hz, double sr) const noexcept
        {
            const auto w = 2.0 * 3.14159265358979323846 * hz / sr;
            const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
            const auto num = b0 + b1 * z1c + b2 * z2c;
            const auto den = 1.0 + a1 * z1c + a2 * z2c;
            return 20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den)));
        }

        enum class Type { lowPass, highPass, bandPass, peak, lowShelf, highShelf };

        /** RBJ cookbook designs. gainDb is used by peak and shelves only. */
        static Biquad design (Type type, double sr, double hz, double q, double gainDb = 0.0) noexcept
        {
            constexpr double pi = 3.14159265358979323846;
            hz = std::min (hz, 0.49 * sr);
            const auto w0 = 2.0 * pi * hz / sr;
            const auto cw = std::cos (w0), sw = std::sin (w0);
            const auto alpha = sw / (2.0 * q);
            const auto A = std::pow (10.0, gainDb / 40.0);
            double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

            switch (type)
            {
                case Type::lowPass:
                    b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                    a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                    break;
                case Type::highPass:
                    b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                    a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                    break;
                case Type::bandPass:
                    b0 = alpha; b1 = 0; b2 = -alpha;
                    a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                    break;
                case Type::peak:
                    b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                    a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                    break;
                case Type::lowShelf:
                {
                    const auto s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) - (A - 1) * cw + s);
                    b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                    b2 = A * ((A + 1) - (A - 1) * cw - s);
                    a0 = (A + 1) + (A - 1) * cw + s;
                    a1 = -2 * ((A - 1) + (A + 1) * cw);
                    a2 = (A + 1) + (A - 1) * cw - s;
                    break;
                }
                case Type::highShelf:
                {
                    const auto s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) + (A - 1) * cw + s);
                    b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                    b2 = A * ((A + 1) + (A - 1) * cw - s);
                    a0 = (A + 1) - (A - 1) * cw + s;
                    a1 = 2 * ((A - 1) - (A + 1) * cw);
                    a2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }

            Biquad b;
            b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0;
            b.a1 = a1 / a0; b.a2 = a2 / a0;
            return b;
        }
    };

    /** Mean square to dB with a floor (never -inf). */
    inline double powerToDb (double p, double floorDb = -150.0) noexcept
    {
        return p > 0.0 ? std::max (floorDb, 10.0 * std::log10 (p)) : floorDb;
    }

    inline double gainToDb (double g, double floorDb = -150.0) noexcept
    {
        return g > 0.0 ? std::max (floorDb, 20.0 * std::log10 (g)) : floorDb;
    }
}
