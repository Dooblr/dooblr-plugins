#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace dooblrprophet::dsp
{
    /** Echo and reverb presets (original voicings for poly pads, leads and unison / mono lines).
        A preset sets the module's character (filters, drive, wow, ducking, space) and the
        macro values it starts from; the macros then move within it. */
    inline constexpr int numEchoPresets = 10, numReverbPresets = 10;

    struct EchoPreset
    {
        const char* name;
        bool sync; int division; float timeMs; float feedback; float mix; bool pingPong; // macro starting values
        float lowCutHz, highCutHz;  // tone of the repeats (in the feedback loop)
        float drive;                // 0..1 soft saturation in the loop
        float wowMs, wowHz;         // delay-time wobble
        float duck;                 // 0..1: repeats duck under the dry signal
        float spread;               // stereo (not ping-pong): right repeats are later by this fraction
    };

    struct ReverbPreset
    {
        const char* name;
        float size, tone, mix;                       // macro starting values
        float preDelayMs;
        float scaleMin, scaleMax;                    // room scale across the Size / Decay macro
        float decayMin, decayMax;                    // RT60 in seconds across the Size / Decay macro
        float lowCutHz, modMs, modHz, diffusion, width, spring;
    };

    const std::array<EchoPreset, numEchoPresets>& echoPresets();
    const std::array<ReverbPreset, numReverbPresets>& reverbPresets();
    const juce::StringArray& echoPresetNames();
    const juce::StringArray& reverbPresetNames();

    /** Echo tempo-sync divisions and their length in quarter notes. */
    const juce::StringArray& echoDivisionNames();
    double echoDivisionQuarters (int index);

    inline constexpr float maxEchoSeconds = 4.0f, maxEchoFeedback = 0.95f;

    struct FxSettings
    {
        bool echoOn = false;
        int echoPreset = 0;
        float echoMix = 0.25f, echoTimeMs = 110.0f, echoFeedback = 0.12f;
        bool echoSync = false, echoPingPong = false;
        int echoDivision = 8;

        bool reverbOn = false;
        int reverbPreset = 0;
        float reverbMix = 0.22f, reverbSize = 0.35f, reverbTone = 0.7f;

        bool operator== (const FxSettings&) const = default;
    };

    /** The echo time in seconds for these settings at this tempo (clamped to maxEchoSeconds). */
    double echoSeconds (const FxSettings& s, double bpm);
    /** The reverb's RT60 in seconds and pre-delay for these settings (for the graph and tests). */
    double reverbDecaySeconds (const FxSettings& s);

    /** Stereo echo: free or tempo-synced time, feedback with filtered, saturated repeats, wow,
        ducking, and a ping-pong mode (the mono-summed input feeds the left side; each repeat
        crosses to the other side). Zero latency; buffers are allocated in prepare only. */
    class Echo
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void process (float* left, float* right, int numSamples, const FxSettings& s, double bpm) noexcept;
        bool isIdle() const noexcept { return gain == 0.0f && cleared; }

    private:
        double sampleRate = 48000.0;
        std::vector<float> bufferL, bufferR;
        int size = 0, write = 0;
        float delay = 0.0f, gain = 0.0f, gainStep = 0.0f, timeCoef = 0.0f;
        float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f, duckEnv = 0.0f;
        double wowPhase = 0.0;
        bool primed = false, cleared = true;
        int clearPosition = 0;

        float read (const std::vector<float>& buffer, float delaySamples) const noexcept;
    };

    /** Stereo reverb: pre-delay, input diffusion, an 8-line feedback delay network with
        per-line damping and modulation, an optional spring-like dispersion stage, a low cut and
        stereo width. Original design. Zero latency; buffers are allocated in prepare only. */
    class Reverb
    {
    public:
        static constexpr int numLines = 8, numDiffusers = 4, numDispersers = 6;

        void prepare (double sampleRate);
        void reset();
        void process (float* left, float* right, int numSamples, const FxSettings& s) noexcept;
        bool isIdle() const noexcept { return gain == 0.0f && cleared; }

    private:
        struct Line { std::vector<float> data; int size = 0; float damp = 0.0f; float cosine = 1.0f, sine = 0.0f, rotC = 1.0f, rotS = 0.0f; };
        struct Allpass { std::vector<float> data; int size = 0, pos = 0; };

        double sampleRate = 48000.0;
        std::array<Line, numLines> lines;
        int lineWrite = 0;
        std::vector<float> preL, preR;
        int preSize = 0, preWrite = 0;
        std::array<Allpass, numDiffusers> diffL, diffR;
        std::array<float, numDispersers> dispL {}, dispR {};
        float scale = 1.0f, gain = 0.0f, gainStep = 0.0f, scaleCoef = 0.0f;
        float lowL = 0.0f, lowR = 0.0f;
        bool primed = false, cleared = true;
        int clearStage = 0;
    };

    /** The global FX stage after the voice mix: Echo, then Reverb. Off modules are skipped, so
        with both off the output is the input, bit for bit. */
    class FxChain
    {
    public:
        void prepare (double sampleRate) { echo.prepare (sampleRate); reverb.prepare (sampleRate); }
        void reset() { echo.reset(); reverb.reset(); }

        /** left == right is allowed (mono bus): the stereo result is summed back to mono. */
        void process (float* left, float* right, int numSamples, const FxSettings& s, double bpm) noexcept;

    private:
        Echo echo;
        Reverb reverb;
    };
}
