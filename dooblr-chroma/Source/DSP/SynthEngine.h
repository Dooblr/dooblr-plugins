#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Biquad.h"
#include "Modes.h"

namespace dooblrchroma::dsp
{
    struct ResonatorSettings
    {
        bool on = true;
        ResType type = ResType::string;
        int density = 1;
        float pitch = 0.0f;      // semitones
        float cents = 0.0f;
        float key = 1.0f;
        float pLevel = 0.0f;
        float pRate = 0.05f;
        float pVel = 0.0f;
        float decay = 1.4f;
        float decayKey = 0.0f;
        float decayVel = 0.0f;
        float release = 0.35f;
        float material = -0.35f;
        float tone = -3.0f;
        float lowCut = 0.0f;
        float radius = 0.55f;
        float hit = 0.22f;
        float hitVel = 0.0f;
        float hitKey = 0.0f;
        float hitRnd = 0.0f;
        float partials[kMaxManualPartials] { 1.0f, 2.0f, 3.0f, 4.0f };
    };

    struct LayerSettings
    {
        ResonatorSettings a, b;
        bool couple = false;
        float balance = 0.5f;
        float level = 1.0f;
        float transpose = 0.0f;
    };

    struct Settings
    {
        float malletStiffness = 0.55f, malletNoise = 0.12f, malletColor = 1800.0f;
        float mixMallet = 0.85f, mixNoise = 0.0f, mixDirect = 0.0f;
        int noiseFilter = 0;
        float noiseFreq = 1800.0f, noiseQ = 0.8f;
        int envMode = 0;
        float envAttack = 0.005f, envHold = 0.08f, envDecay = 0.25f, envSustain = 0.0f, envRelease = 0.15f;
        float lfoRate = 4.0f, lfoDepth = 0.0f;
        int unisonCount = 1;
        float unisonDetune = 8.0f;
        float vibratoRate = 5.0f, vibratoDepth = 0.0f;
        float glide = 0.0f;
        float macro1 = 0, macro2 = 0, macro3 = 0, macro4 = 0;
        int layerMode = 0;
        float splitNote = 60.0f;
        LayerSettings layers[2];
        float outputDb = 0.0f;
        float eqLow = 0, eqMid = 0, eqHigh = 0, eqMidFreq = 1000.0f;
        float compThreshold = 0, compRatio = 4, compAttack = 0.01f, compRelease = 0.12f, compMix = 0;
        float distDrive = 0, distMix = 0.5f;
        float chorusRate = 0.8f, chorusDepth = 0.35f, chorusMix = 0;
        float delayTime = 0.35f, delayFeedback = 0.35f, delayMix = 0;
        float reverbSize = 0.45f, reverbDamp = 0.4f, reverbMix = 0;
    };

    class SynthEngine
    {
    public:
        static constexpr int kMaxVoices = 8;
        static constexpr int kMaxUnison = 4;

        void setSettings (const Settings& s) { settings = s; }
        const Settings& getSettings() const noexcept { return settings; }

        void prepare (double sampleRate, int samplesPerBlock);
        void reset();
        void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

        float peak() const noexcept { return peakAbs; }

    private:
        struct ModeOsc
        {
            float a1 = 0, a2 = 0, y1 = 0, y2 = 0, x1 = 0, gain = 0, coupleGain = 0, freq = 0;

            void clear() noexcept { y1 = y2 = x1 = 0; }

            float process (float x) noexcept
            {
                const float y = gain * (x - x1) + a1 * y1 + a2 * y2;
                x1 = x;
                y2 = y1;
                y1 = y;
                if (std::abs (y1) < 1.0e-30f) y1 = 0;
                if (std::abs (y2) < 1.0e-30f) y2 = 0;
                return y;
            }
        };

        struct ModalBank
        {
            std::array<ModeOsc, kMaxModes> modes {};
            int n = 0;
            Biquad lowCut1, lowCut2;
            bool lowCutOn = false;
            float lastVel = 0, lastCoupleVel = 0;

            void clear();
            void setup (const ResonatorSettings& rs, float f0, float velocity, float hitPos,
                        float decayScale, float toneOffset, float sampleRate);
            float process (float excitation, float coupleIn);
        };

        struct UnisonPair
        {
            ModalBank a, b;
            float coupleA = 0, coupleB = 0;
        };

        struct LayerVoice
        {
            std::array<UnisonPair, kMaxUnison> unisons {};
            int unisonUsed = 1;
            float pitchEnv = 1.0f;
            float pitchEnvTarget = 1.0f;
            float pitchEnvCoeff = 0;
        };

        struct Voice
        {
            bool active = false;
            bool gate = false;
            int note = -1;
            float velocity = 0;
            float currentNote = 60.0f;
            float targetNote = 60.0f;
            int age = 0;
            LayerVoice layers[2];
            float env = 0;
            int envStage = 0; // 0 idle, 1 att, 2 hold, 3 dec, 4 sus, 5 rel
            float envTime = 0;
            float malletPhase = 1.0f;
            float malletDur = 0.001f;
            float lfoPhase = 0;
            float vibratoPhase = 0;
            float hitRnd[2][2] {}; // layer, resonator
            juce::Random rng { 1 };
            Biquad malletColor;
            Biquad noiseLp, noiseHp;
            float dampGain = 1.0f;   // smoothed release damper, starts at 1
            float declick = 0;       // residual from a steal / retrigger fade
            float lastOut = 0;

            void reset();
        };

        struct Fx
        {
            Biquad eqLowL, eqLowR, eqMidL, eqMidR, eqHighL, eqHighR;
            float compEnv = 0;
            std::vector<float> chorusL, chorusR;
            int chorusW = 0;
            float chorusPhase = 0;
            std::vector<float> delayL, delayR;
            int delayW = 0;
            // Freeverb-style comb/allpass
            static constexpr int kCombs = 4;
            static constexpr int kAllpasses = 2;
            std::vector<float> combL[kCombs], combR[kCombs];
            int combIdx[kCombs] {};
            float combFilterL[kCombs] {}, combFilterR[kCombs] {};
            std::vector<float> apL[kAllpasses], apR[kAllpasses];
            int apIdx[kAllpasses] {};
            float apZ1L[kAllpasses] {}, apZ1R[kAllpasses] {};

            void prepare (double sampleRate);
            void reset();
            void setEq (const Settings& s, float sampleRate);
            void process (float& left, float& right, const Settings& s, float sampleRate);
        };

        void noteOn (int note, float velocity);
        void noteOff (int note);
        void renderVoice (Voice& v, float* left, float* right, int start, int num);
        void updateVoicePitch (Voice& v, const Settings& s);

        Settings settings;
        std::array<Voice, kMaxVoices> voices;
        Fx fx;
        double sampleRate = 48000.0;
        int blockSize = 512;
        float peakAbs = 0;
        juce::Random noise { 0xC0FFEE };
    };
}
