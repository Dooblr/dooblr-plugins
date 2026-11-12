// Phoneme inventory and calibrated vowel/consonant targets.
// Ported from CHIMERA (MIT, Copyright 2026 Jameson Hodge).
#include "Inventory.h"
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace dooblrthroat::dsp {
namespace phonetics {
namespace {
static const ArticKey kDom0[] { ArticKey::LipAp };
static const ArticKey kDom1[] { ArticKey::TtX, ArticKey::TtY };
static const ArticKey kDom2[] { ArticKey::TtX, ArticKey::TtY, ArticKey::TtCurl };
static const ArticKey kDom3[] { ArticKey::TbX, ArticKey::TbY };
static const ArticKey kDom4[] { ArticKey::TbY };
static const ArticKey kDom5[1] {}; // unused empty
static const ArticKey kDom6[] { ArticKey::Labiodental, ArticKey::LipAp };
static const ArticKey kDom7[] { ArticKey::TtX, ArticKey::TtY, ArticKey::Jaw };
static const ArticKey kDom8[] { ArticKey::TtX, ArticKey::TtY, ArticKey::Jaw, ArticKey::LipProt, ArticKey::LipWidth };
static const ArticKey kDom9[] { ArticKey::Root, ArticKey::TbX, ArticKey::TbY };
static const ArticKey kDom10[] { ArticKey::TtX, ArticKey::TtY, ArticKey::Lateral };
static const ArticKey kDom11[] { ArticKey::TtX, ArticKey::TtY, ArticKey::LipProt, ArticKey::LipWidth };
static const ArticKey kDom12[] { ArticKey::LipAp, ArticKey::LipProt, ArticKey::LipWidth, ArticKey::TbX, ArticKey::TbY };
static const ArticKey kDom13[] { ArticKey::TtX, ArticKey::TtY, ArticKey::TtCurl, ArticKey::LipProt, ArticKey::LipWidth, ArticKey::Root };
static const ArticKey kDom14[] { ArticKey::TtX, ArticKey::TtY, ArticKey::Lateral, ArticKey::TbX, ArticKey::TbY };
static const ArticKey kDom15[] { ArticKey::TtX, ArticKey::TtY, ArticKey::TtCurl, ArticKey::Lateral };
static const ArticKey kDom16[] { ArticKey::TbX, ArticKey::TbY, ArticKey::Lateral };
static std::vector<PhonemeDef> gBase;
static std::unordered_map<std::string, size_t> gByIpa;
static bool gReady = false;
static std::mutex gLock;

ArticPatch makePatch(std::initializer_list<std::pair<ArticKey,double>> xs) {
    ArticPatch p; for (auto [k,v] : xs) p.set(k,v); return p; }

void buildBase() {
    gBase.clear();
    gBase.push_back(PhonemeDef{ "p", "voiceless bilabial stop", Manner::Stop, Voicing::Voiceless, "bilabial", makePatch({{ArticKey::LipAp,-0.25}}), kDom0, 1, false, 0.085, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "b", "voiced bilabial stop", Manner::Stop, Voicing::Voiced, "bilabial", makePatch({{ArticKey::LipAp,-0.25}}), kDom0, 1, false, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "t", "voiceless alveolar stop", Manner::Stop, Voicing::Voiceless, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, false, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "d", "voiced alveolar stop", Manner::Stop, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, false, 0.06, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0288", "voiceless retroflex stop", Manner::Stop, Voicing::Voiceless, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,10.05}, {ArticKey::TtCurl,0.95}}), kDom2, 3, false, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0256", "voiced retroflex stop", Manner::Stop, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,10.05}, {ArticKey::TtCurl,0.95}}), kDom2, 3, false, 0.065, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "c", "voiceless palatal stop", Manner::Stop, Voicing::Voiceless, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.45}}), kDom3, 2, false, 0.08, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u025f", "voiced palatal stop", Manner::Stop, Voicing::Voiced, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.45}}), kDom3, 2, false, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "k", "voiceless velar stop", Manner::Stop, Voicing::Voiceless, "velar", makePatch({{ArticKey::TbX,2.3}, {ArticKey::TbY,8.35}, {ArticKey::Jaw,0.7}}), kDom4, 1, false, 0.08, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "g", "voiced velar stop", Manner::Stop, Voicing::Voiced, "velar", makePatch({{ArticKey::TbX,2.3}, {ArticKey::TbY,8.35}, {ArticKey::Jaw,0.7}}), kDom4, 1, false, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "q", "voiceless uvular stop", Manner::Stop, Voicing::Voiceless, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.6}}), kDom3, 2, false, 0.085, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0262", "voiced uvular stop", Manner::Stop, Voicing::Voiced, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.6}}), kDom3, 2, false, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0294", "glottal stop", Manner::Glottal, Voicing::Voiceless, "glottal", makePatch({}), kDom5, 0, false, 0.06, nullptr, GlottalKind::Stop });
    gBase.push_back(PhonemeDef{ "m", "bilabial nasal", Manner::Nasal, Voicing::Voiced, "bilabial", makePatch({{ArticKey::LipAp,-0.25}}), kDom0, 1, true, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0271", "labiodental nasal", Manner::Nasal, Voicing::Voiced, "labiodental", makePatch({{ArticKey::Labiodental,1.0}, {ArticKey::LipAp,-0.1}}), kDom6, 2, true, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "n", "alveolar nasal", Manner::Nasal, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, true, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0273", "retroflex nasal", Manner::Nasal, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,10.05}, {ArticKey::TtCurl,0.95}}), kDom2, 3, true, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0272", "palatal nasal", Manner::Nasal, Voicing::Voiced, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.45}}), kDom3, 2, true, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u014b", "velar nasal", Manner::Nasal, Voicing::Voiced, "velar", makePatch({{ArticKey::TbX,2.3}, {ArticKey::TbY,8.35}, {ArticKey::Jaw,0.7}}), kDom4, 1, true, 0.08, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0274", "uvular nasal", Manner::Nasal, Voicing::Voiced, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.6}}), kDom3, 2, true, 0.08, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0278", "voiceless bilabial fricative", Manner::Fricative, Voicing::Voiceless, "bilabial", makePatch({{ArticKey::LipAp,0.08}}), kDom0, 1, false, 0.11, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u03b2", "voiced bilabial fricative", Manner::Fricative, Voicing::Voiced, "bilabial", makePatch({{ArticKey::LipAp,0.1}}), kDom0, 1, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "f", "voiceless labiodental fricative", Manner::Fricative, Voicing::Voiceless, "labiodental", makePatch({{ArticKey::Labiodental,1.0}, {ArticKey::LipAp,0.05}}), kDom6, 2, false, 0.11, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "v", "voiced labiodental fricative", Manner::Fricative, Voicing::Voiced, "labiodental", makePatch({{ArticKey::Labiodental,1.0}, {ArticKey::LipAp,0.06}}), kDom6, 2, false, 0.08, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u03b8", "voiceless dental fricative", Manner::Fricative, Voicing::Voiceless, "dental", makePatch({{ArticKey::TtX,6.85}, {ArticKey::TtY,8.5}}), kDom1, 2, false, 0.11, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u00f0", "voiced dental fricative", Manner::Fricative, Voicing::Voiced, "dental", makePatch({{ArticKey::TtX,6.85}, {ArticKey::TtY,8.5}}), kDom1, 2, false, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "s", "voiceless alveolar fricative", Manner::Fricative, Voicing::Voiceless, "alveolar", makePatch({{ArticKey::TtX,6.3}, {ArticKey::TtY,9.15}, {ArticKey::Jaw,0.3}}), kDom7, 3, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "z", "voiced alveolar fricative", Manner::Fricative, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.3}, {ArticKey::TtY,9.15}, {ArticKey::Jaw,0.3}}), kDom7, 3, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0283", "voiceless postalveolar fricative", Manner::Fricative, Voicing::Voiceless, "postalveolar", makePatch({{ArticKey::TtX,5.7}, {ArticKey::TtY,9.5}, {ArticKey::Jaw,0.35}, {ArticKey::LipProt,0.75}, {ArticKey::LipWidth,0.2}}), kDom8, 5, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0292", "voiced postalveolar fricative", Manner::Fricative, Voicing::Voiced, "postalveolar", makePatch({{ArticKey::TtX,5.7}, {ArticKey::TtY,9.5}, {ArticKey::Jaw,0.35}, {ArticKey::LipProt,0.75}, {ArticKey::LipWidth,0.2}}), kDom8, 5, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0282", "voiceless retroflex fricative", Manner::Fricative, Voicing::Voiceless, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,9.7}, {ArticKey::TtCurl,0.9}}), kDom2, 3, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0290", "voiced retroflex fricative", Manner::Fricative, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,9.7}, {ArticKey::TtCurl,0.9}}), kDom2, 3, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u00e7", "voiceless palatal fricative", Manner::Fricative, Voicing::Voiceless, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.25}}), kDom3, 2, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u029d", "voiced palatal fricative", Manner::Fricative, Voicing::Voiced, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.25}}), kDom3, 2, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "x", "voiceless velar fricative", Manner::Fricative, Voicing::Voiceless, "velar", makePatch({{ArticKey::TbX,2.3}, {ArticKey::TbY,7.7}}), kDom4, 1, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0263", "voiced velar fricative", Manner::Fricative, Voicing::Voiced, "velar", makePatch({{ArticKey::TbX,2.3}, {ArticKey::TbY,7.7}}), kDom4, 1, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u03c7", "voiceless uvular fricative", Manner::Fricative, Voicing::Voiceless, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.6}}), kDom3, 2, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0281", "voiced uvular fricative", Manner::Fricative, Voicing::Voiced, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.6}}), kDom3, 2, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0127", "voiceless pharyngeal fricative", Manner::Fricative, Voicing::Voiceless, "pharyngeal", makePatch({{ArticKey::Root,-0.95}, {ArticKey::TbX,1.3}, {ArticKey::TbY,6.2}}), kDom9, 3, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0295", "voiced pharyngeal fricative", Manner::Fricative, Voicing::Voiced, "pharyngeal", makePatch({{ArticKey::Root,-0.95}, {ArticKey::TbX,1.3}, {ArticKey::TbY,6.2}}), kDom9, 3, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "h", "voiceless glottal fricative", Manner::Glottal, Voicing::Voiceless, "glottal", makePatch({}), kDom5, 0, false, 0.09, nullptr, GlottalKind::Open });
    gBase.push_back(PhonemeDef{ "\u0266", "voiced glottal fricative", Manner::Glottal, Voicing::Voiced, "glottal", makePatch({}), kDom5, 0, false, 0.08, nullptr, GlottalKind::Breathy });
    gBase.push_back(PhonemeDef{ "\u026c", "voiceless lateral fricative", Manner::Fricative, Voicing::Voiceless, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}, {ArticKey::Lateral,0.35}}), kDom10, 3, false, 0.12, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u026e", "voiced lateral fricative", Manner::Fricative, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}, {ArticKey::Lateral,0.45}}), kDom10, 3, false, 0.09, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "ts", "voiceless alveolar affricate", Manner::Affricate, Voicing::Voiceless, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, false, 0.14, "s", GlottalKind::None });
    gBase.push_back(PhonemeDef{ "dz", "voiced alveolar affricate", Manner::Affricate, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, false, 0.12, "z", GlottalKind::None });
    gBase.push_back(PhonemeDef{ "t\u0283", "voiceless postalveolar affricate", Manner::Affricate, Voicing::Voiceless, "postalveolar", makePatch({{ArticKey::TtX,5.75}, {ArticKey::TtY,10.0}, {ArticKey::LipProt,0.75}, {ArticKey::LipWidth,0.2}}), kDom11, 4, false, 0.15, "\u0283", GlottalKind::None });
    gBase.push_back(PhonemeDef{ "d\u0292", "voiced postalveolar affricate", Manner::Affricate, Voicing::Voiced, "postalveolar", makePatch({{ArticKey::TtX,5.75}, {ArticKey::TtY,10.0}, {ArticKey::LipProt,0.75}, {ArticKey::LipWidth,0.2}}), kDom11, 4, false, 0.13, "\u0292", GlottalKind::None });
    gBase.push_back(PhonemeDef{ "pf", "voiceless labiodental affricate", Manner::Affricate, Voicing::Voiceless, "labiodental", makePatch({{ArticKey::LipAp,-0.25}}), kDom0, 1, false, 0.14, "f", GlottalKind::None });
    gBase.push_back(PhonemeDef{ "w", "labial-velar approximant", Manner::Approximant, Voicing::Voiced, "labial-velar", makePatch({{ArticKey::LipAp,0.28}, {ArticKey::LipProt,1.0}, {ArticKey::LipWidth,0.05}, {ArticKey::TbX,2.45}, {ArticKey::TbY,7.45}}), kDom12, 5, false, 0.06, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u028b", "labiodental approximant", Manner::Approximant, Voicing::Voiced, "labiodental", makePatch({{ArticKey::Labiodental,0.9}, {ArticKey::LipAp,0.25}}), kDom6, 2, false, 0.06, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0279", "alveolar approximant", Manner::Approximant, Voicing::Voiced, "postalveolar", makePatch({{ArticKey::TtX,5.35}, {ArticKey::TtY,9.35}, {ArticKey::TtCurl,0.85}, {ArticKey::LipProt,0.5}, {ArticKey::LipWidth,0.3}, {ArticKey::Root,-0.6}, {ArticKey::TbX,2.3}, {ArticKey::TbY,7.1}}), kDom13, 6, false, 0.065, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u027b", "retroflex approximant", Manner::Approximant, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.4}, {ArticKey::TtY,9.55}, {ArticKey::TtCurl,1.0}}), kDom2, 3, false, 0.065, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "j", "palatal approximant", Manner::Approximant, Voicing::Voiced, "palatal", makePatch({{ArticKey::TbX,3.8}, {ArticKey::TbY,7.95}, {ArticKey::Jaw,0.25}}), kDom3, 2, false, 0.055, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0270", "velar approximant", Manner::Approximant, Voicing::Voiced, "velar", makePatch({{ArticKey::TbX,2.35}, {ArticKey::TbY,7.45}}), kDom3, 2, false, 0.06, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "l", "alveolar lateral approximant", Manner::Lateral, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}, {ArticKey::Lateral,1.0}}), kDom10, 3, false, 0.065, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u026b", "velarised lateral", Manner::Lateral, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}, {ArticKey::Lateral,1.0}, {ArticKey::TbX,1.9}, {ArticKey::TbY,6.8}}), kDom14, 5, false, 0.075, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u026d", "retroflex lateral", Manner::Lateral, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,10.05}, {ArticKey::TtCurl,0.95}, {ArticKey::Lateral,1.0}}), kDom15, 4, false, 0.065, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u028e", "palatal lateral", Manner::Lateral, Voicing::Voiced, "palatal", makePatch({{ArticKey::TbX,3.75}, {ArticKey::TbY,8.45}, {ArticKey::Lateral,1.0}}), kDom16, 3, false, 0.07, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u027e", "alveolar tap", Manner::Tap, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.68}}), kDom1, 2, false, 0.025, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u027d", "retroflex flap", Manner::Tap, Voicing::Voiced, "retroflex", makePatch({{ArticKey::TtX,5.45}, {ArticKey::TtY,10.05}, {ArticKey::TtCurl,0.95}}), kDom2, 3, false, 0.03, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "r", "alveolar trill", Manner::Trill, Voicing::Voiced, "alveolar", makePatch({{ArticKey::TtX,6.25}, {ArticKey::TtY,9.45}}), kDom1, 2, false, 0.11, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0280", "uvular trill", Manner::Trill, Voicing::Voiced, "uvular", makePatch({{ArticKey::TbX,1.55}, {ArticKey::TbY,7.85}}), kDom3, 2, false, 0.11, nullptr, GlottalKind::None });
    gBase.push_back(PhonemeDef{ "\u0299", "bilabial trill", Manner::Trill, Voicing::Voiced, "bilabial", makePatch({{ArticKey::LipAp,0.02}}), kDom0, 1, false, 0.12, nullptr, GlottalKind::None });
    gByIpa.clear();
    for (size_t i=0;i<gBase.size();++i) gByIpa[gBase[i].ipa]=i;
}
} // namespace

