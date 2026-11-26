#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "Loudness.h"
#include "TruePeak.h"

namespace dooblrl::dsp
{
    enum class Style : int
    {
        transparent = 0,
        punchy,
        dynamicStyle, // "Dynamic" is a keyword-ish name; this is the transient-enhance-before-limit style
        allround,
        aggressive,
        modern,
        bus,
        safe,
        numStyles
    };

    struct Settings
    {
        float thresholdDb = -3.0f;
        float attackMs = 0.5f;
        float releaseMs = 50.0f;
        float kneeDb = 3.0f;
        float enhance = 0.0f;          // 0..100
        int style = (int) Style::allround;
        int oversamplingIndex = 2;     // 4x
        float lookaheadMs = 2.0f;
        float transientLink = 1.0f;    // 0..1
        float releaseLink = 0.7f;      // 0..1
        float inputGainDb = 0.0f;
        float ceilingDb = -0.3f;
        bool unityGain = false;
        bool delta = false;
        bool autoComp = true;
        int dither = 0;                // 0 off, 1 16-bit, 2 24-bit
        int shaping = 1;
        int lufsTarget = 0;
        bool bypass = false;
        int latencyMode = 0; // 0 Zero, 1 Natural, 2 High
    };

    struct Meters
    {
        float inputPeakL = 0, inputPeakR = 0;
        float outputPeakL = 0, outputPeakR = 0;
        float inputTruePeakL = 0, inputTruePeakR = 0;
        float outputTruePeakL = 0, outputTruePeakR = 0;
        float gainReductionDb = 0;
        float momentaryLufs = -70, shortTermLufs = -70, integratedLufs = -70, lra = 0;
        bool hasIntegrated = false;
    };

    /** Two-stage mastering / bus limiter.

        Pre-process: lookahead soft-knee GR that lets transients overshoot.
        Enhance: reshapes residual peaks (harmonics + density) when amount > 0; amount 0 is a skip.
        Auto Comp: true-peak catcher that ducks only while an inter-sample over lasts.
        Final sample ceiling is always applied (except in delta audition).
    */
    class LimiterEngine
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr float maxLookaheadMs = 20.0f;

        void prepare (double sampleRate, int maxBlock, int oversamplingIndex);
        void reset() noexcept;
        void setSettings (const Settings& s) noexcept { settings = s; }
        const Settings& getSettings() const noexcept { return settings; }

        void process (float* const* io, int numChannels, int numSamples);

        int latencySamples() const noexcept { return reportedLatency; }
        int lookaheadSamples() const noexcept { return hostLookahead; }
        int oversamplingLatencySamples() const noexcept { return osLatency; }
        int oversamplingFactor() const noexcept { return osFactor; }

        Meters takeMeters() noexcept;
        void resetMeters() noexcept;
        float currentGainReductionDb() const noexcept { return grDb.load (std::memory_order_relaxed); }

        LoudnessMeter& getLoudness() noexcept { return loudness; }
        TruePeak& getOutputTruePeak() noexcept { return outTp; }

        /** Pre-process only (no enhance, no auto-comp, then ceiling). For the Enhance=0 null test. */
        void processPreProcessOnly (float* const* io, int numChannels, int numSamples);

    private:
        struct StyleAlgo
        {
            float attackScale = 1, releaseScale = 1, kneeBiasDb = 0, ratio = 100;
            float rmsMix = 0;           // 0 = peak, 1 = RMS
            float overshootDb = 4;      // how far pre-process may overshoot the threshold
            float evenHarm = 0.25f, oddHarm = 0.55f;
            bool enhanceBeforeLimit = false;
            bool programRelease = false;
            bool dcBlock = false;
            bool softClip = false;
            float clipDrive = 1.2f;
            float extraCeilingMarginDb = 0;
            float minLookaheadMs = 0;
            bool noHarmonics = false;
        };

        static StyleAlgo algoFor (int style) noexcept;
        static int factorForIndex (int index) noexcept;

        void ensureOversampler (int index);
        void processHostRate (float* const* io, int numChannels, int numSamples, bool skipEnhance);
        void processOversampled (juce::dsp::AudioBlock<float> block, bool skipEnhance);
        void processSample (float& l, float& r, bool skipEnhance) noexcept;
        float detectorGainDb (float peakLin, float rmsLin, const StyleAlgo& a) const noexcept;
        float enhanceResidual (float x, float amount, const StyleAlgo& a) const noexcept;
        void applyDither (float& l, float& r) noexcept;
        float nextRandom() noexcept;
        bool isZeroLatency() const noexcept { return settings.latencyMode <= 0; }
        float effectiveLookaheadMs() const noexcept;
        int autoCompLatency() const noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        int maxBlock = 512;
        int osFactor = 4, osIndex = 2, osLatency = 0, hostLookahead = 96, reportedLatency = 0;
        int osLookahead = 384;

        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 5> oversamplers, iirOversamplers;
        juce::dsp::Oversampling<float>* activeOs = nullptr;

        std::vector<float> delayL, delayR, dryL, dryR, dryDelayL, dryDelayR;
        std::vector<float> acL, acR;
        int delayWrite = 0, delaySize = 0;
        int dryDelayWrite = 0, dryDelaySize = 0;
        int acWrite = 0, acSize = 0;

        float envL = 1, envR = 1;          // GR envelope (linear gain, 1 = none)
        float rmsL = 0, rmsR = 0;
        float autoCompGain = 1;
        float dcZL = 0, dcZR = 0;
        float shapeZL = 0, shapeZR = 0, shapeZ2L = 0, shapeZ2R = 0;
        float unitySmooth = 1;
        float targetTrim = 1;
        uint32_t rng = 0xA341316Cu;

        TruePeak inTp, outTp, autoTp;
        LoudnessMeter loudness;

        std::atomic<float> inPeakL { 0 }, inPeakR { 0 }, outPeakL { 0 }, outPeakR { 0 };
        std::atomic<float> inTpL { 0 }, inTpR { 0 }, outTpL { 0 }, outTpR { 0 };
        std::atomic<float> grDb { 0 };
        std::atomic<float> mLufs { -70 }, sLufs { -70 }, iLufs { -70 }, lraVal { 0 };
        std::atomic<int> integratedFlag { 0 };
    };
}
