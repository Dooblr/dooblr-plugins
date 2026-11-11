#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Primitives.h"

namespace dooblrm::dsp
{
    /** Amount of Contour = 1 opens the ladder by this many octaves. */
    inline constexpr float contourOctaves = 5.0f;

    struct EnvSettings
    {
        float attack = 0.002f, decay = 0.5f, sustain = 1.0f;
        bool decaySwitch = true;
    };

    struct Settings
    {
        float tune = 0.0f;
        float glide = 0.0f;
        float modMix = 0.0f;
        bool osc3Control = true;
        bool oscMod = false;
        bool filterMod = false;

        int osc1Range = 2, osc1Wave = 2;
        int osc2Range = 2, osc2Wave = 2;
        float osc2Freq = 0.0f;
        int osc3Range = 2, osc3Wave = 0;
        float osc3Freq = 0.0f;
        bool osc3Lfo = false;
        float lfoRate = 1.0f;
        bool lfoSync = false;
        int lfoDivision = 2;
        bool lfoRetrigger = false;

        float mixOsc1 = 0.8f; bool mixOsc1On = true;
        float mixOsc2 = 0.0f; bool mixOsc2On = false;
        float mixOsc3 = 0.0f; bool mixOsc3On = false;
        float mixExt  = 0.0f; bool mixExtOn  = false;
        float mixNoise = 0.0f; bool mixNoiseOn = false;
        int noiseType = 0;

        float cutoff = 1200.0f;
        float emphasis = 0.15f;
        float contourAmt = 0.0f;
        EnvSettings filterEnv;
        bool kybd1 = false, kybd2 = false;

        EnvSettings ampEnv;

        float volume = 0.7f;

        int notePriority = 0; // 0 low, 1 last
        bool legato = true;
        int bendRange = 2;
        bool velocityOn = false;
        float outputDb = 0.0f;

        float modWheel = 0.0f;
        float pitchBend = 0.0f;
    };

    struct HostTime
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool hasPpq = false;
        bool playing = false;
    };

    class SynthEngine
    {
    public:
        void prepare (double sampleRate, int samplesPerBlock);
        void reset();
        void setSettings (const Settings& s) noexcept { settings = s; }
        const Settings& getSettings() const noexcept { return settings; }

        void process (float* left, float* right, int numSamples, juce::MidiBuffer& midi, const HostTime&);

        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();

        int getCurrentNote() const noexcept { return currentNote; }
        bool isGated() const noexcept { return gate; }
        float getAmpEnv() const noexcept { return amp.value; }
        float getFilterEnv() const noexcept { return filterEnvTap.load (std::memory_order_relaxed); }
        int getFilterEnvStage() const noexcept { return filterEnvStageTap.load (std::memory_order_relaxed); }
        const std::atomic<float>& getFilterEnvTap() const noexcept { return filterEnvTap; }
        const std::atomic<int>& getFilterEnvStageTap() const noexcept { return filterEnvStageTap; }
        float getLfoHz() const noexcept { return lastLfoHz; }
        float getLfoValue() const noexcept { return lastLfo; }
        float getLfoPhase() const noexcept { return lfo.phase; }
        const std::atomic<float>& getLfoTap() const noexcept { return lfoTap; }
        float getGlidePitch() const noexcept { return glide.current; }
        float getLastCutoffHz() const noexcept { return lastCutoffHz; }
        float getLastSample() const noexcept { return lastSample; }
        ScopeTap& getScopeTap() noexcept { return scope; }

        /** Semitones of MIDI pitch-bend currently applied (−1..1, from the wheel message). */
        float getMidiPitchBend() const noexcept { return midiPitchBend.load (std::memory_order_relaxed); }
        float getMidiModWheel() const noexcept { return midiModWheel.load (std::memory_order_relaxed); }

    private:
        void handleMidiBytes (const juce::uint8* data, int size);
        float oscillatorHz (int rangeIndex, float detuneSt, bool keyTrack, float note, float modSt, float driftSt) const noexcept;
        float renderSample();

        Settings settings;
        double sampleRate = 48000.0;

        float lfoTargetHz() const noexcept;

        BlitOsc osc1, osc2, osc3;
        LfoOsc lfo;
        Drift drift1, drift2, drift3;
        LadderFilter ladder;
        Contour filt, amp;
        Glide glide;
        Noise noise;
        ScopeTap scope;

        struct Held
        {
            int note = -1;
            int order = 0;
        };

        std::array<Held, 32> held {};
        int heldCount = 0;
        int noteOrder = 0;
        int currentNote = 60;
        float currentVelocity = 0.8f;
        bool gate = false;
        bool hasPlayed = false;
        bool sustainPedal = false;

        std::atomic<float> midiPitchBend { 0.0f };
        std::atomic<float> midiModWheel { 0.0f };
        std::atomic<float> filterEnvTap { 0.0f };
        std::atomic<int> filterEnvStageTap { 0 };
        std::atomic<float> lfoTap { 0.0f };

        HostTime hostTime;
        float feedbackSample = 0.0f;
        float lastSample = 0.0f;
        float lastCutoffHz = 1200.0f;
        float lastLfoHz = 1.0f;
        float lastLfo = 0.0f;
        float osc3AudioGain = 1.0f;
        float outputSmoothed = 1.0f;
        int scopeDecim = 0;
        Rng thermal;
    };
}
