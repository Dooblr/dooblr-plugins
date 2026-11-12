#include "Klatt.h"
#include "Math.h"
#include "Physics.h"
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>

namespace dooblrthroat::dsp
{
    namespace
    {
        constexpr double kHop = 0.005;
        constexpr double kPi = 3.14159265358979323846;

        enum Place { Labial, Dental, Alveolar, Post, Palatal, Velar, Glottal, None };

        enum class KManner { Vowel, Stop, Fricative, Affricate, Nasal, Approximant, Lateral, GlottalC };

        struct Phone
        {
            const char* ipa;
            KManner manner;
            bool voiced;
            Place place;
            // Mid-segment formants and bandwidths (male, Hz). Vowels also
            // carry Hillenbrand 1995 female and child F1-F4 in the f*f / f*c slots.
            float f1, b1, f2, b2, f3, b3, f4, b4, f5, b5;
            float f1f, f2f, f3f, f4f;
            float f1c, f2c, f3c, f4c;
            // Consonant loci (0 = use mid f2/f3).
            float f2loc, f3loc;
            float av, ah, af;
            // Parallel amplitudes, linear 0..1.
            float a2, a3, a4, a5, a6, ab;
            float fnp, fnz;
            // HLsyn-style default areas (cm^2): glottal, constriction, nasal coupling.
            float ag, ac, an;
        };

