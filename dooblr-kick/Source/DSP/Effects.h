#pragma once

#include <array>
#include <complex>
#include <memory>

#include <juce_dsp/juce_dsp.h>

namespace dooblrkick::dsp
{
    /** Topology-preserving state-variable filter (one channel). */
    class SvfFilter
    {
    public:
        enum class Mode { lowPass, highPass, bandPass };

        void setParameters (Mode newMode, float cutoffHz, float resonance01, double sampleRate) noexcept;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
        float process (float x) noexcept;

    private:
        Mode mode = Mode::lowPass;
        float g = 0.0f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

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
    struct DistortionSettings
    {
        enum Type { softClip = 0, hardClip, tube, fold, bitcrush, numTypes };

        bool enabled = false;
        int type = softClip;
        float driveDb = 12.0f;  // pre-gain into the shaper (bitcrush: preamp into the clip)
        float tone = 1.0f;      // 0 = dark (1 kHz low-pass) ... 1 = open (20 kHz)
        float bits = 8.0f;      // bitcrush only
        float rateHz = 12000.0f; // bitcrush only: sample-and-hold rate
        float outputDb = 0.0f;
        float mix = 1.0f;
    };

    /** Waveshaper bank. Soft/hard/tube/fold run 4x oversampled; bitcrush aliases on purpose. */
    class Distortion
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const DistortionSettings&) noexcept;

        /** The static curve (used by tests and the oversampled path). */
        static float shape (int type, float x, float gain) noexcept;

    private:
        double sampleRate = 48000.0;
        int maxBlock = 512;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        juce::AudioBuffer<float> dry;
        std::array<float, 2> toneState {}, holdValue {}, dcX {}, dcY {};
        float holdPhase = 1.0f;
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
    struct ShaperSettings
    {
        bool enabled = false;
        float attack = 0.0f;  // -1 .. +1
        float sustain = 0.0f; // -1 .. +1
    };

    /** Envelope-difference transient shaper (stereo linked). */
    class TransientShaper
    {
    public:
        void prepare (double sampleRate);
        void reset() { fast = slow = 0.0f; }
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const ShaperSettings&) noexcept;

    private:
        float fastAttack = 0, fastRelease = 0, slowAttack = 0, slowRelease = 0;
        float fast = 0.0f, slow = 0.0f;
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

        /** The static curve: target gain reduction (<= 0 dB) for a detector level, before the
            attack / release ballistics. Shared by the DSP and the editor's transfer curve. */
        static float staticReductionDb (float inputDb, float thresholdDb, float ratio) noexcept;

        /** Deepest gain reduction (<= 0 dB, makeup excluded) during the last process() call;
            0 when the compressor is off. */
        float getLastMinReductionDb() const noexcept { return lastMinReductionDb; }

    private:
        double sampleRate = 48000.0;
        float envelopeDb = 0.0f; // current gain reduction (<= 0)
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
        void reset() { limiterGain = 1.0f; outputGain = -1.0f; }
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const LimiterSettings&) noexcept;

        static float softClip (float x, float ceiling) noexcept;

        /** Deepest limiter / clipper gain reduction (<= 0 dB) during the last process() call
            (clipper: the smallest output / input ratio of any sample). 0 in Off mode. */
        float getLastGainReductionDb() const noexcept { return lastReductionDb; }

        /** Peak after the limiter / clipper and before the output gain, last process() call. */
        float getLastPeakOut() const noexcept { return lastPeakOut; }

    private:
        double sampleRate = 48000.0;
        float limiterGain = 1.0f;
        float outputGain = -1.0f; // < 0: jump to the target on the next block
        float lastReductionDb = 0.0f;
        float lastPeakOut = 0.0f;
    };
}
