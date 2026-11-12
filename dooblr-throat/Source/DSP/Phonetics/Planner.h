#pragma once
// Segment sequence → gestural score. Ported from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge). Runs off the audio thread.

#include "Inventory.h"
#include "Score.h"
#include "Types.h"
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    struct Plan
    {
        std::vector<Gesture> gestures;
        std::vector<PlannedSegment> segments;
        double end = 0;
    };

    struct PlanOptions
    {
        BodyMix body {};
        double t0 = 0;
        double rate = 1;
        double f0 = 110;
        Intonation intonation = Intonation::Statement;
        double accent = 0.14;
        bool openEnd = false;
        bool sung = false;
        bool hasF0 = true;
    };

    Plan planUtterance (const std::vector<Segment>& input, const PlanOptions& o);
    void oralGesturesInto (const PhonemeDef& def, const ArticPatch& tg,
                           double a, double b, double speed, std::string_view label,
                           std::vector<Gesture>& out);
}