        // Formant targets: Peterson & Barney 1952 men, filled with Hillenbrand 1995
        // and typical Klatt 1980 bandwidths / F4-F5. Loci follow published
        // Sussman-style English locus equations / Klatt transition rules.
        const Phone kPhones[] = {
            // Vowels — F1–F4 from Hillenbrand, Getty, Clark & Wheeler 1995
            // (men / women / children). Bandwidths and F5 follow Klatt 1980 defaults.
            { "i",  KManner::Vowel, true, None, 342, 60, 2322, 90, 3000, 150, 3657, 200, 4500, 200, 437, 2761, 3372, 4352, 452, 3081, 3702, 4418, 0, 0, 0.90f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.2f, 0 },
            { "ɪ",  KManner::Vowel, true, None, 427, 70, 2034, 100, 2684, 150, 3618, 200, 4500, 200, 483, 2365, 3053, 4334, 529, 2733, 3605, 4448, 0, 0, 0.86f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },
            { "e",  KManner::Vowel, true, None, 476, 70, 2089, 100, 2691, 150, 3677, 200, 4500, 200, 536, 2530, 3047, 4315, 564, 2770, 3520, 4450, 0, 0, 0.88f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.1f, 0 },
            { "ɛ",  KManner::Vowel, true, None, 580, 80, 1799, 100, 2605, 150, 3677, 200, 4500, 200, 731, 2058, 2979, 4299, 695, 2610, 3578, 4479, 0, 0, 0.88f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.1f, 0 },
            { "æ",  KManner::Vowel, true, None, 588, 90, 1952, 110, 2601, 160, 3624, 200, 4500, 200, 669, 2349, 2972, 4290, 717, 2501, 3507, 4422, 0, 0, 0.90f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.3f, 0 },
            { "a",  KManner::Vowel, true, None, 750, 90, 1400, 110, 2500, 160, 3600, 200, 4500, 200, 900, 1600, 2850, 4250, 980, 1750, 3200, 4400, 0, 0, 0.92f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.3f, 0 },
            { "ɑ",  KManner::Vowel, true, None, 768, 90, 1333, 110, 2522, 160, 3687, 200, 4500, 200, 936, 1551, 2815, 4299, 1002, 1688, 3194, 4418, 0, 0, 0.92f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.4f, 0 },
            { "ɒ",  KManner::Vowel, true, None, 720, 90, 1100, 110, 2500, 160, 3500, 200, 4500, 200, 850, 1250, 2850, 4150, 900, 1350, 3150, 4200, 0, 0, 0.90f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.2f, 0 },
            { "ɔ",  KManner::Vowel, true, None, 652, 80,  997, 100, 2538, 150, 3486, 200, 4500, 200, 781, 1136, 2824, 4112, 757, 1215, 3159, 4065, 0, 0, 0.90f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.2f, 0 },
            { "o",  KManner::Vowel, true, None, 497, 70,  910,  90, 2459, 150, 3382, 200, 4500, 200, 555, 1035, 2828, 3927, 586, 1065, 3205, 4000, 0, 0, 0.88f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },
            { "ʊ",  KManner::Vowel, true, None, 469, 70, 1122, 100, 2434, 150, 3400, 200, 4500, 200, 519, 1228, 2829, 4052, 568, 1401, 3319, 4128, 0, 0, 0.86f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.9f, 0 },
            { "u",  KManner::Vowel, true, None, 378, 60,  997,  90, 2343, 150, 3357, 200, 4500, 200, 459, 1105, 2735, 4115, 494, 1345, 3192, 4334, 0, 0, 0.88f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.8f, 0 },
            { "ʌ",  KManner::Vowel, true, None, 623, 80, 1200, 100, 2550, 150, 3298, 200, 4500, 200, 753, 1426, 2933, 4098, 694, 1600, 3364, 4318, 0, 0, 0.88f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.1f, 0 },
            { "ə",  KManner::Vowel, true, None, 500, 80, 1500, 100, 2490, 150, 3500, 200, 4500, 200, 600, 1750, 2900, 4200, 620, 1900, 3300, 4400, 0, 0, 0.74f, 0.06f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },
            { "ɐ",  KManner::Vowel, true, None, 650, 80, 1400, 100, 2500, 150, 3500, 200, 4500, 200, 780, 1600, 2900, 4200, 800, 1700, 3250, 4350, 0, 0, 0.84f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.2f, 0 },
            { "ɝ",  KManner::Vowel, true, None, 474, 80, 1379, 100, 1710, 120, 3232, 200, 4500, 200, 523, 1588, 1929, 3914, 586, 1653, 2274, 3956, 0, 0, 0.86f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },
            { "ɚ",  KManner::Vowel, true, None, 480, 80, 1370, 100, 1710, 120, 3250, 200, 4500, 200, 520, 1580, 1930, 3900, 580, 1650, 2270, 3950, 0, 0, 0.76f, 0.06f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },
            { "ɨ",  KManner::Vowel, true, None, 400, 70, 1700, 100, 2500, 150, 3500, 200, 4500, 200, 480, 2000, 3000, 4200, 520, 2200, 3400, 4400, 0, 0, 0.76f, 0.05f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 1.0f, 0 },

            // Stops. Mid formants are closure (low F1); loci used at release.
            { "p",  KManner::Stop, false, Labial,    180, 80,  900, 120, 2100, 180, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 900, 2100, 0,    0.08f, 0, 0.15f, 0.05f, 0.05f, 0.10f, 0.10f, 0.55f, 270, 270, 0.20f, 0.00f, 0.00f },
            { "b",  KManner::Stop, true,  Labial,    180, 80,  900, 120, 2100, 180, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 900, 2100, 0.28f, 0.02f, 0, 0.12f, 0.05f, 0.05f, 0.08f, 0.08f, 0.45f, 270, 270, 0.05f, 0.00f, 0.00f },
            { "t",  KManner::Stop, false, Alveolar,  180, 80, 1800, 120, 2600, 180, 3600, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 2600, 0,    0.10f, 0, 0.05f, 0.10f, 0.35f, 0.45f, 0.25f, 0.15f, 270, 270, 0.20f, 0.00f, 0.00f },
            { "d",  KManner::Stop, true,  Alveolar,  180, 80, 1800, 120, 2600, 180, 3600, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 2600, 0.30f, 0.02f, 0, 0.05f, 0.08f, 0.28f, 0.30f, 0.15f, 0.10f, 270, 270, 0.05f, 0.00f, 0.00f },
            { "k",  KManner::Stop, false, Velar,     180, 80, 1400, 120, 2000, 180, 3500, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1400, 2000, 0,    0.10f, 0, 0.15f, 0.40f, 0.25f, 0.10f, 0.08f, 0.10f, 270, 270, 0.20f, 0.00f, 0.00f },
            { "g",  KManner::Stop, true,  Velar,     180, 80, 1400, 120, 2000, 180, 3500, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1400, 2000, 0.30f, 0.02f, 0, 0.12f, 0.32f, 0.18f, 0.08f, 0.06f, 0.08f, 270, 270, 0.05f, 0.00f, 0.00f },
            { "ʔ",  KManner::Stop, false, Glottal,   150, 80, 1500, 150, 2500, 200, 3500, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 2500, 0,    0,     0, 0,0,0,0,0,0, 270, 270, 0.20f, 0.00f, 0.00f },

            // Affricates
            { "tʃ", KManner::Affricate, false, Post, 200, 90, 1900, 130, 2300, 180, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1900, 2300, 0,    0.06f, 0.7f, 0.10f, 0.45f, 0.55f, 0.25f, 0.10f, 0.08f, 270, 270, 0.18f, 0.08f, 0.00f },
            { "dʒ", KManner::Affricate, true,  Post, 200, 90, 1900, 130, 2300, 180, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1900, 2300, 0.28f, 0.02f, 0.55f, 0.08f, 0.40f, 0.45f, 0.20f, 0.08f, 0.06f, 270, 270, 0.05f, 0.08f, 0.00f },

            // Fricatives
            { "f",  KManner::Fricative, false, Labial,   400, 200, 1000, 200, 2400, 250, 3500, 300, 4500, 300, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 2400, 0,    0.04f, 0.85f, 0.10f, 0.10f, 0.15f, 0.25f, 0.30f, 0.80f, 270, 270, 0.22f, 0.10f, 0.00f },
            { "v",  KManner::Fricative, true,  Labial,   400, 150, 1000, 180, 2400, 220, 3500, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 2400, 0.40f, 0.03f, 0.55f, 0.08f, 0.08f, 0.12f, 0.18f, 0.22f, 0.55f, 270, 270, 0.05f, 0.10f, 0.00f },
            { "θ",  KManner::Fricative, false, Dental,   400, 200, 1400, 200, 2500, 250, 3600, 300, 4500, 300, 0, 0, 0, 0, 0, 0, 0, 0, 1400, 2500, 0,    0.04f, 0.70f, 0.08f, 0.10f, 0.20f, 0.30f, 0.35f, 0.55f, 270, 270, 0.22f, 0.10f, 0.00f },
            { "ð",  KManner::Fricative, true,  Dental,   400, 150, 1400, 180, 2500, 220, 3600, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1400, 2500, 0.40f, 0.03f, 0.45f, 0.06f, 0.08f, 0.15f, 0.20f, 0.22f, 0.40f, 270, 270, 0.05f, 0.10f, 0.00f },
            { "s",  KManner::Fricative, false, Alveolar, 400, 200, 1700, 200, 2700, 200, 4000, 250, 5500, 300, 0, 0, 0, 0, 0, 0, 0, 0, 1700, 2700, 0,    0.03f, 1.00f, 0.04f, 0.08f, 0.20f, 0.70f, 0.85f, 0.25f, 270, 270, 0.22f, 0.10f, 0.00f },
            { "z",  KManner::Fricative, true,  Alveolar, 400, 150, 1700, 180, 2700, 180, 4000, 220, 5500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1700, 2700, 0.42f, 0.03f, 0.70f, 0.04f, 0.06f, 0.16f, 0.50f, 0.60f, 0.18f, 270, 270, 0.05f, 0.10f, 0.00f },
            { "ʃ",  KManner::Fricative, false, Post,     400, 200, 1800, 200, 2400, 200, 3200, 250, 4500, 300, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 2300, 0,    0.03f, 0.95f, 0.08f, 0.45f, 0.70f, 0.40f, 0.15f, 0.12f, 270, 270, 0.22f, 0.10f, 0.00f },
            { "ʒ",  KManner::Fricative, true,  Post,     400, 150, 1800, 180, 2400, 180, 3200, 220, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 2300, 0.42f, 0.03f, 0.65f, 0.06f, 0.35f, 0.55f, 0.30f, 0.12f, 0.10f, 270, 270, 0.05f, 0.10f, 0.00f },
            { "h",  KManner::Fricative, false, Glottal,  500, 150, 1500, 200, 2500, 250, 3500, 300, 4500, 300, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 2500, 0,    0.85f, 0.15f, 0.05f, 0.05f, 0.05f, 0.08f, 0.08f, 0.20f, 270, 270, 0.30f, 1.00f, 0.00f },

            // Nasals
            { "m",  KManner::Nasal, true, Labial,    250, 80, 1000, 120, 2100, 180, 3300, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 900, 2100, 0.75f, 0.02f, 0, 0.05f, 0.04f, 0.03f, 0.02f, 0.02f, 0.02f, 250, 1000, 0.05f, 0.00f, 0.35f },
            { "n",  KManner::Nasal, true, Alveolar,  250, 80, 1600, 120, 2600, 180, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1700, 2600, 0.75f, 0.02f, 0, 0.05f, 0.04f, 0.03f, 0.02f, 0.02f, 0.02f, 270, 1450, 0.05f, 0.00f, 0.35f },
            { "ŋ",  KManner::Nasal, true, Velar,     250, 80, 1200, 120, 2000, 180, 3300, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1200, 2000, 0.75f, 0.02f, 0, 0.05f, 0.04f, 0.03f, 0.02f, 0.02f, 0.02f, 270, 1600, 0.05f, 0.00f, 0.35f },

            // Approximants / liquids
            { "w",  KManner::Approximant, true, Labial,    300, 70,  700,  90, 2200, 150, 3300, 200, 4500, 200, 0, 0, 0, 0, 0, 0, 0, 0, 700, 2200, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
            { "j",  KManner::Approximant, true, Palatal,   280, 70, 2100,  90, 3000, 150, 3500, 200, 4500, 200, 0, 0, 0, 0, 0, 0, 0, 0, 2100, 3000, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
            { "ɹ",  KManner::Approximant, true, Post,      350, 80, 1200, 100, 1650, 120, 3250, 200, 4500, 200, 0, 0, 0, 0, 0, 0, 0, 0, 1200, 1650, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
            { "r",  KManner::Approximant, true, Post,      350, 80, 1200, 100, 1650, 120, 3250, 200, 4500, 200, 0, 0, 0, 0, 0, 0, 0, 0, 1200, 1650, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
            { "l",  KManner::Lateral,     true, Alveolar,  350, 80, 1200, 100, 2600, 200, 3500, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 1200, 2600, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
            { "ɫ",  KManner::Lateral,     true, Velar,     350, 80,  900, 100, 2500, 200, 3400, 250, 4500, 250, 0, 0, 0, 0, 0, 0, 0, 0, 900, 2500, 0.80f, 0.04f, 0, 0,0,0,0,0,0, 270, 270, 0.05f, 0.35f, 0.00f },
        };

        const Phone kSchwa = kPhones[13];

        const Phone* findPhone (std::string_view ipa)
        {
            for (const auto& p : kPhones)
                if (ipa == p.ipa) return &p;
            return nullptr;
        }

        bool isVowelManner (KManner m) { return m == KManner::Vowel; }

        double formantScale (const BodyMix& body)
        {
            double w[3] { 1, 0, 0 };
            body.weights (w);
            return w[0] * 1.0 + w[1] * 1.17 + w[2] * 1.30;
        }

        void scaleFrame (KlattFrame& f, double s)
        {
            f.f1 = float (f.f1 * s); f.f2 = float (f.f2 * s); f.f3 = float (f.f3 * s);
            f.f4 = float (f.f4 * s); f.f5 = float (f.f5 * s);
            f.fnp = float (f.fnp * s); f.fnz = float (f.fnz * s);
        }

        void hlSynSources (double ag, double ac, double an, bool canVoice,
                           float& av, float& ah, float& af) noexcept
        {
            // Stevens & Bickley 1990 / Hanson & Stevens 2002 style mapping
            // (original implementation of the published constraints).
            const auto ag0 = 0.05;          // modal glottal area, cm^2
            const auto acSeal = 0.015;      // oral seal
            const auto acFricLo = 0.02;
            const auto acFricHi = 0.35;
            const auto ps = 8.0;            // cmH2O, nominal
            const auto flowG = ag * std::sqrt (ps);
            // Constriction flow vanishes when the oral tract is sealed (unless nasal).
            const auto oralOpen = (ac > acSeal) || (an > 0.05);
            const auto flowC = oralOpen ? flowG * (ac / (ac + ag + 0.05)) : 0.0;

            if (canVoice && ag > 0.02 && ag < 0.16 && oralOpen)
                av = float (clamp (1.0 - 3.0 * js_max (0.0, ag - ag0), 0.15, 1.0));
            else if (canVoice && ! oralOpen && an > 0.05)
                av = 0.70f; // nasal murmur
            else if (canVoice && ! oralOpen)
                av = 0.22f; // voicebar
            else
                av = 0.0f;

            // Aspiration when the glottis is abducted and something is open.
            const auto abduct = js_max (0.0, ag - 0.08);
            ah = (oralOpen || an > 0.05) ? float (clamp (4.0 * abduct * std::sqrt (ps) / 4.0, 0.0, 1.0)) : 0.0f;

            // Frication only in the constriction window with enough flow.
            if (ac > acFricLo && ac < acFricHi && flowC > 0.02)
            {
                const auto mid = 0.10;
                const auto win = 1.0 - clamp (std::abs (ac - mid) / 0.12, 0.0, 1.0);
                af = float (clamp (6.0 * flowC * win, 0.0, 1.0));
            }
            else
                af = 0.0f;
        }

        KlattFrame fromPhone (const Phone& p, const BodyMix& body)
        {
            KlattFrame f;
            double w[3] { 1, 0, 0 };
            body.weights (w);
            const auto vowel = (p.manner == KManner::Vowel);
            if (vowel && (p.f1f > 0 || p.f1c > 0))
            {
                f.f1 = float (w[0]*p.f1 + w[1]*p.f1f + w[2]*p.f1c);
                f.f2 = float (w[0]*p.f2 + w[1]*p.f2f + w[2]*p.f2c);
                f.f3 = float (w[0]*p.f3 + w[1]*p.f3f + w[2]*p.f3c);
                f.f4 = float (w[0]*p.f4 + w[1]*p.f4f + w[2]*p.f4c);
            }
            else
            {
                f.f1 = p.f1; f.f2 = p.f2; f.f3 = p.f3; f.f4 = p.f4;
            }
            f.b1 = p.b1; f.b2 = p.b2; f.b3 = p.b3; f.b4 = p.b4;
            f.f5 = p.f5; f.b5 = p.b5;
            f.fnp = p.fnp; f.bnz = 100; f.bnp = 100; f.fnz = p.fnz;
            f.nasal = (p.manner == KManner::Nasal) ? 1.0f : 0.0f;
            hlSynSources (p.ag, p.ac, p.an, p.voiced, f.av, f.ah, f.af);
            // Keep a floor from the published Klatt 1980 amplitude tables so
            // vowels stay strong and tabulated bursts remain a fallback.
            f.av = js_max (f.av, p.av * 0.85f);
            if (p.manner == KManner::Fricative || p.manner == KManner::Affricate)
                f.af = js_max (f.af, p.af * 0.70f);
            if (p.place == Glottal && p.manner == KManner::Fricative)
                f.ah = js_max (f.ah, 0.80f);
            f.a2 = p.a2; f.a3 = p.a3; f.a4 = p.a4; f.a5 = p.a5; f.a6 = p.a6; f.ab = p.ab;
            f.tilt = vowel ? 0.38f : 0.22f;
            f.rd = 0.7f;
            f.oq = float (0.50 + 0.10 * w[1] + 0.12 * w[2]); // slightly higher OQ for female/child
            f.f0Ratio = 1.0f;
            return f;
        }

        float locusF2 (const Phone& c, float vowelF2)
        {
            switch (c.place)
            {
                case Labial:    return 0.61f * vowelF2 + 394.0f;          // Sussman et al. /b/
                case Dental:
                case Alveolar:  return 0.37f * vowelF2 + 1130.0f;         // /d/
                case Post:      return 0.45f * vowelF2 + 1000.0f;
                case Palatal:   return js_max (1800.0f, vowelF2);
                case Velar:     return vowelF2 > 1600.0f ? 2050.0f : 1200.0f;
                default:        return c.f2loc > 0 ? c.f2loc : c.f2;
            }
        }

        float locusF3 (const Phone& c, float vowelF3, float vowelF2)
        {
            switch (c.place)
            {
                case Labial:    return 0.8f * vowelF3 + 400.0f;
                case Dental:
                case Alveolar:  return 2700.0f;
                case Post:      return 2300.0f;
                case Palatal:   return 3000.0f;
                case Velar:     return vowelF2 > 1600.0f ? 2400.0f : 1850.0f;
                default:        return c.f3loc > 0 ? c.f3loc : c.f3;
            }
        }

        float cosine01 (double u)
        {
            u = clamp (u, 0.0, 1.0);
            return float (0.5 - 0.5 * std::cos (kPi * u));
        }

        const Phone& phoneOf (const phonetics::PlannedSegment& s)
        {
            if (auto* p = findPhone (s.ipa)) return *p;
            return kSchwa;
        }

        const phonetics::PlannedSegment* nearestVowel (const std::vector<phonetics::PlannedSegment>& segs, size_t i, int dir)
        {
            std::ptrdiff_t j = std::ptrdiff_t (i) + dir;
            while (j >= 0 && size_t (j) < segs.size())
            {
                if (phoneOf (segs[size_t (j)]).manner == KManner::Vowel)
                    return &segs[size_t (j)];
                j += dir;
            }
            return nullptr;
        }
    }

    void KlattResonator::setCascade (double f, double bw, double fs) noexcept
    {
        f = clamp (f, 40.0, fs * 0.45);
        bw = clamp (bw, 20.0, 4000.0);
        const auto T = 1.0 / fs;
        const auto r = std::exp (-kPi * bw * T);
        C = -r * r;
        B = 2.0 * r * std::cos (2.0 * kPi * f * T);
        A = 1.0 - B - C;
    }

    void KlattResonator::setParallel (double f, double bw, double fs, double amp) noexcept
    {
        setCascade (f, bw, fs);
        const auto w = 2.0 * kPi * clamp (f, 40.0, fs * 0.45) / fs;
        const auto re = 1.0 - B * std::cos (w) - C * std::cos (2.0 * w);
        const auto im = B * std::sin (w) + C * std::sin (2.0 * w);
        const auto mag = std::sqrt (re * re + im * im);
        A = amp * mag;
    }

    double KlattResonator::tick (double x) noexcept
    {
        const auto y = A * x + B * y1 + C * y2;
        y2 = y1;
        y1 = is_finite (y) ? y : 0.0;
        return y1;
    }

    void KlattZero::set (double f, double bw, double fs) noexcept
    {
        KlattResonator r;
        r.setCascade (f, bw, fs);
        const auto den = (std::abs (r.A) > 1.0e-9) ? r.A : 1.0;
        A = 1.0 / den;
        B = -r.B / den;
        C = -r.C / den;
    }

    double KlattZero::tick (double x) noexcept
    {
        const auto y = A * x + B * x1 + C * x2;
        x2 = x1;
        x1 = x;
        return is_finite (y) ? y : 0.0;
    }

    void KlattSynth::setFs (double fsIn) noexcept
    {
        fs = (fsIn > 1000.0) ? fsIn : 48000.0;
    }

    void KlattSynth::reset() noexcept
    {
        lf.reset();
        r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset();
        p2.reset(); p3.reset(); p4.reset(); p5.reset(); p6.reset();
        rnp.reset(); rnz.reset();
        phase = 0; cycleAmp = 1; ugPrev = 0; radPrev = 0;
        tiltY = 0; hpX = 0; hpY = 0; noiseHp = 0; bypassY = 0;
    }

    void KlattSynth::setFrame (const KlattFrame& f) noexcept
    {
        frame = f;
        const auto F = fs;
        r1.setCascade (f.f1, f.b1, F);
        r2.setCascade (f.f2, f.b2, F);
        r3.setCascade (f.f3, f.b3, F);
        r4.setCascade (f.f4, f.b4, F);
        r5.setCascade (f.f5, f.b5, F);
        rnp.setCascade (f.fnp, f.bnp, F);
        rnz.set (f.fnz, f.bnz, F);
        p2.setParallel (f.f2, f.b2 * 1.2, F, f.a2);
        p3.setParallel (f.f3, f.b3 * 1.2, F, f.a3);
        p4.setParallel (f.f4, f.b4 * 1.1, F, f.a4);
        p5.setParallel (f.f5, f.b5, F, f.a5);
        p6.setParallel (js_min (double (f.f5) + 800.0, F * 0.42), 300.0, F, f.a6);
    }

    double KlattSynth::tick (double f0Hz, double gate) noexcept
    {
        const auto g = clamp (gate, 0.0, 1.5);
        if (g < 1.0e-5 && frame.av < 1.0e-5 && frame.af < 1.0e-5 && frame.ah < 1.0e-5)
            return 0.0;

        f0Hz = clamp (f0Hz, 50.0, 800.0);
        phase += f0Hz / fs;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            cycleAmp = 1.0 + 0.016 * rng.gauss(); // slight shimmer, not pitch
            cycleAmp = clamp (cycleAmp, 0.92, 1.08);
        }

        // KLGLOTT88-style glottal flow (Klatt & Klatt 1990): cubic pulse
        // Ug ~ x^2 (1-x) on the open phase, peak-normalised to 1 at x = 2/3.
        const auto oq = clamp (double (frame.oq), 0.30, 0.90);
        double ug = 0.0;
        if (phase < oq)
        {
            const auto x = phase / oq;
            ug = (27.0 / 4.0) * x * x * (1.0 - x) * cycleAmp;
        }
        const auto open = (ug > 0.08) ? 1.0 : 0.0;

        // Lip radiation is +6 dB/oct: feed the cascade dUg/dt, not Ug.
        // Scale so a typical 200 Hz pulse has derivative peaks near 1.
        const auto dug = (ug - ugPrev) * (fs / 400.0);
        ugPrev = ug;

        // Aspiration (AH) is a separate cascade source, not scaled by AV.
        auto noise = rng.gauss();
        noiseHp += (noise - noiseHp) * 0.35;
        const auto asp = frame.ah * g * (0.45 * open * std::sqrt (js_max (0.0, ug)) + 0.55) * noiseHp * 0.35;

        auto src = dug * frame.av * g + asp;

        // Spectral tilt TL: extra lowpass, ~0–24 dB down at 3 kHz (KLSYN88 TL).
        const auto tlDb = 24.0 * clamp (double (frame.tilt), 0.0, 1.0);
        const auto tlLin = std::pow (10.0, -tlDb / 20.0);
        // One-pole: gain at 3 kHz equals tlLin (approximately).
        const auto w3 = 2.0 * kPi * 3000.0 / fs;
        const auto pole = std::exp (-w3 * (1.0 - tlLin) / js_max (0.15, tlLin));
        tiltY = (1.0 - pole) * src + pole * tiltY;
        src = tiltY;

        // Cascade: optional nasal pole/zero, then F1-F5.
        auto y = src;
        if (frame.nasal > 0.02f)
        {
            const auto n = clamp (double (frame.nasal), 0.0, 1.0);
            const auto nasalY = rnz.tick (rnp.tick (y));
            y = y * (1.0 - 0.85 * n) + nasalY * n;
        }
        y = r5.tick (r4.tick (r3.tick (r2.tick (r1.tick (y)))));

        // Parallel frication / burst branch.
        const auto fric = frame.af * g * noiseHp;
        auto p = p2.tick (fric) + p3.tick (fric) + p4.tick (fric) + p5.tick (fric) + p6.tick (fric);
        // Labial bypass: highpassed noise (Klatt AB).
        const auto bpCoef = 1.0 - std::exp (-2.0 * kPi * 3000.0 / fs);
        bypassY += bpCoef * (fric - bypassY);
        p += frame.ab * (fric - bypassY);
        y += p;

        // DC block only — radiation already applied at the glottal source.
        const auto hp = y - hpX + 0.995 * hpY;
        hpX = y;
        hpY = hp;
        auto out = hp * outGain;
        if (! is_finite (out)) out = 0.0;
        if (out > 0.95) out = 0.95 + 0.05 * std::tanh ((out - 0.95) / 0.05);
        if (out < -0.95) out = -0.95 + 0.05 * std::tanh ((out + 0.95) / 0.05);
        return out;
    }

    KlattFrame lerpKlatt (const KlattFrame& a, const KlattFrame& b, double u) noexcept
    {
        const auto t = float (clamp (u, 0.0, 1.0));
        const auto s = 1.0f - t;
        KlattFrame o;
        o.f1 = s*a.f1 + t*b.f1; o.b1 = s*a.b1 + t*b.b1;
        o.f2 = s*a.f2 + t*b.f2; o.b2 = s*a.b2 + t*b.b2;
        o.f3 = s*a.f3 + t*b.f3; o.b3 = s*a.b3 + t*b.b3;
        o.f4 = s*a.f4 + t*b.f4; o.b4 = s*a.b4 + t*b.b4;
        o.f5 = s*a.f5 + t*b.f5; o.b5 = s*a.b5 + t*b.b5;
        o.fnp = s*a.fnp + t*b.fnp; o.bnp = s*a.bnp + t*b.bnp;
        o.fnz = s*a.fnz + t*b.fnz; o.bnz = s*a.bnz + t*b.bnz;
        o.nasal = s*a.nasal + t*b.nasal;
        o.av = s*a.av + t*b.av; o.ah = s*a.ah + t*b.ah; o.af = s*a.af + t*b.af;
        o.a2 = s*a.a2 + t*b.a2; o.a3 = s*a.a3 + t*b.a3; o.a4 = s*a.a4 + t*b.a4;
        o.a5 = s*a.a5 + t*b.a5; o.a6 = s*a.a6 + t*b.a6; o.ab = s*a.ab + t*b.ab;
        o.tilt = s*a.tilt + t*b.tilt; o.rd = s*a.rd + t*b.rd; o.oq = s*a.oq + t*b.oq;
        o.f0Ratio = s*a.f0Ratio + t*b.f0Ratio;
        return o;
    }

    KlattFrame klattAtTime (const KlattTrack& tr, double t) noexcept
    {
        if (tr.frames.empty()) return {};
        const auto hop = (tr.hop > 1e-5) ? tr.hop : kHop;
        auto u = (t - tr.t0) / hop;
        if (u <= 0.0) return tr.frames.front();
        if (u >= double (tr.frames.size() - 1)) return tr.frames.back();
        const auto i = size_t (u);
        return lerpKlatt (tr.frames[i], tr.frames[i + 1], u - double (i));
    }

    KlattFrame klattVowelFrame (std::string_view ipa, const BodyMix& body)
    {
        const auto* p = findPhone (ipa);
        auto f = fromPhone (p ? *p : kSchwa, body);
        if (! p || p->manner != KManner::Vowel)
            scaleFrame (f, formantScale (body));
        return f;
    }

    KlattTrack buildKlattTrack (const std::vector<phonetics::PlannedSegment>& segs, const BodyMix& body, bool vowelOnBeat)
    {
        KlattTrack tr;
        tr.hop = kHop;
        if (segs.empty()) return tr;
        tr.t0 = segs.front().t0;
        auto tEnd = segs.back().t1 + 0.06;
        tr.end = tEnd;
        const auto n = size_t (std::ceil ((tEnd - tr.t0) / kHop)) + 2;
        tr.frames.assign (n, KlattFrame {});
        const auto sc = formantScale (body);

        auto timeOf = [&] (size_t i) { return tr.t0 + double (i) * kHop; };

        auto vowelFormants = [&] (const phonetics::PlannedSegment* vs, float& f1, float& f2, float& f3)
        {
            const auto fr = fromPhone (vs ? phoneOf (*vs) : kSchwa, body);
            f1 = fr.f1; f2 = fr.f2; f3 = fr.f3;
        };

        // Pass 1: steady mid-segment targets.
        for (size_t fi = 0; fi < n; ++fi)
        {
            const auto t = timeOf (fi);
            const phonetics::PlannedSegment* cur = nullptr;
            for (const auto& s : segs)
                if (t >= s.t0 && t < s.t1) { cur = &s; break; }
            if (cur == nullptr)
            {
                if (t < segs.front().t0) cur = &segs.front();
                else cur = &segs.back();
            }
            const auto& ph = phoneOf (*cur);
            auto fr = fromPhone (ph, body);

            if (ph.manner == KManner::Fricative && ph.place == Glottal)
            {
                // /h/ takes the following vowel's formants (Klatt 1980).
                float f1, f2, f3;
                vowelFormants (nearestVowel (segs, size_t (cur - segs.data()), +1), f1, f2, f3);
                fr.f1 = f1; fr.f2 = f2; fr.f3 = f3;
                fr.av = 0.0f;
                fr.ah = 0.85f;
                fr.af = 0.12f;
            }
            if (ph.manner == KManner::Stop)
            {
                fr.av = ph.voiced ? 0.22f : 0.0f;
                fr.af = 0.0f;
                fr.ah = 0.0f;
                fr.f1 = 180;
                fr.a2 = fr.a3 = fr.a4 = fr.a5 = fr.a6 = fr.ab = 0;
            }
            if (ph.manner == KManner::Nasal)
            {
                fr.f1 = 250;
                fr.nasal = 1.0f;
            }
            if (ph.manner != KManner::Vowel)
                scaleFrame (fr, sc);
            tr.frames[fi] = fr;
        }

        // Pass 2: locus-based CV / VC transitions, bursts, VOT.
        for (size_t si = 0; si < segs.size(); ++si)
        {
            const auto& s = segs[si];
            const auto& ph = phoneOf (s);
            const auto* nxtV = nearestVowel (segs, si, +1);
            const auto* prvV = nearestVowel (segs, si, -1);
            float v1 = 500, v2 = 1500, v3 = 2500;
            vowelFormants (nxtV ? nxtV : prvV, v1, v2, v3);
            const auto loc2 = locusF2 (ph, v2);
            const auto loc3 = locusF3 (ph, v3, v2);
            const auto loc1 = float ((ph.manner == KManner::Stop || ph.manner == KManner::Nasal) ? 180.0 * sc : ph.f1 * sc);

            auto applyLerpFormants = [&] (double tA, double tB, float a1, float a2, float a3, float b1, float b2, float b3, bool endsAtB)
            {
                for (size_t fi = 0; fi < n; ++fi)
                {
                    const auto t = timeOf (fi);
                    if (t < tA || t > tB) continue;
                    const auto u = (tB > tA) ? (t - tA) / (tB - tA) : 1.0;
                    const auto w = endsAtB ? cosine01 (u) : cosine01 (1.0 - u);
                    auto& fr = tr.frames[fi];
                    fr.f1 = a1 + (b1 - a1) * w;
                    fr.f2 = a2 + (b2 - a2) * w;
                    fr.f3 = a3 + (b3 - a3) * w;
                }
            };

            if (ph.manner == KManner::Vowel)
            {
                const auto trans = js_min (0.045, 0.45 * (s.t1 - s.t0));
                if (si > 0)
                {
                    const auto& prev = phoneOf (segs[si - 1]);
                    if (! isVowelManner (prev.manner))
                    {
                        float pv1, pv2, pv3;
                        vowelFormants (&s, pv1, pv2, pv3);
                        const auto p2 = locusF2 (prev, pv2);
                        const auto p3 = locusF3 (prev, pv3, pv2);
                        const auto p1 = float ((prev.manner == KManner::Stop || prev.manner == KManner::Nasal) ? 200.0 * sc : 300.0 * sc);
                        applyLerpFormants (s.t0, s.t0 + trans, p1, p2, p3,
                                           pv1, pv2, pv3, true);
                    }
                }
                if (si + 1 < segs.size())
                {
                    const auto& nx = phoneOf (segs[si + 1]);
                    if (! isVowelManner (nx.manner))
                    {
                        float pv1, pv2, pv3;
                        vowelFormants (&s, pv1, pv2, pv3);
                        const auto p2 = locusF2 (nx, pv2);
                        const auto p3 = locusF3 (nx, pv3, pv2);
                        const auto p1 = float ((nx.manner == KManner::Stop || nx.manner == KManner::Nasal) ? 200.0 * sc : 300.0 * sc);
                        applyLerpFormants (s.t1 - trans, s.t1,
                                           pv1, pv2, pv3,
                                           p1, p2, p3, true);
                    }
                }
            }

            if (ph.manner == KManner::Stop || ph.manner == KManner::Affricate)
            {
                const auto release = s.t1;
                const auto burstDur = (ph.manner == KManner::Affricate) ? 0.018 : 0.010;
                const auto vot = ph.voiced ? 0.012 : (ph.place == Velar ? 0.055 : (ph.place == Labial ? 0.040 : 0.048));
                const auto fricDur = (ph.manner == KManner::Affricate) ? 0.055 : 0.0;

                for (size_t fi = 0; fi < n; ++fi)
                {
                    const auto t = timeOf (fi);
                    auto& fr = tr.frames[fi];
                    if (t < s.t0 || t > release + vot + fricDur + 0.01) continue;

                    // Area trajectories (HLsyn): sealed closure, then a constriction
                    // window that automatically yields a burst, then an abducted
                    // glottis for voiceless VOT, then a modal open vowel.
                    double ag = ph.voiced ? 0.05 : 0.22;
                    double ac = 0.0;
                    const auto an = 0.0;
                    if (t < release)
                    {
                        fr.f1 = loc1; fr.f2 = loc2; fr.f3 = loc3;
                        ac = 0.0;
                        ag = ph.voiced ? 0.05 : 0.20;
                        fr.a2 = fr.a3 = fr.a4 = fr.a5 = fr.a6 = fr.ab = 0;
                    }
                    else if (t < release + burstDur + fricDur)
                    {
                        const auto u = clamp ((t - release) / js_max (0.006, burstDur + 0.5 * fricDur), 0.0, 1.0);
                        // ac sweeps 0 → 0.10 (peak frication) → ~0.6
                        ac = (u < 0.4) ? (0.10 * (u / 0.4)) : (0.10 + 0.50 * (u - 0.4) / 0.6);
                        ag = ph.voiced ? 0.06 : 0.24;
                        fr.a2 = ph.a2; fr.a3 = ph.a3; fr.a4 = ph.a4;
                        fr.a5 = ph.a5; fr.a6 = ph.a6; fr.ab = ph.ab;
                        fr.f2 = loc2; fr.f3 = loc3; fr.f1 = loc1;
                    }
                    else if (t < release + vot)
                    {
                        ac = 0.80;
                        ag = ph.voiced ? 0.06 : 0.28; // abducted → AH, delayed AV
                        fr.f2 = loc2; fr.f3 = loc3;
                    }
                    else
                    {
                        ac = 1.0;
                        ag = 0.05;
                    }
                    float av, ah, af;
                    hlSynSources (ag, ac, an, ph.voiced || t >= release + vot * 0.75, av, ah, af);
                    if (t >= s.t0 && t < release)
                    {
                        fr.av = av; fr.ah = 0; fr.af = 0;
                    }
                    else if (t < release + burstDur + fricDur)
                    {
                        fr.af = js_max (fr.af, af);
                        fr.ah = js_max (fr.ah, ah);
                        fr.av = ph.voiced ? js_max (fr.av * 0.4f, av * 0.5f) : js_min (fr.av, av);
                    }
                    else if (! ph.voiced && t < release + vot)
                    {
                        fr.ah = js_max (fr.ah, ah);
                        if (t < release + vot * 0.75)
                            fr.av = js_min (fr.av, 0.12f);
                    }
                }
            }

            if (ph.manner == KManner::Fricative)
            {
                const auto att = 0.012, rel = 0.018;
                for (size_t fi = 0; fi < n; ++fi)
                {
                    const auto t = timeOf (fi);
                    if (t < s.t0 || t > s.t1) continue;
                    auto env = 1.0;
                    if (t < s.t0 + att) env = (t - s.t0) / att;
                    if (t > s.t1 - rel) env = (s.t1 - t) / rel;
                    env = clamp (env, 0.0, 1.0);
                    auto& fr = tr.frames[fi];
                    const auto glottalH = (ph.place == Glottal);
                    const auto ag = glottalH ? 0.40 : (ph.voiced ? 0.05 : 0.22);
                    const auto ac = glottalH ? 1.00 : 0.10;
                    float av, ah, af;
                    hlSynSources (ag, ac, 0.0, ph.voiced, av, ah, af);
                    fr.af = float (js_max (af, ph.af * 0.70f) * env);
                    fr.ah = float (js_max (ah, ph.ah) * env);
                    fr.av = float (av * env);
                    if (glottalH) { fr.av = 0; fr.ah = float (js_max (ah, 0.80f) * env); }
                    fr.a2 = ph.a2; fr.a3 = ph.a3; fr.a4 = ph.a4;
                    fr.a5 = ph.a5; fr.a6 = ph.a6; fr.ab = ph.ab;
                    // /h/ already borrowed vowel formants; other fricatives keep their spectrum.
                }
            }

            if (ph.manner == KManner::Nasal || ph.manner == KManner::Approximant || ph.manner == KManner::Lateral)
            {
                const auto trans = js_min (0.035, 0.4 * (s.t1 - s.t0));
                // Blend formants toward neighbouring vowels at the edges.
                if (prvV)
                {
                    float ev1, ev2, ev3;
                    vowelFormants (prvV, ev1, ev2, ev3);
                    applyLerpFormants (s.t0, s.t0 + trans, loc1, loc2, loc3, ev1, ev2, ev3, true);
                }
                if (nxtV)
                {
                    float ev1, ev2, ev3;
                    vowelFormants (nxtV, ev1, ev2, ev3);
                    applyLerpFormants (s.t1 - trans, s.t1, loc1, loc2, loc3, ev1, ev2, ev3, true);
                }
            }
        }

        // Fade in / out and close the last frames.
        const auto fadeIn = segs.front().t0 + 0.015;
        const auto fadeOut0 = segs.back().t1;
        const auto fadeOut1 = tr.end;
        for (size_t fi = 0; fi < n; ++fi)
        {
            const auto t = timeOf (fi);
            auto& fr = tr.frames[fi];
            if (t < fadeIn)
            {
                const auto w = cosine01 ((t - tr.t0) / js_max (0.005, fadeIn - tr.t0));
                fr.av *= w; fr.ah *= w; fr.af *= w;
            }
            if (t >= fadeOut0)
            {
                const auto w = 1.0f - cosine01 ((t - fadeOut0) / js_max (0.01, fadeOut1 - fadeOut0));
                fr.av *= w; fr.ah *= w; fr.af *= w;
            }
            // Unstressed vowels a touch quieter, stressed a touch louder.
        }

        // Stress from PlannedSegment: lift AV slightly on stress==1.
        for (size_t fi = 0; fi < n; ++fi)
        {
            const auto t = timeOf (fi);
            for (const auto& s : segs)
            {
                if (t < s.t0 || t >= s.t1) continue;
                if (s.stress.has_value() && *s.stress == 1)
                    tr.frames[fi].av = js_min (1.0f, tr.frames[fi].av * 1.08f);
                else if (s.stress.has_value() && *s.stress == 0)
                    tr.frames[fi].av *= 0.90f;
            }
        }
        // KTH / Kenmochi singing timing: shift so the first vowel onset sits
        // at t = 0 and onset consonants occupy negative time (before the beat).
        if (vowelOnBeat)
        {
            double vowelT = segs.front().t0;
            for (const auto& s : segs)
            {
                if (phoneOf (s).manner == KManner::Vowel)
                {
                    vowelT = s.t0;
                    break;
                }
            }
            const auto dt = vowelT; // new t0 = old t0 - vowelT
            if (std::abs (dt) > 1.0e-4)
            {
                tr.t0 -= dt;
                tr.end -= dt;
            }
        }

        return tr;
    }
}
