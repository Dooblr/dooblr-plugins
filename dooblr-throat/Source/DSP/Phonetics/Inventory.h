#pragma once
// Phoneme inventory (IPA) as articulatory specifications for the human model.
// Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).

#include "../Articulation.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dooblrthroat::dsp::phonetics
{
    enum class Manner {
        Vowel, Stop, Nasal, Fricative, Affricate, Approximant,
        Lateral, Tap, Trill, Glottal
    };

    enum class Voicing { Voiced, Voiceless };
    enum class GlottalKind { None, Open, Stop, Breathy };

    inline const char* mannerName (Manner m)
    {
        switch (m) {
            case Manner::Vowel: return "vowel";
            case Manner::Stop: return "stop";
            case Manner::Nasal: return "nasal";
            case Manner::Fricative: return "fricative";
            case Manner::Affricate: return "affricate";
            case Manner::Approximant: return "approximant";
            case Manner::Lateral: return "lateral";
            case Manner::Tap: return "tap";
            case Manner::Trill: return "trill";
            case Manner::Glottal: return "glottal";
        }
        return "?";
    }

    struct PhonemeDef
    {
        const char* ipa = "";
        const char* name = "";
        Manner manner = Manner::Vowel;
        Voicing voicing = Voicing::Voiced;
        const char* place = "";
        ArticPatch targets {};
        const ArticKey* dominant = nullptr;
        size_t nDominant = 0;
        bool nasal = false;
        double dur = 0.08;
        const char* release = nullptr;
        GlottalKind glottal = GlottalKind::None;

        bool isDominant (ArticKey k) const
        {
            for (size_t i = 0; i < nDominant; ++i)
                if (dominant[i] == k) return true;
            return false;
        }
    };

    void initInventory();
    std::optional<PhonemeDef> phoneme (std::string_view ipa, const BodyMix& body);
    const PhonemeDef* phonemeByIpa (std::string_view ipa);
    const std::vector<PhonemeDef>& consonants();
    std::optional<ArticPatch> vowelArtic (std::string_view ipa, const BodyMix& body);
    const char* const* expandDiphthong (std::string_view ipa); // two strings, second may be null, or nullptr
    bool isVowelIpa (std::string_view ipa, const BodyMix& body);
}
