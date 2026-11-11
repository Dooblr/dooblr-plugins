#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace dooblrsub::dsp
{
    /**
        Autocorrelation pitch tracker on a low-passed tracking band.

        A one-pole low-pass (and a matching high-pass at ~20 Hz) isolate the fundamental
        region. Every hop, a normalised autocorrelation of the last window is searched
        between the period limits implied by Frequency (with a floor of 30 Hz and a
        ceiling of 120 Hz). The lag of the first strong peak is interpolated and
        smoothed by Tracking speed.

        The tracker is intentionally simple and robust on bass / kick / tom fundamentals
        rather than a general-purpose YIN of speech.
    */
    class PitchTracker
    {
    public:
        static constexpr int windowSize = 2048;
        static constexpr int hopSize = 256;

        void prepare (double sampleRateToUse);
        void reset() noexcept;

        /** Push one (mono) sample. Returns the current smoothed frequency in Hz (0 if unvoiced). */
        float process (float sample, float crossoverHz, float trackingSpeed01) noexcept;

        float getFrequencyHz() const noexcept { return smoothedHz; }
        float getConfidence() const noexcept { return confidence; }
        bool isVoiced() const noexcept { return confidence > 0.35f && smoothedHz > 1.0f; }

    private:
        void analyse (float minHz, float maxHz) noexcept;

        double sampleRate = 44100.0;
        std::vector<float> buffer;
        int write = 0, hopCount = 0;
        float lp = 0.0f, hp = 0.0f, lpState = 0.0f, hpState = 0.0f;
        float smoothedHz = 0.0f, rawHz = 0.0f, confidence = 0.0f;
    };
}
