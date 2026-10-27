#pragma once

#include <array>
#include <complex>

#include <juce_dsp/juce_dsp.h>

namespace dooblrvocals::dsp
{
    /** RBJ biquad (direct form 1, stereo). */
    class Biquad
    {
    public:
        enum class Type { lowShelf, peak, highShelf };

        void setParameters (Type type, double frequency, double q, double gainDb, double sampleRate) noexcept;
        void reset() noexcept;
        float process (int channel, float x) noexcept;
        double magnitudeAt (double frequency, double sampleRate) const noexcept;

    private:
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<std::array<double, 4>, 2> state {};
    };

    //==============================================================================
    struct EqSettings
    {
        bool enabled = false;
        float lowFreq = 80.0f, lowGainDb = 0.0f;
        float midFreq = 1000.0f, midGainDb = 0.0f, midQ = 1.0f;
        float highFreq = 8000.0f, highGainDb = 0.0f;
    };

    class ThreeBandEq
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings&) noexcept;

    private:
        double sampleRate = 48000.0;
        Biquad low, mid, high;
        EqSettings last;
        bool hasCoefficients = false;
    };

    //==============================================================================
    struct CompressorSettings
    {
        bool enabled = false;
        float thresholdDb = -12.0f, ratio = 4.0f, attackMs = 5.0f, releaseMs = 80.0f, makeupDb = 0.0f;
    };

    /** Feed-forward peak compressor, stereo linked, 6 dB soft knee. */
    class Compressor
    {
    public:
        void prepare (double sampleRate);
        void reset() { envelopeDb = 0.0f; lastMinReductionDb = 0.0f; }
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const CompressorSettings&) noexcept;

        static constexpr float kneeDb = 6.0f;

        static float staticReductionDb (float inputDb, float thresholdDb, float ratio) noexcept;
        float getLastMinReductionDb() const noexcept { return lastMinReductionDb; }

    private:
        double sampleRate = 48000.0;
        float envelopeDb = 0.0f;
        float lastMinReductionDb = 0.0f;
    };

    //==============================================================================
    struct LimiterSettings
    {
        enum Mode { off = 0, clipper, limiter };

        int mode = limiter;
        float ceilingDb = -0.3f;
        float releaseMs = 60.0f;
        float outputDb = 0.0f;
    };

    /** Final stage: soft-knee clipper or zero-latency peak limiter, then output gain. */
    class OutputStage
    {
    public:
        void prepare (double sampleRate);
        void reset() { limiterGain = 1.0f; outputGain = -1.0f; lastReductionDb = 0.0f; lastPeakOut = 0.0f; }
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const LimiterSettings&) noexcept;

        static float softClip (float x, float ceiling) noexcept;
        float getLastGainReductionDb() const noexcept { return lastReductionDb; }
        float getLastPeakOut() const noexcept { return lastPeakOut; }

    private:
        double sampleRate = 48000.0;
        float limiterGain = 1.0f;
        float outputGain = -1.0f;
        float lastReductionDb = 0.0f;
        float lastPeakOut = 0.0f;
    };

    //==============================================================================
    struct GateSettings
    {
        bool enabled = false;
        float thresholdDb = -40.0f;
        float attackMs = 1.0f;
        float holdMs = 50.0f;
        float releaseMs = 80.0f;
        float rangeDb = 80.0f; // attenuation when closed (0 = no gate, 80 ≈ mute)
    };

    /**
        Downward expander / gate, stereo-linked peak detector.
        Open above threshold, closed below; gain moves with attack / hold / release.
        Range is how far the closed gain drops (dB of attenuation).
    */
    class Gate
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const GateSettings&) noexcept;

        float getLastMinReductionDb() const noexcept { return lastMinReductionDb; }
        float getLastGain() const noexcept { return lastGain; }

        /** Static closed/open gain for a detector level (no ballistics). */
        static float staticGain (float inputDb, float thresholdDb, float rangeDb) noexcept;

    private:
        double sampleRate = 48000.0;
        float gain = 1.0f;
        int holdSamplesLeft = 0;
        float lastMinReductionDb = 0.0f;
        float lastGain = 1.0f;
        bool open = false;
    };

    /** The final output gain, with the per-sample 5 ms ramp OutputStage used in kick. */
    class OutputGain
    {
    public:
        void prepare (double newSampleRate) { sampleRate = newSampleRate; reset(); }
        void reset() noexcept { gain = -1.0f; }
        void process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb) noexcept;

    private:
        double sampleRate = 48000.0;
        float gain = -1.0f;
    };
}
