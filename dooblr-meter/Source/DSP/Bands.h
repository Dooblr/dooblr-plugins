#pragma once

#include <array>
#include <cmath>

namespace dooblrmeter::dsp
{
    /** The 30 one-third-octave bands from 25 Hz to 20 kHz (base-2 centres 1000 * 2^(k/3)). */
    namespace bands
    {
        inline constexpr int count = 30;
        inline constexpr int firstK = -16; // 1000 * 2^(-16/3) = 24.8 Hz

        inline double centre (int b) noexcept { return 1000.0 * std::pow (2.0, (firstK + b) / 3.0); }
        inline double lowerEdge (int b) noexcept { return centre (b) * std::pow (2.0, -1.0 / 6.0); }
        inline double upperEdge (int b) noexcept { return centre (b) * std::pow (2.0, 1.0 / 6.0); }

        /** Band index containing hz, or -1 outside 22..22.4k Hz. */
        inline int indexOf (double hz) noexcept
        {
            if (hz <= 0.0)
                return -1;
            const auto k = (int) std::lround (3.0 * std::log2 (hz / 1000.0)) - firstK;
            return (k >= 0 && k < count) ? k : -1;
        }

        /** Nominal (ISO) label, e.g. "31.5", "1.25k". */
        inline const char* nominal (int b) noexcept
        {
            static const char* names[count] = { "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200",
                                                "250", "315", "400", "500", "630", "800", "1k", "1.25k", "1.6k", "2k",
                                                "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k" };
            return (b >= 0 && b < count) ? names[b] : "";
        }

        using Levels = std::array<float, count>;
    }
}
