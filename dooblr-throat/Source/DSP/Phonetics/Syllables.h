#pragma once
// English syllabification (maximal onset). Ported from CHIMERA
// (MIT, Copyright 2026 Jameson Hodge).

#include "English.h"
#include "Types.h"
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    struct SyllableParts
    {
        std::vector<Segment> onset;
        Segment nucleus;
        std::vector<Segment> coda;
    };

    std::vector<SyllableParts> syllabifySegments (const std::vector<Segment>& segs);
    std::vector<Syllable> lyricsToSyllables (std::string_view text, const EnglishG2P& g2p);
}
