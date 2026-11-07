#pragma once

#include <array>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace dooblrfeedback::dsp
{
    /**
        12 dB/oct Butterworth high-pass (one TPT SVF section, Q = 1/sqrt(2)).

        Off (mix at 0) leaves the buffer untouched, so the rest of the path is bit-identical
        to a build without this filter. Switching on or off crossfades against the dry input
        over fadeSeconds. The cutoff is smoothed and coefficients refresh every 16 samples,
        so moving the frequency does not zipper.
    */
    class HighPass
    {
    public:
        static constexpr float minHz = 20.0f, maxHz = 400.0f, defaultHz = 80.0f;
        static constexpr float fadeSeconds = 0.005f;   // 5 ms on / off crossfade
        static constexpr float smoothSeconds = 0.02f;  // 20 ms cutoff glide
        static constexpr int controlInterval = 16;
        static constexpr float slopeDbPerOctave = 12.0f;

        void prepare (double sampleRate, int maxBlockSize) noexcept;
        void reset() noexcept;

        /** In-place. `dry` is the unfiltered input for the same block (used while the mix
            is between 0 and 1). When mix is 0 and `on` is false, `io` is not touched. */
        void process (float* const* io, const float* const* dry, int numChannels, int numSamples,
                      bool on, float freqHz) noexcept;

        float currentMix() const noexcept { return mix; }

        /** Analog 2nd-order Butterworth high-pass, dB (matches the TPT section at audio rates). */
        static float analogResponseDb (float freqHz, float cutoffHz) noexcept;

    private:
        void design (float cutoffHz) noexcept;
        void apply (int channel, float* x, int n) noexcept;

        double sampleRate = 48000.0;
        float mix = 0.0f, mixStep = 0.0f, k = 1.41421356f;
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        std::array<float, 2> ic1 {}, ic2 {};
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqSmooth;
        int controlCountdown = 0;
    };
}
