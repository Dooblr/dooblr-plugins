#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_core/juce_core.h>

namespace dooblrsub::dsp
{
    enum class Target : int { club = 0, festival = 1, home = 2 };
    enum class Verdict : int { works = 0, check = 1, wont = 2 };

    inline constexpr int numSubBands = 5;
    inline constexpr const char* subBandNames[numSubBands] {
        "20-30", "30-45", "45-60", "60-90", "90-120"
    };
    inline constexpr float subBandLoHz[numSubBands] { 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
    inline constexpr float subBandHiHz[numSubBands] { 30.0f, 45.0f, 60.0f, 90.0f, 120.0f };

    inline constexpr int maxReasons = 8;
    inline constexpr int reasonBytes = 48;

    struct Analysis
    {
        std::array<float, numSubBands> bandRms {};
        float subRms = 0.0f;
        float subPeak = 0.0f;
        float crest = 1.0f;
        float peakToRms = 1.0f;
        float lrCorrelation = 1.0f;
        float sideRms = 0.0f;
        float dc = 0.0f;
        float infraRms = 0.0f;
        float cancellation = 0.0f; // -1 = anti-phase sub vs fundamental, +1 = in phase
        float sustainedDb = -120.0f;
        float pitchHz = 0.0f;
        float pitchStd = 0.0f;
        float headroomPeak = 0.0f;
    };

    struct ReasonList
    {
        int count = 0;
        char text[maxReasons][reasonBytes] {};

        void add (const char* s) noexcept
        {
            if (count >= maxReasons || s == nullptr) return;
            std::snprintf (text[count], reasonBytes, "%s", s);
            ++count;
        }
    };

    struct Thresholds
    {
        float infraRatio = 0.22f;          // infra / sub  -> fail
        float infraRatioWarn = 0.10f;
        float minCorrelation = 0.35f;      // below -> stereo low end fail
        float minCorrelationWarn = 0.70f;
        float clipPeak = 0.98f;
        float clipPeakWarn = 0.89f;
        float cancelFail = -0.45f;
        float cancelWarn = -0.20f;
        float dcFail = 0.02f;
        float pitchRelStdFail = 0.25f;
        float sustainedFailDb = -3.0f;
        float limiterCeiling = 0.92f;      // also used by the sub-path limiter
        float infraHzBias = 0.0f;          // added to the infra HPF cutoff
    };

    Thresholds thresholdsFor (Target target) noexcept;
    const char* verdictLabel (Verdict v) noexcept;

    /** Pure verdict from measured values. Unit-tested with synthetic Analysis / buffers. */
    Verdict evaluate (const Analysis& a, Target target, ReasonList& reasons) noexcept;

    /** Offline analysis of a buffer (tests and the live analyser share the same rules). */
    Analysis analyseBuffer (const float* left, const float* right, int numSamples, double sampleRate,
                            float subVsFundCorrelation = 0.0f, float trackedHz = 0.0f,
                            float trackedStd = 0.0f) noexcept;

    /**
        Real-time analyser: IIR sub-band RMS, crest, L/R correlation below 120 Hz,
        DC, infrasonic energy, and a lock-free snapshot for the editor.
    */
    class SystemCheck
    {
    public:
        void prepare (double sampleRateToUse);
        void reset() noexcept;
        void setTarget (Target t) noexcept { target = t; }
        Target getTarget() const noexcept { return target; }

        void process (const float* left, const float* right, int numSamples, int numChannels,
                      float generatedSub, float originalLow, float trackedHz) noexcept;

        Analysis snapshot() const noexcept;
        Verdict currentVerdict (ReasonList& reasons) const noexcept;

        static constexpr int spectrumBins = 96;

        void copySpectrum (float* dest, int n) const noexcept;

    private:
        void tickFilters (float l, float r, float genSub, float origLow) noexcept;
        void publish() noexcept;

        double sampleRate = 44100.0;
        Target target = Target::club;

        struct OnePole { float z = 0.0f; float process (float x, float a) noexcept { z += a * (x - z); return z; } };
        OnePole infraL, infraR, lowL, lowR, dcL, dcR, band[numSubBands][2];
        float bandState[numSubBands][2] {};

        double accBand[numSubBands] {}, accSub {}, accPeak {}, accMid {}, accSide {}, accDc {}, accInfra {};
        double accLowL {}, accLowR {}, accLowLR {}, accGen {}, accFund {}, accGenFund {};
        int accN = 0, hop = 0;

        std::array<float, 64> pitchHist {};
        int pitchWrite = 0, pitchFilled = 0;

        Analysis published;
        std::array<float, spectrumBins> spectrum {};
        mutable std::atomic<int> seq { 0 };

        // Tiny DFT accumulator for the display (20-200 Hz, log).
        std::vector<float> specRing;
        int specWrite = 0;
    };
}