namespace tables {
struct VowelRow { const char* body; const char* ipa; ArticPatch artic; };
struct ConsRow  { const char* body; const char* ipa; ArticPatch artic; };
static const VowelRow kVowels[] = {
    { "male", "i", makePatch({{ArticKey::TbX, 3.683}, {ArticKey::TbY, 7.739}, {ArticKey::Jaw, 0.413}, {ArticKey::LipAp, 1.046}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.514}, {ArticKey::Larynx, 0.798}, {ArticKey::TtX, 6.545}, {ArticKey::TtY, 7.288}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u026a", makePatch({{ArticKey::TbX, 3.637}, {ArticKey::TbY, 7.419}, {ArticKey::Jaw, 0.575}, {ArticKey::LipAp, 1.154}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.001}, {ArticKey::Larynx, 0.357}, {ArticKey::TtX, 6.472}, {ArticKey::TtY, 7.032}, {ArticKey::TtCurl, 0}}) },
    { "male", "e", makePatch({{ArticKey::TbX, 3.6}, {ArticKey::TbY, 7.236}, {ArticKey::Jaw, 0.745}, {ArticKey::LipAp, 1.315}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.269}, {ArticKey::Larynx, 0.506}, {ArticKey::TtX, 6.396}, {ArticKey::TtY, 6.817}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u025b", makePatch({{ArticKey::TbX, 3.403}, {ArticKey::TbY, 6.556}, {ArticKey::Jaw, 1.041}, {ArticKey::LipAp, 1.519}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.048}, {ArticKey::Larynx, 0.379}, {ArticKey::TtX, 6.218}, {ArticKey::TtY, 6.32}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u00e6", makePatch({{ArticKey::TbX, 3.373}, {ArticKey::TbY, 6.016}, {ArticKey::Jaw, 1.516}, {ArticKey::LipAp, 1.875}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.16}, {ArticKey::Larynx, 0.328}, {ArticKey::TtX, 6.002}, {ArticKey::TtY, 5.726}, {ArticKey::TtCurl, 0.002}}) },
    { "male", "a", makePatch({{ArticKey::TbX, 2.292}, {ArticKey::TbY, 5.667}, {ArticKey::Jaw, 1.649}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.373}, {ArticKey::Larynx, 0.37}, {ArticKey::TtX, 5.617}, {ArticKey::TtY, 5.487}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0251", makePatch({{ArticKey::TbX, 2.022}, {ArticKey::TbY, 5.692}, {ArticKey::Jaw, 1.685}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.354}, {ArticKey::Larynx, 0.217}, {ArticKey::TtX, 5.519}, {ArticKey::TtY, 5.466}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0252", makePatch({{ArticKey::TbX, 1.365}, {ArticKey::TbY, 5.824}, {ArticKey::Jaw, 1.534}, {ArticKey::LipAp, 0.965}, {ArticKey::LipProt, 0.349}, {ArticKey::LipWidth, 0.483}, {ArticKey::Root, -0.221}, {ArticKey::Larynx, -0.047}, {ArticKey::TtX, 5.392}, {ArticKey::TtY, 5.64}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0254", makePatch({{ArticKey::TbX, 1.501}, {ArticKey::TbY, 6.115}, {ArticKey::Jaw, 1.182}, {ArticKey::LipAp, 0.844}, {ArticKey::LipProt, 0.31}, {ArticKey::LipWidth, 0.539}, {ArticKey::Root, -0.328}, {ArticKey::Larynx, 0.175}, {ArticKey::TtX, 5.588}, {ArticKey::TtY, 6.043}, {ArticKey::TtCurl, 0}}) },
    { "male", "o", makePatch({{ArticKey::TbX, 1.519}, {ArticKey::TbY, 6.592}, {ArticKey::Jaw, 0.668}, {ArticKey::LipAp, 0.405}, {ArticKey::LipProt, 0.597}, {ArticKey::LipWidth, 0.027}, {ArticKey::Root, 0.227}, {ArticKey::Larynx, 0.061}, {ArticKey::TtX, 5.801}, {ArticKey::TtY, 6.66}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u028a", makePatch({{ArticKey::TbX, 2.317}, {ArticKey::TbY, 7.139}, {ArticKey::Jaw, 0.516}, {ArticKey::LipAp, 0.44}, {ArticKey::LipProt, 1.228}, {ArticKey::LipWidth, 0.199}, {ArticKey::Root, 0.478}, {ArticKey::Larynx, 0.425}, {ArticKey::TtX, 6.098}, {ArticKey::TtY, 6.987}, {ArticKey::TtCurl, 0}}) },
    { "male", "u", makePatch({{ArticKey::TbX, 2.419}, {ArticKey::TbY, 7.428}, {ArticKey::Jaw, 0.203}, {ArticKey::LipAp, 0.291}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.065}, {ArticKey::Root, 0.402}, {ArticKey::Larynx, 0.252}, {ArticKey::TtX, 6.239}, {ArticKey::TtY, 7.369}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u028c", makePatch({{ArticKey::TbX, 1.955}, {ArticKey::TbY, 6.23}, {ArticKey::Jaw, 1.022}, {ArticKey::LipAp, 0.76}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 0.607}, {ArticKey::Root, 0.212}, {ArticKey::Larynx, 0.053}, {ArticKey::TtX, 5.791}, {ArticKey::TtY, 6.227}, {ArticKey::TtCurl, 0.002}}) },
    { "male", "\u0259", makePatch({{ArticKey::TbX, 2.944}, {ArticKey::TbY, 6.685}, {ArticKey::Jaw, 0.505}, {ArticKey::LipAp, 0.323}, {ArticKey::LipProt, 0.352}, {ArticKey::LipWidth, 0.533}, {ArticKey::Root, 0.421}, {ArticKey::Larynx, 0.149}, {ArticKey::TtX, 6.29}, {ArticKey::TtY, 6.837}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0250", makePatch({{ArticKey::TbX, 2.374}, {ArticKey::TbY, 6.055}, {ArticKey::Jaw, 1.394}, {ArticKey::LipAp, 1.042}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 0.722}, {ArticKey::Root, 0.136}, {ArticKey::Larynx, -0.013}, {ArticKey::TtX, 5.757}, {ArticKey::TtY, 5.841}, {ArticKey::TtCurl, 0.001}}) },
    { "male", "\u025d", makePatch({{ArticKey::TbX, 2.727}, {ArticKey::TbY, 7.285}, {ArticKey::Jaw, 0.976}, {ArticKey::LipAp, 0.654}, {ArticKey::LipProt, 0.592}, {ArticKey::LipWidth, 0.347}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -0.921}, {ArticKey::TtX, 5.642}, {ArticKey::TtY, 8.632}, {ArticKey::TtCurl, 1}}) },
    { "male", "y", makePatch({{ArticKey::TbX, 3.478}, {ArticKey::TbY, 7.838}, {ArticKey::Jaw, 0.202}, {ArticKey::LipAp, 0.3}, {ArticKey::LipProt, 1.397}, {ArticKey::LipWidth, 0.3}, {ArticKey::Root, 0.038}, {ArticKey::Larynx, 0.038}, {ArticKey::TtX, 6.557}, {ArticKey::TtY, 7.512}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u00f8", makePatch({{ArticKey::TbX, 3.45}, {ArticKey::TbY, 7.301}, {ArticKey::Jaw, 0.625}, {ArticKey::LipAp, 0.334}, {ArticKey::LipProt, 0.599}, {ArticKey::LipWidth, 0.001}, {ArticKey::Root, 0.16}, {ArticKey::Larynx, 0.088}, {ArticKey::TtX, 6.397}, {ArticKey::TtY, 6.947}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0153", makePatch({{ArticKey::TbX, 3.01}, {ArticKey::TbY, 6.638}, {ArticKey::Jaw, 1.036}, {ArticKey::LipAp, 0.687}, {ArticKey::LipProt, 0.629}, {ArticKey::LipWidth, 0.303}, {ArticKey::Root, 0.106}, {ArticKey::Larynx, 0.075}, {ArticKey::TtX, 6.103}, {ArticKey::TtY, 6.353}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0268", makePatch({{ArticKey::TbX, 2.941}, {ArticKey::TbY, 7.71}, {ArticKey::Jaw, 0.184}, {ArticKey::LipAp, 0.2}, {ArticKey::LipProt, 0.003}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.663}, {ArticKey::Larynx, 0.243}, {ArticKey::TtX, 6.4}, {ArticKey::TtY, 7.491}, {ArticKey::TtCurl, 0.003}}) },
    { "male", "\u026f", makePatch({{ArticKey::TbX, 2.715}, {ArticKey::TbY, 7.625}, {ArticKey::Jaw, 0.176}, {ArticKey::LipAp, 0.187}, {ArticKey::LipProt, 0.348}, {ArticKey::LipWidth, 0.55}, {ArticKey::Root, 0.784}, {ArticKey::Larynx, 0.284}, {ArticKey::TtX, 6.337}, {ArticKey::TtY, 7.463}, {ArticKey::TtCurl, 0}}) },
    { "male", "\u0264", makePatch({{ArticKey::TbX, 2.259}, {ArticKey::TbY, 7.251}, {ArticKey::Jaw, 0.638}, {ArticKey::LipAp, 0.467}, {ArticKey::LipProt, 0.351}, {ArticKey::LipWidth, 0.545}, {ArticKey::Root, 0.64}, {ArticKey::Larynx, 0.382}, {ArticKey::TtX, 6.035}, {ArticKey::TtY, 6.918}, {ArticKey::TtCurl, 0}}) },
    { "female", "i", makePatch({{ArticKey::TbX, 3.754}, {ArticKey::TbY, 7.647}, {ArticKey::Jaw, 0.459}, {ArticKey::LipAp, 1.092}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.295}, {ArticKey::Larynx, 1.016}, {ArticKey::TtX, 6.55}, {ArticKey::TtY, 7.215}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u026a", makePatch({{ArticKey::TbX, 3.647}, {ArticKey::TbY, 7.421}, {ArticKey::Jaw, 0.567}, {ArticKey::LipAp, 1.147}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.077}, {ArticKey::Larynx, 0.196}, {ArticKey::TtX, 6.478}, {ArticKey::TtY, 7.039}, {ArticKey::TtCurl, 0}}) },
    { "female", "e", makePatch({{ArticKey::TbX, 3.593}, {ArticKey::TbY, 7.368}, {ArticKey::Jaw, 0.8}, {ArticKey::LipAp, 1.369}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.08}, {ArticKey::Larynx, 0.853}, {ArticKey::TtX, 6.372}, {ArticKey::TtY, 6.815}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u025b", makePatch({{ArticKey::TbX, 3.1}, {ArticKey::TbY, 6.484}, {ArticKey::Jaw, 1.181}, {ArticKey::LipAp, 1.642}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.105}, {ArticKey::Larynx, 0.773}, {ArticKey::TtX, 6.068}, {ArticKey::TtY, 6.173}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u00e6", makePatch({{ArticKey::TbX, 3.712}, {ArticKey::TbY, 6.328}, {ArticKey::Jaw, 1.483}, {ArticKey::LipAp, 1.851}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.094}, {ArticKey::Larynx, 0.854}, {ArticKey::TtX, 6.119}, {ArticKey::TtY, 5.86}, {ArticKey::TtCurl, 0}}) },
    { "female", "a", makePatch({{ArticKey::TbX, 2.191}, {ArticKey::TbY, 5.739}, {ArticKey::Jaw, 1.637}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.407}, {ArticKey::Larynx, 0.641}, {ArticKey::TtX, 5.592}, {ArticKey::TtY, 5.523}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0251", makePatch({{ArticKey::TbX, 1.847}, {ArticKey::TbY, 5.832}, {ArticKey::Jaw, 1.682}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.665}, {ArticKey::Larynx, 0.523}, {ArticKey::TtX, 5.468}, {ArticKey::TtY, 5.517}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0252", makePatch({{ArticKey::TbX, 1.33}, {ArticKey::TbY, 5.889}, {ArticKey::Jaw, 1.51}, {ArticKey::LipAp, 1.002}, {ArticKey::LipProt, 0.323}, {ArticKey::LipWidth, 0.525}, {ArticKey::Root, -0.255}, {ArticKey::Larynx, -0.007}, {ArticKey::TtX, 5.392}, {ArticKey::TtY, 5.683}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0254", makePatch({{ArticKey::TbX, 1.397}, {ArticKey::TbY, 6.219}, {ArticKey::Jaw, 1.256}, {ArticKey::LipAp, 0.904}, {ArticKey::LipProt, 0.281}, {ArticKey::LipWidth, 0.59}, {ArticKey::Root, -0.619}, {ArticKey::Larynx, 0.254}, {ArticKey::TtX, 5.525}, {ArticKey::TtY, 6.015}, {ArticKey::TtCurl, 0}}) },
    { "female", "o", makePatch({{ArticKey::TbX, 1.827}, {ArticKey::TbY, 7.11}, {ArticKey::Jaw, 0.966}, {ArticKey::LipAp, 0.288}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.304}, {ArticKey::Root, 0.221}, {ArticKey::Larynx, 0.46}, {ArticKey::TtX, 5.776}, {ArticKey::TtY, 6.579}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u028a", makePatch({{ArticKey::TbX, 2.314}, {ArticKey::TbY, 7.268}, {ArticKey::Jaw, 0.573}, {ArticKey::LipAp, 0.371}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.301}, {ArticKey::Root, 0.255}, {ArticKey::Larynx, 0.382}, {ArticKey::TtX, 6.076}, {ArticKey::TtY, 6.981}, {ArticKey::TtCurl, 0}}) },
    { "female", "u", makePatch({{ArticKey::TbX, 2.243}, {ArticKey::TbY, 7.381}, {ArticKey::Jaw, 0.266}, {ArticKey::LipAp, 0.187}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.301}, {ArticKey::Root, 0.205}, {ArticKey::Larynx, 0.24}, {ArticKey::TtX, 6.164}, {ArticKey::TtY, 7.296}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u028c", makePatch({{ArticKey::TbX, 1.87}, {ArticKey::TbY, 6.262}, {ArticKey::Jaw, 1.067}, {ArticKey::LipAp, 0.985}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.196}, {ArticKey::Larynx, 0.087}, {ArticKey::TtX, 5.747}, {ArticKey::TtY, 6.198}, {ArticKey::TtCurl, 0.002}}) },
    { "female", "\u0259", makePatch({{ArticKey::TbX, 2.996}, {ArticKey::TbY, 6.841}, {ArticKey::Jaw, 0.818}, {ArticKey::LipAp, 0.599}, {ArticKey::LipProt, 0.351}, {ArticKey::LipWidth, 0.544}, {ArticKey::Root, 0.279}, {ArticKey::Larynx, 0.14}, {ArticKey::TtX, 6.186}, {ArticKey::TtY, 6.614}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0250", makePatch({{ArticKey::TbX, 2.369}, {ArticKey::TbY, 6.131}, {ArticKey::Jaw, 1.408}, {ArticKey::LipAp, 1.232}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.112}, {ArticKey::Larynx, 0.009}, {ArticKey::TtX, 5.75}, {ArticKey::TtY, 5.854}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u025d", makePatch({{ArticKey::TbX, 2.581}, {ArticKey::TbY, 7.338}, {ArticKey::Jaw, 0.694}, {ArticKey::LipAp, 0.657}, {ArticKey::LipProt, 0.59}, {ArticKey::LipWidth, 0.349}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -0.832}, {ArticKey::TtX, 5.71}, {ArticKey::TtY, 8.898}, {ArticKey::TtCurl, 1}}) },
    { "female", "y", makePatch({{ArticKey::TbX, 3.481}, {ArticKey::TbY, 7.85}, {ArticKey::Jaw, 0.228}, {ArticKey::LipAp, 0.301}, {ArticKey::LipProt, 0.6}, {ArticKey::LipWidth, 0.301}, {ArticKey::Root, -0.26}, {ArticKey::Larynx, -0.123}, {ArticKey::TtX, 6.549}, {ArticKey::TtY, 7.494}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u00f8", makePatch({{ArticKey::TbX, 3.455}, {ArticKey::TbY, 7.417}, {ArticKey::Jaw, 0.651}, {ArticKey::LipAp, 0.51}, {ArticKey::LipProt, 1.215}, {ArticKey::LipWidth, 0.3}, {ArticKey::Root, -0.033}, {ArticKey::Larynx, -0.029}, {ArticKey::TtX, 6.389}, {ArticKey::TtY, 6.963}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0153", makePatch({{ArticKey::TbX, 3.051}, {ArticKey::TbY, 6.641}, {ArticKey::Jaw, 1.015}, {ArticKey::LipAp, 0.75}, {ArticKey::LipProt, 0.575}, {ArticKey::LipWidth, 0.381}, {ArticKey::Root, -0.228}, {ArticKey::Larynx, 0.162}, {ArticKey::TtX, 6.124}, {ArticKey::TtY, 6.372}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0268", makePatch({{ArticKey::TbX, 2.866}, {ArticKey::TbY, 7.731}, {ArticKey::Jaw, 0.195}, {ArticKey::LipAp, 0.568}, {ArticKey::LipProt, 0.35}, {ArticKey::LipWidth, 1}, {ArticKey::Root, 0.337}, {ArticKey::Larynx, 0.172}, {ArticKey::TtX, 6.376}, {ArticKey::TtY, 7.482}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u026f", makePatch({{ArticKey::TbX, 2.65}, {ArticKey::TbY, 7.646}, {ArticKey::Jaw, 0.185}, {ArticKey::LipAp, 0.368}, {ArticKey::LipProt, 0.061}, {ArticKey::LipWidth, 0.602}, {ArticKey::Root, 0.352}, {ArticKey::Larynx, 0.19}, {ArticKey::TtX, 6.314}, {ArticKey::TtY, 7.462}, {ArticKey::TtCurl, 0}}) },
    { "female", "\u0264", makePatch({{ArticKey::TbX, 2.318}, {ArticKey::TbY, 7.294}, {ArticKey::Jaw, 0.692}, {ArticKey::LipAp, 0.518}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 0.911}, {ArticKey::Root, 0.428}, {ArticKey::Larynx, 0.274}, {ArticKey::TtX, 6.032}, {ArticKey::TtY, 6.886}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "i", makePatch({{ArticKey::TbX, 3.719}, {ArticKey::TbY, 7.754}, {ArticKey::Jaw, 0.096}, {ArticKey::LipAp, 0.386}, {ArticKey::LipProt, 0.004}, {ArticKey::LipWidth, 0.655}, {ArticKey::Root, 0.095}, {ArticKey::Larynx, -0.001}, {ArticKey::TtX, 6.664}, {ArticKey::TtY, 7.581}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u026a", makePatch({{ArticKey::TbX, 3.381}, {ArticKey::TbY, 7.692}, {ArticKey::Jaw, 0.489}, {ArticKey::LipAp, 0.366}, {ArticKey::LipProt, 0.001}, {ArticKey::LipWidth, 0.55}, {ArticKey::Root, 0.005}, {ArticKey::Larynx, -0.025}, {ArticKey::TtX, 6.427}, {ArticKey::TtY, 7.204}, {ArticKey::TtCurl, 0}}) },
    { "child", "e", makePatch({{ArticKey::TbX, 3.477}, {ArticKey::TbY, 7.552}, {ArticKey::Jaw, 0.676}, {ArticKey::LipAp, 0.717}, {ArticKey::LipProt, 0.351}, {ArticKey::LipWidth, 0.55}, {ArticKey::Root, -0.219}, {ArticKey::Larynx, -0.14}, {ArticKey::TtX, 6.386}, {ArticKey::TtY, 6.989}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u025b", makePatch({{ArticKey::TbX, 3.101}, {ArticKey::TbY, 6.836}, {ArticKey::Jaw, 1.071}, {ArticKey::LipAp, 1.177}, {ArticKey::LipProt, 0.355}, {ArticKey::LipWidth, 0.548}, {ArticKey::Root, -0.339}, {ArticKey::Larynx, -0.168}, {ArticKey::TtX, 6.115}, {ArticKey::TtY, 6.391}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u00e6", makePatch({{ArticKey::TbX, 3.496}, {ArticKey::TbY, 6.678}, {ArticKey::Jaw, 1.367}, {ArticKey::LipAp, 1.601}, {ArticKey::LipProt, 0.356}, {ArticKey::LipWidth, 0.548}, {ArticKey::Root, -0.534}, {ArticKey::Larynx, -0.23}, {ArticKey::TtX, 6.106}, {ArticKey::TtY, 6.08}, {ArticKey::TtCurl, 0}}) },
    { "child", "a", makePatch({{ArticKey::TbX, 2.008}, {ArticKey::TbY, 6.239}, {ArticKey::Jaw, 1.816}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0.042}, {ArticKey::LipWidth, 1}, {ArticKey::Root, -0.753}, {ArticKey::Larynx, -0.092}, {ArticKey::TtX, 5.452}, {ArticKey::TtY, 5.547}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0251", makePatch({{ArticKey::TbX, 1.651}, {ArticKey::TbY, 6.308}, {ArticKey::Jaw, 1.905}, {ArticKey::LipAp, 2}, {ArticKey::LipProt, 0.361}, {ArticKey::LipWidth, 0.952}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -0.353}, {ArticKey::TtX, 5.302}, {ArticKey::TtY, 5.497}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0252", makePatch({{ArticKey::TbX, 1.2}, {ArticKey::TbY, 6.233}, {ArticKey::Jaw, 1.785}, {ArticKey::LipAp, 0.95}, {ArticKey::LipProt, 1.286}, {ArticKey::LipWidth, 0.486}, {ArticKey::Root, -0.151}, {ArticKey::Larynx, -0.253}, {ArticKey::TtX, 5.225}, {ArticKey::TtY, 5.571}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0254", makePatch({{ArticKey::TbX, 1.2}, {ArticKey::TbY, 6.764}, {ArticKey::Jaw, 1.408}, {ArticKey::LipAp, 0.762}, {ArticKey::LipProt, 1.086}, {ArticKey::LipWidth, 0.461}, {ArticKey::Root, -0.561}, {ArticKey::Larynx, -0.049}, {ArticKey::TtX, 5.4}, {ArticKey::TtY, 6.076}, {ArticKey::TtCurl, 0}}) },
    { "child", "o", makePatch({{ArticKey::TbX, 1.814}, {ArticKey::TbY, 7.259}, {ArticKey::Jaw, 0.78}, {ArticKey::LipAp, 0.131}, {ArticKey::LipProt, 1.241}, {ArticKey::LipWidth, 0.301}, {ArticKey::Root, -0.566}, {ArticKey::Larynx, -0.209}, {ArticKey::TtX, 5.847}, {ArticKey::TtY, 6.794}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u028a", makePatch({{ArticKey::TbX, 2.338}, {ArticKey::TbY, 7.482}, {ArticKey::Jaw, 0.498}, {ArticKey::LipAp, 0.335}, {ArticKey::LipProt, 0.6}, {ArticKey::LipWidth, 0.279}, {ArticKey::Root, -0.498}, {ArticKey::Larynx, -0.249}, {ArticKey::TtX, 6.111}, {ArticKey::TtY, 7.122}, {ArticKey::TtCurl, 0}}) },
    { "child", "u", makePatch({{ArticKey::TbX, 2.324}, {ArticKey::TbY, 7.563}, {ArticKey::Jaw, 0.162}, {ArticKey::LipAp, 0.301}, {ArticKey::LipProt, 0.637}, {ArticKey::LipWidth, 0.054}, {ArticKey::Root, -0.646}, {ArticKey::Larynx, -0.327}, {ArticKey::TtX, 6.224}, {ArticKey::TtY, 7.453}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u028c", makePatch({{ArticKey::TbX, 1.88}, {ArticKey::TbY, 7.088}, {ArticKey::Jaw, 0.861}, {ArticKey::LipAp, 0.569}, {ArticKey::LipProt, 0.369}, {ArticKey::LipWidth, 0.516}, {ArticKey::Root, -0.189}, {ArticKey::Larynx, -0.157}, {ArticKey::TtX, 5.835}, {ArticKey::TtY, 6.664}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0259", makePatch({{ArticKey::TbX, 2.698}, {ArticKey::TbY, 7.382}, {ArticKey::Jaw, 0.804}, {ArticKey::LipAp, 0.569}, {ArticKey::LipProt, 0.359}, {ArticKey::LipWidth, 0.535}, {ArticKey::Root, -0.503}, {ArticKey::Larynx, -0.379}, {ArticKey::TtX, 6.102}, {ArticKey::TtY, 6.817}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0250", makePatch({{ArticKey::TbX, 2.184}, {ArticKey::TbY, 6.6}, {ArticKey::Jaw, 1.355}, {ArticKey::LipAp, 0.981}, {ArticKey::LipProt, 0.373}, {ArticKey::LipWidth, 0.524}, {ArticKey::Root, -0.358}, {ArticKey::Larynx, -0.292}, {ArticKey::TtX, 5.718}, {ArticKey::TtY, 6.066}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u025d", makePatch({{ArticKey::TbX, 1.859}, {ArticKey::TbY, 7}, {ArticKey::Jaw, 0.05}, {ArticKey::LipAp, 0.12}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.214}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -1.288}, {ArticKey::TtX, 5.722}, {ArticKey::TtY, 9.357}, {ArticKey::TtCurl, 1}}) },
    { "child", "y", makePatch({{ArticKey::TbX, 3.543}, {ArticKey::TbY, 7.972}, {ArticKey::Jaw, 0.242}, {ArticKey::LipAp, 0.284}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.301}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, 0.121}, {ArticKey::TtX, 6.562}, {ArticKey::TtY, 7.524}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u00f8", makePatch({{ArticKey::TbX, 3.448}, {ArticKey::TbY, 7.622}, {ArticKey::Jaw, 0.691}, {ArticKey::LipAp, 0.253}, {ArticKey::LipProt, 1.4}, {ArticKey::LipWidth, 0.302}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -0.569}, {ArticKey::TtX, 6.371}, {ArticKey::TtY, 7}, {ArticKey::TtCurl, 0}}) },
    { "child", "\u0153", makePatch({{ArticKey::TbX, 2.95}, {ArticKey::TbY, 7.017}, {ArticKey::Jaw, 1.143}, {ArticKey::LipAp, 0.693}, {ArticKey::LipProt, 1.143}, {ArticKey::LipWidth, 0.316}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, -0.432}, {ArticKey::TtX, 6.039}, {ArticKey::TtY, 6.394}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u0268", makePatch({{ArticKey::TbX, 3.095}, {ArticKey::TbY, 7.893}, {ArticKey::Jaw, 0.201}, {ArticKey::LipAp, 0.172}, {ArticKey::LipProt, 0}, {ArticKey::LipWidth, 0.549}, {ArticKey::Root, -0.9}, {ArticKey::Larynx, 0.16}, {ArticKey::TtX, 6.442}, {ArticKey::TtY, 7.536}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u026f", makePatch({{ArticKey::TbX, 2.712}, {ArticKey::TbY, 7.767}, {ArticKey::Jaw, 0.155}, {ArticKey::LipAp, 0.219}, {ArticKey::LipProt, 0.353}, {ArticKey::LipWidth, 0.55}, {ArticKey::Root, -0.561}, {ArticKey::Larynx, -0.427}, {ArticKey::TtX, 6.343}, {ArticKey::TtY, 7.533}, {ArticKey::TtCurl, 0.001}}) },
    { "child", "\u0264", makePatch({{ArticKey::TbX, 1.935}, {ArticKey::TbY, 7.365}, {ArticKey::Jaw, 0.721}, {ArticKey::LipAp, 0.838}, {ArticKey::LipProt, 0.1}, {ArticKey::LipWidth, 0.633}, {ArticKey::Root, -0.448}, {ArticKey::Larynx, -0.208}, {ArticKey::TtX, 5.906}, {ArticKey::TtY, 6.885}, {ArticKey::TtCurl, 0}}) },
};
static constexpr size_t kNumVowels = 66;
static const ConsRow kCons[] = {
    { "male", "\u0278", makePatch({{ArticKey::LipAp, 0.032}}) },
    { "male", "\u03b2", makePatch({{ArticKey::LipAp, 0.04}}) },
    { "male", "f", makePatch({{ArticKey::LipAp, 0.016}}) },
    { "male", "v", makePatch({{ArticKey::LipAp, 0.031}}) },
    { "male", "\u03b8", makePatch({{ArticKey::TtY, 8.225}}) },
    { "male", "\u00f0", makePatch({{ArticKey::TtY, 8.19}}) },
    { "male", "s", makePatch({{ArticKey::TtY, 9.111}}) },
    { "male", "z", makePatch({{ArticKey::TtY, 9.097}}) },
    { "male", "\u0283", makePatch({{ArticKey::TtY, 9.512}}) },
    { "male", "\u0292", makePatch({{ArticKey::TtY, 9.495}}) },
    { "male", "\u0282", makePatch({{ArticKey::TtY, 9.73}}) },
    { "male", "\u0290", makePatch({{ArticKey::TtY, 9.718}}) },
    { "male", "\u00e7", makePatch({{ArticKey::TbY, 7.93}}) },
    { "male", "\u029d", makePatch({{ArticKey::TbY, 7.917}}) },
    { "male", "x", makePatch({{ArticKey::TbY, 7.365}}) },
    { "male", "\u0263", makePatch({{ArticKey::TbY, 7.352}}) },
    { "male", "\u03c7", makePatch({{ArticKey::TbY, 7.061}}) },
    { "male", "\u0281", makePatch({{ArticKey::TbY, 7.049}}) },
    { "male", "\u0127", makePatch({{ArticKey::TbX, 1.247}}) },
    { "male", "\u0295", makePatch({{ArticKey::TbX, 1.284}}) },
    { "female", "\u0278", makePatch({{ArticKey::LipAp, 0.033}}) },
    { "female", "\u03b2", makePatch({{ArticKey::LipAp, 0.042}}) },
    { "female", "f", makePatch({{ArticKey::LipAp, 0.013}}) },
    { "female", "v", makePatch({{ArticKey::LipAp, 0.028}}) },
    { "female", "\u03b8", makePatch({{ArticKey::TtY, 8.242}}) },
    { "female", "\u00f0", makePatch({{ArticKey::TtY, 8.208}}) },
    { "female", "s", makePatch({{ArticKey::TtY, 9.117}}) },
    { "female", "z", makePatch({{ArticKey::TtY, 9.104}}) },
    { "female", "\u0283", makePatch({{ArticKey::TtY, 9.521}}) },
    { "female", "\u0292", makePatch({{ArticKey::TtY, 9.505}}) },
    { "female", "\u0282", makePatch({{ArticKey::TtY, 9.738}}) },
    { "female", "\u0290", makePatch({{ArticKey::TtY, 9.727}}) },
    { "female", "\u00e7", makePatch({{ArticKey::TbY, 7.985}}) },
    { "female", "\u029d", makePatch({{ArticKey::TbY, 7.973}}) },
    { "female", "x", makePatch({{ArticKey::TbY, 7.43}}) },
    { "female", "\u0263", makePatch({{ArticKey::TbY, 7.417}}) },
    { "female", "\u03c7", makePatch({{ArticKey::TbY, 7.122}}) },
    { "female", "\u0281", makePatch({{ArticKey::TbY, 7.11}}) },
    { "female", "\u0127", makePatch({{ArticKey::TbX, 1.185}}) },
    { "female", "\u0295", makePatch({{ArticKey::TbX, 1.22}}) },
    { "child", "\u0278", makePatch({{ArticKey::LipAp, 0.038}}) },
    { "child", "\u03b2", makePatch({{ArticKey::LipAp, 0.048}}) },
    { "child", "f", makePatch({{ArticKey::LipAp, 0.006}}) },
    { "child", "v", makePatch({{ArticKey::LipAp, 0.02}}) },
    { "child", "\u03b8", makePatch({{ArticKey::TtY, 8.269}}) },
    { "child", "\u00f0", makePatch({{ArticKey::TtY, 8.244}}) },
    { "child", "s", makePatch({{ArticKey::TtY, 9.126}}) },
    { "child", "z", makePatch({{ArticKey::TtY, 9.114}}) },
    { "child", "\u0283", makePatch({{ArticKey::TtY, 9.538}}) },
    { "child", "\u0292", makePatch({{ArticKey::TtY, 9.524}}) },
    { "child", "\u0282", makePatch({{ArticKey::TtY, 9.75}}) },
    { "child", "\u0290", makePatch({{ArticKey::TtY, 9.739}}) },
    { "child", "\u00e7", makePatch({{ArticKey::TbY, 8.043}}) },
    { "child", "\u029d", makePatch({{ArticKey::TbY, 8.032}}) },
    { "child", "x", makePatch({{ArticKey::TbY, 7.499}}) },
    { "child", "\u0263", makePatch({{ArticKey::TbY, 7.487}}) },
    { "child", "\u03c7", makePatch({{ArticKey::TbY, 7.187}}) },
    { "child", "\u0281", makePatch({{ArticKey::TbY, 7.176}}) },
    { "child", "\u0127", makePatch({{ArticKey::TbX, 1.106}}) },
    { "child", "\u0295", makePatch({{ArticKey::TbX, 1.138}}) },
};
static constexpr size_t kNumCons = 60;
} // namespace tables


static std::unordered_map<std::string, ArticPatch> gVowel[3];
static std::unordered_map<std::string, ArticPatch> gCons[3];

static int bodyIndex(BodyName b) {
    switch (b) { case BodyName::Female: return 1; case BodyName::Child: return 2; default: return 0; }
}
static const char* bodyNameStr(BodyName b) {
    switch (b) { case BodyName::Female: return "female"; case BodyName::Child: return "child"; default: return "male"; }
}

static void fillCalTables() {
    for (int i=0;i<3;++i) { gVowel[i].clear(); gCons[i].clear(); }
    for (size_t i=0;i<tables::kNumVowels;++i) {
        const auto& r = tables::kVowels[i];
        int b = r.body[0]=='f' ? 1 : r.body[0]=='c' ? 2 : 0;
        gVowel[b].emplace(r.ipa, r.artic);
    }
    for (size_t i=0;i<tables::kNumCons;++i) {
        const auto& r = tables::kCons[i];
        int b = r.body[0]=='f' ? 1 : r.body[0]=='c' ? 2 : 0;
        gCons[b].emplace(r.ipa, r.artic);
    }
}

void initInventory() {
    std::lock_guard<std::mutex> lock(gLock);
    if (gReady) return;
    buildBase();
    fillCalTables();
    gReady = true;
}

ArticPatch blendBodies(const BodyMix& body, const ArticPatch& male, const ArticPatch& female, const ArticPatch& child) {
    if (auto p = body.preset()) {
        switch (*p) {
            case BodyName::Female: return female;
            case BodyName::Child: return child;
            default: return male;
        }
    }
    std::pair<double, ArticPatch> parts[3];
    double w[3]; body.weights(w);
    parts[0] = {w[0], male};
    parts[1] = {w[1], female};
    parts[2] = {w[2], child};
    return ArticPatch::blend(parts, 3);
}

const PhonemeDef* phonemeByIpa(std::string_view ipa) {
    initInventory();
    auto it = gByIpa.find(std::string(ipa));
    if (it == gByIpa.end()) return nullptr;
    return &gBase[it->second];
}

std::optional<PhonemeDef> phoneme(std::string_view ipa, const BodyMix& body) {
    auto* p = phonemeByIpa(ipa);
    if (!p) return std::nullopt;
    PhonemeDef out = *p;
    auto getc = [&](BodyName b) {
        auto it = gCons[bodyIndex(b)].find(std::string(ipa));
        if (it == gCons[bodyIndex(b)].end()) return p->targets;
        ArticPatch t = p->targets; t.merge(it->second); return t;
    };
    out.targets = blendBodies(body, getc(BodyName::Male), getc(BodyName::Female), getc(BodyName::Child));
    return out;
}

const std::vector<PhonemeDef>& consonants() { initInventory(); return gBase; }

static const ArticKey kVowelKeys[] = {
    ArticKey::TbX, ArticKey::TbY, ArticKey::Jaw, ArticKey::LipAp, ArticKey::LipProt,
    ArticKey::LipWidth, ArticKey::Root, ArticKey::Larynx, ArticKey::TtCurl
};

std::optional<ArticPatch> presetVowelArtic(std::string_view ipa, BodyName body) {
    initInventory();
    auto lookup = [&](BodyName b) -> const ArticPatch* {
        auto it = gVowel[bodyIndex(b)].find(std::string(ipa));
        if (it != gVowel[bodyIndex(b)].end()) return &it->second;
        return nullptr;
    };
    const ArticPatch* v = lookup(body);
    if (!v) v = lookup(BodyName::Male);
    if (!v) return std::nullopt;
    ArticPatch out;
    for (auto k : kVowelKeys)
        if (auto x = v->get(k)) out.set(k, *x);
    if (v->get(ArticKey::TtCurl).value_or(0.0) > 0.05) {
        if (auto x = v->get(ArticKey::TtX)) out.set(ArticKey::TtX, *x);
        if (auto x = v->get(ArticKey::TtY)) out.set(ArticKey::TtY, *x);
    }
    return out;
}

std::optional<ArticPatch> vowelArtic(std::string_view ipa, const BodyMix& body) {
    if (auto p = body.preset())
        return presetVowelArtic(ipa, *p);
    ArticPatch partsA[3];
    std::pair<double, ArticPatch> parts[3];
    double w[3]; body.weights(w);
    BodyName names[3] = { BodyName::Male, BodyName::Female, BodyName::Child };
    for (int i=0;i<3;++i) {
        if (w[i] <= 0.0) { parts[i] = {0.0, ArticPatch{}}; continue; }
        auto a = presetVowelArtic(ipa, names[i]);
        if (!a) return std::nullopt;
        parts[i] = {w[i], *a};
    }
    return ArticPatch::blend(parts, 3);
}

const char* const kDiphthongA[][3] = {
    {"aɪ", "a", "ɪ"},
    {"aʊ", "a", "ʊ"},
    {"ɔɪ", "ɔ", "ɪ"},
    {"eɪ", "e", "i"},
    {"oʊ", "o", "u"},
    {"ɚ",  "ɝ", nullptr}
};

const char* const* expandDiphthong(std::string_view ipa) {
    for (auto& row : kDiphthongA)
        if (ipa == row[0]) return row + 1;
    return nullptr;
}

bool isVowelIpa(std::string_view ipa, const BodyMix& body) {
    if (expandDiphthong(ipa)) return true;
    if (vowelArtic(ipa, body)) return true;
    return false;
}

} // namespace phonetics
} // namespace dooblrthroat::dsp
