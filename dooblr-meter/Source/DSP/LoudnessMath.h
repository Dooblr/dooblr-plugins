#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dooblrmeter::dsp
{
    /** Loudness from 100 ms slots (BS.1770-4 / EBU R128 / EBU Tech 3342).

        A slot holds the sum of K-weighted, channel-weighted squares and its sample count. A 400 ms
        gating block is 4 consecutive slots (75 % overlap, as BS.1770 requires); a 3 s short-term
        window is 30 consecutive slots (10 Hz update, as Tech 3342 recommends). Slots with no
        samples break blocks and windows (Song mode leaves gaps where nothing was played). */
    struct SlotEnergy
    {
        double energy = 0.0;
        int n = 0;
    };

    struct LoudnessResult
    {
        double integrated = -150.0;   // LUFS
        bool hasIntegrated = false;
        double lra = 0.0;             // LU
        bool hasLra = false;
        double relativeGate = -150.0; // integrated gate (LUFS)
    };

    /** Integrated loudness (absolute gate -70 LUFS, relative gate -10 LU) and loudness range
        (short-term, absolute -70, relative -20 LU, 10th to 95th percentile). */
    LoudnessResult computeLoudness (const SlotEnergy* slots, size_t count);

    /** Mean-square loudness of `window` consecutive slots ending at `last` (inclusive); -150 if
        any slot in the window is empty. */
    double windowLoudness (const SlotEnergy* slots, size_t count, size_t last, int window);
}
