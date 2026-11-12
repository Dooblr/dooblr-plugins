#pragma once
// Original cascade/parallel formant synthesizer following Klatt 1980 (JASA)
// and Klatt & Klatt 1990 (KLSYN88 / KLGLOTT88 voicing ideas), with an LF/Rd
// glottal source (Fant, Liljencrants & Lin 1985; Fant 1997; Gobl 2017).
// No third-party synthesizer code is used.

#include "Glottis.h"
#include "Phonetics/Types.h"
#include "Articulation.h"
#include <string>
#include <string_view>
#include <vector>

namespace dooblrthroat::dsp
{
    struct KlattFrame
    {
        float f1 = 500, b1 = 80, f2 = 1500, b2 = 100, f3 = 2500, b3 = 150;
        float f4 = 3500, b4 = 200, f5 = 4500, b5 = 200;
        float fnp = 270, bnp = 100, fnz = 270, bnz = 100, nasal = 0;
        float av = 0, ah = 0, af = 0;
        float a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, ab = 0;
        float tilt = 0.4f; // 0 bright .. 1 dull (spectral tilt)
        float rd = 0.7f;
        float oq = 0.55f; // KLGLOTT88 open quotient
        float f0Ratio = 1.0f;
    };

    struct KlattTrack
    {
        std::vector<KlattFrame> frames;
        double hop = 0.005;
        double t0 = 0, end = 0;
    };

    KlattFrame lerpKlatt (const KlattFrame& a, const KlattFrame& b, double u) noexcept;
    KlattFrame klattAtTime (const KlattTrack& tr, double t) noexcept;
    KlattFrame klattVowelFrame (std::string_view ipa, const BodyMix& body);
    KlattTrack buildKlattTrack (const std::vector<phonetics::PlannedSegment>& segs, const BodyMix& body, bool vowelOnBeat = false);

    struct KlattResonator
    {
        double A = 1, B = 0, C = 0, y1 = 0, y2 = 0;
        void setCascade (double f, double bw, double fs) noexcept;
        void setParallel (double f, double bw, double fs, double amp) noexcept;
        void reset() noexcept { y1 = y2 = 0; }
        double tick (double x) noexcept;
    };

    struct KlattZero
    {
        double A = 1, B = 0, C = 0, x1 = 0, x2 = 0;
        void set (double f, double bw, double fs) noexcept;
        void reset() noexcept { x1 = x2 = 0; }
        double tick (double x) noexcept;
    };

    struct KlattSynth
    {
        double fs = 48000.0;
        KlattFrame frame {};
        LfGlottis lf;
        KlattResonator r1, r2, r3, r4, r5;
        KlattResonator p2, p3, p4, p5, p6;
        KlattResonator rnp;
        KlattZero rnz;
        Rng rng { 2468 };
        double phase = 0, cycleAmp = 1, ugPrev = 0, radPrev = 0;
        double tiltY = 0, hpX = 0, hpY = 0, noiseHp = 0, bypassY = 0;
        double outGain = 0.04;

        void setFs (double fsIn) noexcept;
        void reset() noexcept;
        void setFrame (const KlattFrame& f) noexcept;
        double tick (double f0Hz, double gate) noexcept;
    };
}
