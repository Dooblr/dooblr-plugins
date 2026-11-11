#pragma once

#include "OutputPeakTap.h"
#include "PitchTracker.h"
#include "SubGenerator.h"
#include "SystemCheck.h"
#include "ToneShift.h"

#include <array>
#include <vector>

namespace dooblrsub::dsp
{
    struct EngineSettings
    {
        SubSettings sub;
        ToneShiftSettings tone;
        bool monoBelow = true;
        float monoHz = 100.0f;
        bool infraHpf = true;
        float infraHz = 25.0f;
        bool subLimit = true;
        Target target = Target::club;
        float mix = 1.0f;
        float outputDb = 0.0f;
        bool bypass = false;
    };

    struct DisplaySnapshot
    {
        Analysis analysis;
        Verdict verdict = Verdict::works;
        ReasonList reasons;
        float trackedHz = 0.0f;
        float envelope = 0.0f;
        std::array<float, SystemCheck::spectrumBins> spectrum {};
        std::array<float, numSubBands> bands {};
    };

    /**
        Full dooblr-sub path: tone shift (latency) -> sub generator -> safety -> mix / output.
        Latency is the tone-shift STFT size and is always reported, including on bypass
        (bypass is the delayed dry signal so host delay compensation stays correct).
    */
    class SubEngine
    {
    public:
        void prepare (double sampleRateToUse, int blockSize);
        void reset();
        void setSettings (const EngineSettings& s) noexcept { settings = s; }

        void process (float* left, float* right, int numSamples);

        int getLatencySamples() const noexcept { return ToneShift::latencySamples; }
        DisplaySnapshot display() const;
        OutputPeakTap& getPeakTap() noexcept { return peakTap; }

        float getTrackedHz() const noexcept { return sub.getTrackedHz(); }
        const SystemCheck& getSystemCheck() const noexcept { return check; }
        ToneShift& getToneShift() noexcept { return toneLeft; }
        SubGenerator& getSub() noexcept { return sub; }

        static constexpr int maxChunk = 512;

    private:
        void processSafety (float& l, float& r) noexcept;

        double sampleRate = 44100.0;
        EngineSettings settings;
        ToneShift toneLeft, toneRight;
        SubGenerator sub;
        SystemCheck check;
        OutputPeakTap peakTap;

        std::vector<float> toneL, toneR, inL, inR, delayedL, delayedR;
        std::vector<float> dryDelayL, dryDelayR;
        int dryWrite = 0;
        float infraZL = 0.0f, infraZR = 0.0f, monoZL = 0.0f, monoZR = 0.0f;
        float limitEnv = 0.0f;
        float bypassMix = 0.0f, outGain = 1.0f;
    };
}
