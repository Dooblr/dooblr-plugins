#pragma once

#include "PitchTracker.h"

#include <array>
#include <cmath>

namespace dooblrsub::dsp
{
    struct SubSettings
    {
        float subAmount = 0.5f;       // 0..1
        float loAmount = 0.25f;       // 0..1
        float frequencyHz = 60.0f;
        int octaveMode = 0;           // 0 = -1 oct, 1 = unison, 2 = both
        float trackingSpeed = 0.55f;  // 0..1
        float drive = 0.2f;           // 0..1
    };

    /**
        Detects the low fundamental and synthesises a phase-continuous sub-harmonic
        (octave-down and/or unison sine, lightly shaped by Drive) plus a Lo band
        (resonant bass-band boost with the same drive). The oscillator amplitude
        follows the input envelope (fast attack, no clicks on note changes via a
        two-voice crossfade when the tracked pitch jumps).
    */
    class SubGenerator
    {
    public:
        void prepare (double sampleRateToUse);
        void reset() noexcept;
        void setSettings (const SubSettings& s) noexcept { settings = s; }

        /** Mono process: returns sub+lo to add, and writes the generated sub (pre-lo) into generatedSub. */
        float process (float x, float& generatedSub) noexcept;

        float getTrackedHz() const noexcept { return tracker.getFrequencyHz(); }
        float getConfidence() const noexcept { return tracker.getConfidence(); }
        float getEnvelope() const noexcept { return envelope; }

        /** Last block's generated sub vs original low-band, for cancellation metering. */
        void takeCorrelationTaps (float& subAcc, float& fundAcc, float& productAcc, int& n) noexcept;

    private:
        float tickOsc (float& phase, float hz) const noexcept;
        float saturate (float x, float amount) const noexcept;

        double sampleRate = 44100.0;
        SubSettings settings;
        PitchTracker tracker;

        float phaseA = 0.0f, phaseB = 0.0f, freqA = 0.0f, freqB = 0.0f;
        float mixA = 1.0f, crossfade = 0.0f;
        int active = 0;
        float envelope = 0.0f;
        float loLp = 0.0f, loBp = 0.0f;
        float fundLp = 0.0f;

        float corrSub = 0.0f, corrFund = 0.0f, corrProd = 0.0f;
        int corrN = 0;
    };
}
