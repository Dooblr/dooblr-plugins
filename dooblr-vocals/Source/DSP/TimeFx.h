#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace dooblrvocals::dsp
{
    //==============================================================================
    /** Note values for tempo sync, in quarter-note beats (D = dotted, T = triplet). */
    inline constexpr int numNoteValues = 16;
    const char* noteValueName (int index) noexcept;
    double noteValueBeats (int index) noexcept;
    double noteValueSamples (int index, double bpm, double sampleRate) noexcept;

    /** Reverb pre-delay sync: 0 = off (use ms), else a short note value. */
    inline constexpr int numPreDelaySyncs = 7;
    const char* preDelaySyncName (int index) noexcept;
    double preDelaySyncBeats (int index) noexcept; // 0 for Off

    enum class FxRouting { parallel = 0, delayToReverb, reverbToDelay };

    //==============================================================================
    struct ReverbSettings
    {
        bool enabled = false;
        float preDelayMs = 20.0f;
        int preDelaySync = 0;          // 0 = Off (ms)
        float decaySeconds = 1.6f;     // RT60 of the late tail (mid frequencies)
        float size = 0.5f;             // 0..1: room size (line lengths and early spread)
        float damping = 0.45f;         // 0..1: how much faster the highs decay
        float lowCutHz = 250.0f;       // on the wet signal
        float highCutHz = 9000.0f;     // on the wet signal
        float width = 1.0f;            // 0..1.5
        float earlyLate = 0.6f;        // 0 = early reflections only, 0.5 = both, 1 = late only
        float diffusion = 0.75f;       // 0..1
        float modulation = 0.3f;       // 0..1
        float duckDb = 0.0f;           // 0..24 dB of wet reduction while the dry vocal is present
        float duckReleaseMs = 300.0f;
        float mix = 0.25f;             // 0..1 (dry stays at unity up to 50 %)
    };

    struct DelaySettings
    {
        enum Mode { stereo = 0, pingPong, dual };

        bool enabled = false;
        int mode = stereo;
        bool sync = true;
        int noteL = 10, noteR = 8;     // 1/4 and 1/8D
        float timeLMs = 350.0f, timeRMs = 260.0f;
        float feedback = 0.3f;         // 0..0.95
        float lowCutHz = 200.0f;       // in the feedback loop
        float highCutHz = 6000.0f;     // in the feedback loop
        float modulation = 0.0f;       // 0..1
        float drive = 0.0f;            // 0..1 saturation in the loop
        float duckDb = 0.0f;
        float duckReleaseMs = 250.0f;
        float width = 1.0f;
        float mix = 0.2f;
    };

    /** Dry / wet gains for a mix setting: the dry stays at unity up to 50 % and the wet reaches
        unity at 50 %, so 0 % is exactly the dry signal and 100 % is wet only. */
    inline float dryGainFor (float mix) noexcept { return juce::jlimit (0.0f, 1.0f, 2.0f * (1.0f - mix)); }
    inline float wetGainFor (float mix) noexcept { return juce::jlimit (0.0f, 1.0f, 2.0f * mix); }

    //==============================================================================
    /** Second-order Butterworth high / low pass (TDF-II), per channel. */
    class SimpleFilter
    {
    public:
        void setLowPass (double hz, double sampleRate) noexcept;
        void setHighPass (double hz, double sampleRate) noexcept;
        void reset() noexcept { z = {}; }
        float process (int ch, float x) noexcept
        {
            auto& s = z[(size_t) ch];
            const auto y = b0 * x + s[0];
            s[0] = b1 * x - a1 * y + s[1];
            s[1] = b2 * x - a2 * y;
            return y;
        }

    private:
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<std::array<float, 2>, 2> z {};
    };

    /** Peak follower on the dry signal -> wet gain (fast attack, settable release). */
    class Ducker
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept { envelope = 0.0f; gain = 1.0f; }
        /** Returns the wet gain for this sample. */
        float process (float keyAbs, float amountDb, float releaseMs) noexcept;
        float getGain() const noexcept { return gain; }

        static constexpr float thresholdDb = -40.0f, kneeDb = 20.0f;

    private:
        double sampleRate = 48000.0;
        float envelope = 0.0f, gain = 1.0f, attackCoeff = 0.0f;
        float lastReleaseMs = -1.0f, releaseCoeff = 0.0f;
    };

    //==============================================================================
    /**
        Vocal reverb: pre-delay, an input diffuser (4 allpasses), early reflections (12 taps
        spread over the size), and an 8-line feedback delay network with a Householder matrix.
        Each line has a gain for the RT60 and a one-pole HF absorber so the highs decay faster
        with Damping; four lines are slowly modulated to avoid metallic ringing. The wet is
        band-limited (low cut / high cut), widened (M/S), ducked by the dry and mixed.
        Adds no latency. prepare() allocates; process() does not.
    */
    class VocalReverb
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;

        /** Renders the wet signal only (ducking applied, no mix). key: the dry vocal for ducking. */
        void renderWet (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                        const float* keyL, const float* keyR, const ReverbSettings& s, double bpm) noexcept;

        float getDuckDb() const noexcept { return juce::Decibels::gainToDecibels (ducker.getGain(), -60.0f); }

        static constexpr int numLines = 8, numEarly = 12, numDiffusers = 4;
        static constexpr double maxPreDelayMs = 500.0;

        /** Early reflection t (0..numEarly-1): its time after the pre-delay (ms) and relative level; for the display. */
        static double earlyTapMs (int t, float size) noexcept;
        static float earlyTapGain (int t) noexcept;
        /** The pre-delay (ms) the settings give at a tempo (synced or ms, clamped). */
        static double preDelayMsFor (const ReverbSettings& s, double bpm) noexcept;

    private:
        void updateCoefficients (const ReverbSettings& s) noexcept;
        static float readFrac (const std::vector<float>& line, int writePos, float delay) noexcept;

        double sampleRate = 48000.0;
        std::vector<float> preDelay;
        int preWrite = 0;

        std::array<std::vector<float>, numDiffusers> diffusers;
        std::array<int, numDiffusers> diffuserLength {}, diffuserPos {};

        std::vector<float> earlyLine;
        int earlyWrite = 0;
        std::array<float, numEarly> earlyDelay {}, earlyGain {};

        std::array<std::vector<float>, numLines> lines;
        std::array<int, numLines> lineWrite {};
        std::array<float, numLines> lineDelay {}, lineGain {}, dampCoeff {}, dampState {};
        std::array<double, numLines> lfoPhase {};

        SimpleFilter lowCut, highCut;
        Ducker ducker;

        ReverbSettings last;
        bool haveCoefficients = false;
        float lateNorm = 1.0f, diffuseGain = 0.5f, earlyLevel = 1.0f, lateLevel = 1.0f, modDepth = 0.0f;
        float preNow = -1.0f;
    };

    //==============================================================================
    /**
        Vocal delay: stereo, ping-pong or dual L / R times, in ms or synced to the host tempo
        (note values with dotted and triplet). The feedback loop holds a high pass, a low pass,
        optional soft saturation and a subtle time modulation; feedback is capped below unity
        and the loop gain never exceeds it, so even the maximum setting decays. The wet is
        widened (M/S) and ducked by the dry. Adds no latency. prepare() allocates.
    */
    class VocalDelay
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;

        void renderWet (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                        const float* keyL, const float* keyR, const DelaySettings& s, double bpm) noexcept;

        /** The delay (samples) the settings give for a channel (0 = L, 1 = R) at a tempo. */
        static double delaySamplesFor (const DelaySettings& s, int channel, double bpm, double sampleRate) noexcept;

        float getDuckDb() const noexcept { return juce::Decibels::gainToDecibels (ducker.getGain(), -60.0f); }

        static constexpr double maxDelaySeconds = 6.0;
        static constexpr float maxFeedback = 0.95f;

    private:
        double sampleRate = 48000.0;
        std::array<std::vector<float>, 2> lines;
        int writePos = 0;
        std::array<float, 2> currentDelay { -1.0f, -1.0f };
        SimpleFilter lowCut, highCut;
        float lastLowCut = -1.0f, lastHighCut = -1.0f;
        Ducker ducker;
        double lfoPhase = 0.0;
    };

    //==============================================================================
    /**
        The time-based FX block (delay + reverb as one unit), fixed after the processing chain
        and before the output trim and the limiter (the limiter is always the last stage).
        Routing: Parallel (both fed from the block input and summed), Delay > Reverb or Reverb > Delay (series: each stage mixes its own dry / wet). Ducking is
        keyed by the block input (the processed dry vocal) in every routing.

        Enabling, disabling and the mixes ramp (linear, 20 ms); a module that is off and fully
        faded out is skipped and reset, and with both off (or both mixes at 0) the block output
        is bit-identical to its input.
    */
    class FxBlock
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;
        void process (juce::AudioBuffer<float>& buffer, int numSamples, const ReverbSettings& reverb,
                      const DelaySettings& delay, FxRouting routing, double bpm) noexcept;

        float getReverbDuckDb() const noexcept { return reverbDuckDb; }
        float getDelayDuckDb() const noexcept { return delayDuckDb; }

        /** Tests: when set, the L channel the reverb / delay sees is copied here (not owned). */
        struct Probe { float* reverbIn = nullptr; float* delayIn = nullptr; int capacity = 0; int written = 0; };
        void setProbe (Probe* p) noexcept { probe = p; }

        VocalReverb& getReverb() noexcept { return reverb; }
        VocalDelay& getDelay() noexcept { return delay; }

    private:
        struct Ramp
        {
            float value = 0.0f, target = 0.0f, step = 0.0f;
            void set (float t, int numRampSamples) noexcept
            {
                if (! juce::exactlyEqual (t, target)) { target = t; step = (target - value) / (float) std::max (1, numRampSamples); }
            }
            float next() noexcept
            {
                if (! juce::exactlyEqual (value, target))
                {
                    value += step;
                    if ((step > 0.0f && value >= target) || (step < 0.0f && value <= target) || juce::exactlyEqual (step, 0.0f))
                        value = target;
                }
                return value;
            }
        };

        void runStage (bool isReverb, const float* inL, const float* inR, float* outL, float* outR, int n,
                       const float* keyL, const float* keyR, const ReverbSettings& rs, const DelaySettings& ds,
                       double bpm, Ramp& dry, Ramp& wet, bool addToOutput) noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512, rampSamples = 960;
        VocalReverb reverb;
        VocalDelay delay;
        juce::AudioBuffer<float> keyBuffer, stageBuffer, wetBuffer, sumBuffer;
        Ramp reverbDry, reverbWet, delayDry, delayWet;
        bool reverbActive = false, delayActive = false;
        float reverbDuckDb = 0.0f, delayDuckDb = 0.0f;
        Probe* probe = nullptr;
    };
}
