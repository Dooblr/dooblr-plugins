#pragma once
// Klatt (1979) / MITalk segment durations. Ported from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge).

#include "Inventory.h"
#include "Types.h"
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    struct SegInfo
    {
        bool vowel = false;
        std::optional<Manner> manner;
        std::optional<bool> voiced;
        std::optional<double> dur;
    };

    std::vector<double> klattDurations (const std::vector<Segment>& segs,
                                        const std::vector<SegInfo>& info);
}
