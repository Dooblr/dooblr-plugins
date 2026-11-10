#pragma once
#include "Articulation.h"
#include <cstddef>

namespace dooblrthroat::dsp
{
    struct VowelDef
    {
        const char* ipa;
        const char* name;
        double height; // 0 close .. 1 open
        double back;   // 0 front .. 1 back
        ArticPatch male, female, child;
    };

    inline constexpr size_t N_VOWELS = 16;
    extern const VowelDef VOWELS[N_VOWELS];

    ArticPatch vowel_patch (size_t index, BodyMix body);
    ArticPatch vowel_at (double height, double back, BodyMix body); // pad interpolation
    size_t nearest_vowel (double height, double back);
}
