#pragma once
// Shared phonetics types. Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    enum class Intonation { Statement, Question, Flat };

    struct Segment
    {
        std::string ipa;
        std::optional<double> dur;
        std::optional<uint8_t> stress;
        std::optional<double> f0;
        bool wordEnd = false;
    };

    struct PlannedSegment
    {
        std::string ipa;
        double t0 = 0, t1 = 0;
        std::optional<uint8_t> stress;
        std::string kind;
    };

    struct Syllable
    {
        std::string text;
        std::vector<Segment> onset;
        Segment nucleus;
        std::vector<Segment> coda;
        bool melisma = false;
        bool wordEnd = false;
        bool phraseEnd = false;
    };

    struct Phrase
    {
        std::vector<Segment> segments;
        Intonation intonation = Intonation::Statement;
        double pause = 0.3;
        std::vector<std::string> words;
    };
}
