#pragma once
// Context-sensitive consonant targets after Birkholz (2013) and the
// dominance construction of Birkholz (2007). Reimplemented from the papers
// only; this file does not use or link VocalTractLab (GPL-3.0).
//
// Birkholz, P. (2013). Modeling consonant-vowel coarticulation for
// articulatory speech synthesis. PLoS ONE 8(4), e60603.
// Birkholz, P. (2007). Control of an articulatory speech synthesizer based
// on dynamic approximation of spatial articulatory targets. Interspeech 2007.
//
// Message thread only (planning). The audio thread never calls these.

#include "../Articulation.h"
#include "Inventory.h"
#include <string_view>

namespace dooblrthroat::dsp::phonetics
{
    struct CornerWeights
    {
        // Lingual: xv = xa + a1 (xi - xa) + a2 (xu - xa).  wa = 1 - a1 - a2.
        // Labial:  same form with b1, b2.
        double a1 = 0.0, a2 = 0.0;
        double b1 = 0.0, b2 = 0.0;
    };

    /** Clamp (a1, a2) to the triangle 0 <= a1, 0 <= a2, a1 + a2 <= 1 (Birkholz 2013). */
    void clampToVowelTriangle (double& a1, double& a2) noexcept;

    /** Map a vowel articulatory patch into the /a, i, u/ subspace. */
    CornerWeights vowelCornerWeights (const ArticPatch& vowel, const BodyMix& body);

    /** Consonant shape in the context of one corner vowel, via 2007 dominance
        (xc|v[i] = xv[i] + w_c[i] (xc[i] - xv[i])). Built from this plugin's
        inventory, not from any third-party table. */
    ArticPatch consonantInVowelContext (const PhonemeDef& def, const ArticPatch& vowel);

    /** Birkholz 2013 blend: weighted average of C|/a/, C|/i/, C|/u/ with
        weights from how close `vowel` is to those corner vowels. Lips and
        tongue are interpolated in their own subspaces. If `vowel` is null,
        returns the inventory (context-independent) consonant target. */
    ArticPatch contextConsonantTarget (const PhonemeDef& def,
                                       const ArticPatch* vowel,
                                       const BodyMix& body);
}
