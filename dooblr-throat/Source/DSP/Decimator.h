#pragma once
// Kaiser-windowed sinc decimator, ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include <cstddef>
#include <vector>

namespace dooblrthroat::dsp
{
    struct Decimator
    {
        size_t factor = 1;
        double value = 0.0;
        std::vector<double> h, buf;
        size_t pos = 0, count = 0;

        Decimator() = default;
        explicit Decimator (size_t factor, size_t taps_per_phase = 24);
        bool push (double x) noexcept;
    };
}
