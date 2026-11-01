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
        enum class Type { lowShelf, peak, highShelf, highPass }; // highPass ignores gainDb

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
        bool hpfOn = false;      // high-pass (low cut), off by default so existing sessions are unchanged
        float hpfFreq = 80.0f;
        int hpfSlope = 1;        // 0 = 12, 1 = 24, 2 = 48 dB/oct

        /** Per-band dynamic EQ (low shelf, mid bell, high shelf), off by default so existing sessions
            and presets are unchanged. Same law and ranges as dooblr-q's dynamic bands. */
        struct BandDynamics
        {
            bool on = false;
            float rangeDb = 0.0f;       // -24..24: negative = the band cuts as it gets louder, positive = boosts
            float thresholdDb = -24.0f; // -60..0 dBFS, detector level
            float attackMs = 10.0f;     // 0.1..200
            float releaseMs = 120.0f;   // 5..2000
        };
        std::array<BandDynamics, 3> dynamics {};
    };

    /** Dynamic EQ helpers copied from dooblr-q (Source/DSP/DynamicEQBand.cpp, SvfFilter.cpp, read-only
        there): the gain law and the side-chain detector filter, so both plug-ins behave the same. */
    namespace eqdynamics
    {
        /** dB of gain change per dB above threshold (a 2:1 ratio-style law), soft knee, total clamp. */
        inline constexpr float dynamicSlope = 0.5f, kneeWidthDb = 6.0f, maxTotalGainDb = 30.0f;

        /** Detector level (dB) to the signed dynamic gain offset (dB), |offset| <= |range|. */
        float computeDynamicGainDb (float levelDb, float thresholdDb, float rangeDb) noexcept;

        /** TPT state-variable filter (Simper / Zavalishin), used as the side-chain detector. */
        struct SvfCoefficients { double a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0; };
        struct SvfState { double ic1 = 0, ic2 = 0; void reset() noexcept { ic1 = ic2 = 0; } };

        enum class DetectorShape { lowShelf, bell, highShelf };
        /** Low shelf: low-pass at the corner; high shelf: high-pass; bell: unity-peak band-pass from Q. */
        SvfCoefficients makeDetectorCoefficients (DetectorShape shape, double frequency, double q, double sampleRate) noexcept;

        inline float processSvf (float input, SvfState& s, const SvfCoefficients& c) noexcept
        {
            const auto v0 = (double) input;
            const auto v3 = v0 - s.ic2;
            const auto v1 = c.a1 * s.ic1 + c.a2 * v3;
            const auto v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            return (float) (c.m0 * v0 + c.m1 * v1 + c.m2 * v2);
        }
    }

    /** Butterworth high-pass, 12 / 24 / 48 dB/oct as 1 / 2 / 4 cascaded second-order sections. */
    class HighPass
    {
    public:
        static constexpr int maxSections = 4;
        static int sectionsForSlope (int slopeIndex) noexcept { return slopeIndex <= 0 ? 1 : (slopeIndex == 1 ? 2 : 4); }
        static float slopeDbPerOctave (int slopeIndex) noexcept { return 12.0f * (float) sectionsForSlope (slopeIndex); }

        void setParameters (double frequency, int slopeIndex, double sampleRate) noexcept;
        void reset() noexcept;
        float process (int channel, float x) noexcept
        {
            for (int i = 0; i < numSections; ++i)
                x = sections[(size_t) i].process (channel, x);
            return x;
        }
        double magnitudeAt (double frequency, double sampleRate) const noexcept;

    private:
        std::array<Biquad, maxSections> sections;
        int numSections = 1;
    };

    class ThreeBandEq
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings&) noexcept;

        /** Dynamic EQ: coefficients are recomputed every subBlock samples while any band is dynamic. */
        static constexpr int subBlock = 16;
        /** The dynamic gain offset (dB) a band is applying now (0 when its dynamics are off). */
        float getDynamicGainDb (int band) const noexcept { return band >= 0 && band < 3 ? dynamicGainDb[(size_t) band] : 0.0f; }
        static bool isDynamic (const EqSettings::BandDynamics& d) noexcept { return d.on && std::abs (d.rangeDb) > 1.0e-3f; }

    private:
        void setBandCoefficients (int band, const EqSettings&, float extraGainDb) noexcept;
        void processDynamic (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const EqSettings&) noexcept;

        double sampleRate = 48000.0;
        std::array<float, 3> dynamicGainDb {}, envelope {};
        std::array<std::array<eqdynamics::SvfState, 2>, 3> detectorState {};
        std::array<bool, 3> dynamicApplied {};
        Biquad low, mid, high;
        HighPass hpf;
        EqSettings last;
        bool hasCoefficients = false, hpfActive = false;
        float lastHpfFreq = 0.0f;
        int lastHpfSlope = -1;
    };

    //==============================================================================
    struct CompressorSettings
    {
        bool enabled = false;
        float thresholdDb = -12.0f, ratio = 4.0f, attackMs = 5.0f, releaseMs = 80.0f, makeupDb = 0.0f;
        float kneeDb = 6.0f; // soft-knee width; 6 dB is the original fixed knee, so old sessions are unchanged
    };

    /** Feed-forward peak compressor, stereo linked, adjustable soft knee (0-24 dB, default 6 dB). */
    class Compressor
    {
    public:
        void prepare (double sampleRate);
        void reset() { envelopeDb = 0.0f; lastMinReductionDb = 0.0f; }
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const CompressorSettings&) noexcept;

        static constexpr float defaultKneeDb = 6.0f, maxKneeDb = 24.0f;

        /** Gain computer (dB of reduction, <= 0). Quadratic soft knee of width kneeDb centred on the threshold;
            kneeDb = 0 is a hard knee. */
        static float staticReductionDb (float inputDb, float thresholdDb, float ratio, float kneeDb = defaultKneeDb) noexcept;
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

    //==============================================================================
    struct DeEsserSettings
    {
        enum Mode { split = 0, wideband };

        bool enabled = false;       // off by default: presets and existing sessions are unchanged
        float frequency = 6500.0f;  // 3..12 kHz: centre of the detector band and corner of the split band
        float thresholdDb = -30.0f; // -60..0 dBFS: the detector band is held down to this level
        float rangeDb = 8.0f;       // 0..24 dB: the most it will cut
        int mode = split;           // split: only the band above the frequency is cut; wideband: the whole signal
        bool listen = false;        // monitor the side-chain (detector) band instead of the output
    };

    /**
        De-esser. A fourth-order band-pass side chain (two TPT state-variable band-passes, Q 0.8,
        centred on the frequency) feeds a stereo-linked peak follower (0.5 ms attack, 40 ms release).
        The cut is the follower's level above the threshold (4 dB soft knee), capped at Range, so the
        sibilant band is held near the threshold. Split: a second-order (Butterworth) high shelf half an
        octave below the frequency whose top gain is the cut g, from one TPT state-variable filter:
        x - (1 - g) hp - (1 - sqrt g) k bp = lp + sqrt(g) k bp + g hp. Its gain is never above 1 at any
        frequency (|N|^2 - |D|^2 = -(1 - g^2) w^4), so nothing below the band is boosted, and with no
        cut the output is the input sample for sample. Wideband: the whole signal is turned down. Listen outputs the side-chain band.
        No latency. Switching on / off ramps over 10 ms; fully off, the buffer is never touched.
    */
    class DeEsser
    {
    public:
        static constexpr float attackMs = 0.5f, releaseMs = 40.0f, kneeDb = 4.0f, detectorQ = 0.8f;
        static constexpr double splitK = 1.4142135623730951; // Butterworth splitter damping (1 / Q)
        /** The split shelf's corner sits half an octave below Frequency, so the band the side chain is
            centred on gets most of the cut (a shelf at Frequency itself cuts its centre by only ~half). */
        static constexpr double splitCornerRatio = 0.70710678118654752;
        static constexpr float minFrequency = 3000.0f, maxFrequency = 12000.0f, maxRangeDb = 24.0f;

        void prepare (double sampleRate);
        void reset() noexcept;
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const DeEsserSettings&) noexcept;

        /** Static gain computer: dB of cut (>= 0) for a detector level, threshold and range. */
        static float staticReductionDb (float levelDb, float thresholdDb, float rangeDb) noexcept;
        /** Side-chain filter magnitude at a frequency (for the display and tests). */
        static double detectorMagnitudeAt (double frequency, double centre, double sampleRate) noexcept;
        /** Split-mode gain at a frequency for a steady cut (dB, >= 0) with the Frequency control at `corner`
            (the shelf itself sits at corner * splitCornerRatio). */
        static double splitGainAt (double frequency, double corner, double sampleRate, double cutDb) noexcept;

        /** Deepest cut in the last block (dB, <= 0) and the current cut. */
        float getLastMinReductionDb() const noexcept { return lastMinReductionDb; }
        float getCurrentReductionDb() const noexcept { return currentReductionDb; }
        /** Peak of the side-chain band in the last block (linear). */
        float getLastBandPeak() const noexcept { return lastBandPeak; }
        bool isActive() const noexcept { return wet > 0.0f; }

    private:
        void updateCoefficients (float frequency) noexcept;

        double sampleRate = 48000.0;
        eqdynamics::SvfCoefficients detector, splitter;
        std::array<std::array<eqdynamics::SvfState, 2>, 2> detectorState {};
        std::array<eqdynamics::SvfState, 2> splitState {};
        float coefficientFrequency = -1.0f;
        float envelope = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float wet = 0.0f, wetStep = 0.0f;
        float lastMinReductionDb = 0.0f, currentReductionDb = 0.0f, lastBandPeak = 0.0f;
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
