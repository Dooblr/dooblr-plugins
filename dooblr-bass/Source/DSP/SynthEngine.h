#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Primitives.h"

namespace dooblrbass::dsp
{
    inline constexpr int subBlockSize = 32;

    enum class Wave { saw = 0, square };
    enum class Osc3Wave { triangle = 0, saw, square };
    enum class LfoWave { triangle = 0, saw, reverseSaw, square, sampleHold };
    enum class NotePriority { last = 0, low };

    struct EnvSettings
    {
        float attack = 0.002f, decay = 0.28f, sustain = 0.18f, release = 0.22f, velocity = 0.35f;
    };

    /** Every sound parameter, read once per block from the processor's parameters. */
    struct Settings
    {
        Wave osc1Wave = Wave::saw, osc2Wave = Wave::saw;
        Osc3Wave osc3Wave = Osc3Wave::triangle;
        int osc1Octave = 1, osc2Octave = 1, osc3Octave = 1; // 0=32' .. 3=4'
        float osc1PW = 0.5f, osc2PW = 0.5f;
        float osc2Tune = 0.0f, osc3Tune = 0.0f;             // semitones
        bool osc3LoFreq = false, osc3KeyTrack = true;
        int subOctave = 0;                                  // 0 = -1 oct, 1 = -2 oct

        float mixOsc1 = 0.85f, mixOsc2 = 0.45f, mixOsc3 = 0.0f, mixSub = 0.35f, mixNoise = 0.0f;
        float mixDrive = 0.25f, mixOverload = 0.0f;

        float lpfCutoff = 420.0f, lpfReso = 0.28f, lpfEnvAmt = 0.45f, lpfKeyTrack = 0.35f;

        EnvSettings filterEnv { 0.002f, 0.28f, 0.18f, 0.22f, 0.35f };
        EnvSettings ampEnv { 0.002f, 0.45f, 0.85f, 0.18f, 0.0f };
        bool sharedDecay = false;

        float glide = 0.0f;
        bool legato = false;
        NotePriority notePriority = NotePriority::last;
        int bendRange = 2;
        float outputDb = 0.0f;

        LfoWave lfoWave = LfoWave::triangle;
        float lfoRate = 4.5f;
        bool lfoSync = false;
        double lfoSyncQuarters = 1.0;
        float lfoToPitch = 0.0f, lfoToFilter = 0.0f, lfoWheel = 0.3f;
    };

    struct HostTime
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool hasPpq = false, playing = false;
    };

    /** Control values shared for one sub-block (<= subBlockSize samples). */
    struct SubBlock
    {
        const Settings* settings = nullptr;
        double sampleRate = 48000.0;
        float lfo[subBlockSize] {};
        float bend = 0.0f;
        float mix[5] {};          // osc1, osc2, osc3, sub, noise
        float drive = 1.0f, overload = 0.0f;
        float lpfOctaves = 8.0f, lpfReso = 0.0f;
        float pw1 = 0.5f, pw2 = 0.5f;
        float outputGain = 1.0f;
        float lfoPitchAmt = 0.0f, lfoFiltAmt = 0.0f; // already include the mod wheel on pitch
    };

    class Voice
    {
    public:
        void prepare (double sampleRate, uint32_t seed);
        void reset();

        void start (int note, float velocity, bool retrigger);
        void setNote (int note) noexcept;
        void release();
        void kill();

        void render (float* out, int numSamples, const SubBlock& block);

        bool isActive() const noexcept { return active; }
        bool isGateOn() const noexcept { return active && gate; }
        int getNote() const noexcept { return targetNote; }
        float getLevel() const noexcept { return active ? ampEnv.getLevel() * fade : 0.0f; }
        float getFilterEnvLevel() const noexcept { return filterEnv.getLevel(); }
        float getAmpEnvLevel() const noexcept { return ampEnv.getLevel(); }
        float getPitch() const noexcept { return pitch; }

        bool sustained = false;

    private:
        void begin (bool retrigger);

        double sampleRate = 48000.0;
        Random noise;
        Oscillator osc1, osc2, osc3;
        SubOscillator sub;
        LadderFilter lowPass;
        HalfbandDecimator decimator;
        Envelope filterEnv, ampEnv;
        OtaVca vca;
        CvLag cutoffCv, pitchExpo;

        bool active = false, gate = false, killing = false;
        int targetNote = 36;
        float velocity = 1.0f, ampVel = 1.0f;
        float pitch = 36.0f, target = 36.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        float lastFilt = 0.0f;
        EnvSettings lastFilterEnv, lastAmpEnv;
        bool envTimesPrimed = false;
    };

    /**
        Monophonic bass engine: one voice, last/low note priority, legato vs retrigger,
        glide, a global LFO, pitch bend and mod wheel. Real-time safe: no allocation or
        locks after prepare().
    */
    class SynthEngine
    {
    public:
        SynthEngine() = default;

        void prepare (double sampleRate, int maximumBlockSize);
        void reset();
        void setSettings (const Settings& newSettings);
        const Settings& getSettings() const noexcept { return settings; }

        void process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const HostTime& time);

        void handleMidi (const juce::MidiMessage& message);
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void allSoundOff();

        bool isVoiceActive() const noexcept { return voice.isActive(); }
        const Voice& getVoice() const noexcept { return voice; }
        float getModWheel() const noexcept { return modWheel; }
        int getHeldCount() const noexcept { return stackSize; }
        int getCurrentNote() const noexcept { return currentNote; }

        struct Tap
        {
            std::atomic<int> note { -1 };
            std::atomic<float> level { 0.0f };
            std::atomic<float> lfo { 0.0f };
            std::atomic<float> cutoffHz { 420.0f };
        };
        Tap& getTap() noexcept { return tap; }

    private:
        void renderSubBlock (float* left, float* right, int numSamples);
        void updateLfoRate (const HostTime& time);
        float nextLfo() noexcept;
        void removeFromStack (int note) noexcept;
        int chooseNote() const noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        Voice voice;
        uint64_t order = 0;

        struct Held { int note = 0; uint64_t order = 0; };
        std::array<Held, 32> stack {};
        int stackSize = 0;
        int currentNote = -1;
        bool sustainPedal = false;
        std::array<bool, 128> heldBySustain {};

        float bendTarget = 0.0f, bend = 0.0f, modWheel = 0.0f;

        double lfoPhase = 0.0, lfoIncrement = 0.0;
        float lfoHeld = 0.0f, lfoSmoothed = 0.0f;
        Random lfoRandom;

        SubBlock block;
        bool smoothersPrimed = false;
        float outputGain = 1.0f;

        Tap tap;
    };
}
