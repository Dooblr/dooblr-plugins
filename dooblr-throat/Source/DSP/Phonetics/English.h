#pragma once
// English text → phoneme segments: CMUdict (ARPAbet) when loaded, letter-to-sound
// rules otherwise. Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "Types.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    class EnglishG2P
    {
    public:
        bool isLoaded() const { return loaded_; }

        /** Dictionary as "word\\tPHONES" lines (gunzipped CMUdict). */
        void loadDict (std::string_view text);

        std::vector<Phrase> toPhrases (std::string_view text) const;
        std::vector<Segment> word (std::string_view w, bool reduce) const;

    private:
        std::unordered_map<std::string, std::string> dict_;
        bool loaded_ = false;
    };

    std::vector<Segment> arpabetToSegments (std::string_view phones);
    std::vector<Segment> letterToSound (std::string_view word);
    bool isEnglishVowelIpa (std::string_view ipa);
}
