#pragma once

#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

namespace dooblrdistressor::dsp
{
    inline constexpr int maxChannels = 2;

    enum class Ratio : int
    {
        r1 = 0,
        r2,
        r3,
        r4,
        r6,
        r10,  // Opto
        r20,
        nuke,
        numRatios
    };

    enum class DistMode : int { off = 0, dist2, dist3 };

    struct Settings
    {
        float inputKnob = 5.0f;
        float outputKnob = 5.0f;
        float attackKnob = 5.0f;
        float releaseKnob = 5.0f;
        int ratio = (int) Ratio::r6;
        bool audioHp = false;
        DistMode dist = DistMode::off;
        bool detHp = false;
        bool detBand = false;
        bool stereoLink = false;
        float headroomDb = 16.0f;
        float mix = 1.0f;
        bool bypass = false;
        bool gainMatch = false;
    };

    struct Meters
    {
        float gainReductionDb = 0.0f; // positive: how far the VCA is turned down
        float inputPeakL = 0.0f, inputPeakR = 0.0f;
        float outputPeakL = 0.0f, outputPeakR = 0.0f;
        float thd = 0.0f;             // 0.01 = 1 %
        float matchDb = 0.0f;         // live Gain Match compensation (0 when off and settled)
        bool led1pct = false;
        bool led3pct = false;
    };

    /** Independently designed approximation of an EL8-style compressor. Not an emulation. */
    class DistressorEngine
    {
    public:
        void prepare (double sampleRate, int maxBlock) noexcept;
        void reset() noexcept;
        void setSettings (const Settings& s) noexcept { settings = s; }

        /** Audio thread. `io` is the wet path in-place; dry is mixed using the original samples. */
        void process (float* const* io, int numChannels, int numSamples) noexcept;

        Meters takeMeters() noexcept;

        static constexpr float attackMinSec = 50.0e-6f, attackMaxSec = 30.0e-3f;
        static constexpr float attackMinFastSec = 20.0e-6f; // 2:1, 3:1, 4:1 at knob 0
        static constexpr float releaseMinSec = 50.0e-3f, releaseMaxSec = 3.5f, optoReleaseMaxSec = 20.0f;
        static constexpr float thdLed1 = 0.01f, thdLed3 = 0.03f; // UA: 1 % and 3 %
        static constexpr float matchWindowSec = 1.0f, matchSmoothSec = 0.4f;
        static constexpr float matchMaxDb = 24.0f, matchSilenceDb = -60.0f;

        static bool usesFastMinAttack (int ratio) noexcept;
        static float attackSeconds (float knob, int ratio) noexcept;
        static float releaseSeconds (float knob, int ratio) noexcept;
        static float inputGainDb (float knob) noexcept { return (knob - 5.0f) * 3.0f; }
        static float outputGainDb (float knob) noexcept { return (knob - 5.0f) * 3.0f; }
        static float headroomDriveDb (float headroomDb) noexcept { return headroomDb - 16.0f; }

        struct Curve
        {
            float thresholdDb = 0.0f;
            float kneeDb = 0.0f;
            float ratio = 1.0f;
            float makeupDb = 0.0f;
            bool parabolic = false;
            bool risingRatio = false;
            bool brickwall = false;
            bool opto = false;
            bool logRelease = false;
        };

        static Curve curveFor (int ratio, float attackKnob, float releaseKnob) noexcept;

        /** Static gain computer: dB of reduction (<= 0) for a detector level in dB. */
        static float staticReductionDb (float detectorDb, int ratio, float attackKnob, float releaseKnob) noexcept;

        /** Magnitude in dB of the audio high-pass at `hz`, matching the runtime filter. */
        static float audioHpMagnitudeDb (float hz, double sampleRate) noexcept;

        static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
        static float gainToDb (float g) noexcept { return g > 1.0e-12f ? 20.0f * std::log10 (g) : -240.0f; }

        float lastGainReductionDb() const noexcept { return displayedGrDb; }
        float lastMatchDb() const noexcept { return matchDb; }

    private:
        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            float z1 = 0, z2 = 0;

            void reset() noexcept { z1 = z2 = 0; }
            float process (float x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        struct OnePoleHp
        {
            float a = 0, z = 0;
            void reset() noexcept { z = 0; }
            void setCutoff (float hz, double sr) noexcept
            {
                const auto w = juce::MathConstants<float>::twoPi * hz / (float) sr;
                a = std::exp (-w);
            }
            float process (float x) noexcept
            {
                z = a * z + (1.0f - a) * x;
                return x - z;
            }
        };

        static void designAudioHp (Biquad& hp2, OnePoleHp& hp1, double sampleRate) noexcept;
        static void designPeak (Biquad& eq, float hz, float gainDb, float q, double sampleRate) noexcept;
        static float saturate (float x, DistMode mode) noexcept;
        static float smallSignalGain (DistMode mode) noexcept;
        static float knobToNorm (float knob) noexcept;

        void updateCoefficients() noexcept;
        float detectorFilter (float x, int channel) noexcept;
        float waveshape (float x, DistMode mode, float& thdAcc, float& thdRef) noexcept;

        Settings settings;
        double sampleRate = 48000.0;

        Biquad audioHp2[maxChannels], detPeak[maxChannels];
        OnePoleHp audioHp1[maxChannels], detHp[maxChannels];
        float envDb[maxChannels] { 0, 0 };
        float optoSlow[maxChannels] { 0, 0 };
        float lastGrDb[maxChannels] { 0, 0 };

        float displayedGrDb = 0.0f;
        float thdBallistics = 0.0f;
        bool clipLatch = false;
        float inPeak[maxChannels] { 0, 0 };
        float outPeak[maxChannels] { 0, 0 };
        float meterGr = 0.0f, meterThd = 0.0f, meterMatchDb = 0.0f;
        bool meterLed1 = false, meterLed3 = false;

        float dryMs = 0.0f, procMs = 0.0f;
        float matchDb = 0.0f, matchTargetDb = 0.0f;
    };
}
