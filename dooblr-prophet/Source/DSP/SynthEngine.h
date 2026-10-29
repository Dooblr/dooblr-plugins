#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Primitives.h"

namespace dooblrprophet::dsp
{
    inline constexpr int maxVoices = 10;
    inline constexpr int subBlockSize = 32;

    enum class VoiceMode { poly = 0, unison, mono };
    enum class LfoWave { triangle = 0, saw, reverseSaw, square, sampleHold };

    struct OscSettings
    {
        bool switched = false;           // false: continuous shape knob; true: saw / tri / pulse switches
        int coarse = 0;                  // semitones
        float fine = 0.0f;               // cents
        float shape = 0.5f, pulseWidth = 0.5f;
        bool saw = true, tri = false, pulse = false;
    };

    struct EnvSettings
    {
        float attack = 0.005f, decay = 0.5f, sustain = 0.7f, release = 0.3f, velocity = 0.0f;
    };

    /** Every sound parameter, read once per block from the processor's parameters. */
    struct Settings
    {
        int voices = 6;
        VoiceMode mode = VoiceMode::poly;
        int unisonVoices = 4;
        float unisonDetune = 0.25f;      // 0..1 (up to +-30 cents at the outer voices)
        float glide = 0.0f;              // seconds
        float vintage = 0.3f;            // 0..1
        int bendRange = 2;               // semitones
        float atFilter = 0.0f;           // -1..1
        float atLfo = 0.0f;              // 0..1
        float outputDb = 0.0f;

        OscSettings osc1, osc2;
        bool sync = false, osc2LowFreq = false, osc2KeyTrack = true;
        float mixOsc1 = 0.8f, mixOsc2 = 0.6f, mixSub = 0.0f, mixNoise = 0.0f;

        float lpfCutoff = 4000.0f, lpfReso = 0.15f, lpfDrive = 0.2f, lpfEnvAmt = 0.35f, lpfKeyTrack = 0.5f;
        float hpfCutoff = 20.0f, hpfReso = 0.0f, hpfEnvAmt = 0.0f;
        int lpfModel = 0;                // 0 CEM3320 style ("Rev3"), 1 SSM2040 style ("Rev2" / P6)

        EnvSettings filterEnv { 0.005f, 0.6f, 0.3f, 0.5f, 0.3f };
        EnvSettings ampEnv { 0.003f, 0.8f, 0.8f, 0.35f, 0.3f };
        bool filterEnvSync = false;      // filter envelope runs on the amp envelope's ADSR

        /** The ADSR the filter envelope actually uses: its own, or the amp's when synced
            (its velocity amount is always its own). */
        EnvSettings effectiveFilterEnv() const noexcept
        {
            if (! filterEnvSync)
                return filterEnv;
            auto e = ampEnv;
            e.velocity = filterEnv.velocity;
            return e;
        }

        float pmodFiltEnv = 0.0f, pmodOsc2 = 0.0f;
        bool pmodFreq1 = true, pmodShape1 = false, pmodFilter = false;

        LfoWave lfoWave = LfoWave::triangle;
        float lfoRate = 5.0f;
        bool lfoSync = false;
        double lfoSyncQuarters = 1.0;
        float lfoInitial = 0.0f, lfoWheel = 0.25f, lfoNoiseMix = 0.0f;
        bool lfoFreq1 = true, lfoFreq2 = true, lfoShape1 = false, lfoShape2 = false, lfoLpf = false, lfoHpf = false, lfoAmp = false;
    };

    struct HostTime
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool hasPpq = false, playing = false;
    };

    /** Control values shared by all voices for one sub-block (<= subBlockSize samples). */
    struct SubBlock
    {
        const Settings* settings = nullptr;
        double sampleRate = 48000.0;
        float lfo[subBlockSize] {};      // the LFO / noise mod signal, already scaled by its amount
        float lfoAmount = 0.0f;
        float bend = 0.0f;               // semitones
        float aftertouch = 0.0f;         // 0..1
        float warmup = 1.0f;             // exp (-t / warmupSeconds) since prepare
        // Smoothed (zipper-free) controls
        float lpfOctaves = 12.0f, lpfReso = 0.0f, lpfDrive = 1.0f, hpfOctaves = 4.3f, hpfReso = 0.0f;
        float mix[4] {};
        float shape1 = 0.5f, shape2 = 0.5f, pw1 = 0.5f, pw2 = 0.5f;
    };

    class Voice
    {
    public:
        void prepare (double sampleRate, uint32_t seed);
        void reset();

        /** Starts a note. If the voice is still sounding the old note fades out over ~3 ms first. */
        void start (int note, float velocity, float detuneCents, float gain, uint64_t order);
        /** Legato: glides (or jumps) to a new pitch without retriggering the envelopes. */
        void setNote (int note) noexcept { targetNote = note; target = (float) note; }
        void release (uint64_t order);
        /** Fades out quickly and goes idle (voice count reduced, mode change, all sound off). */
        void kill();

        void render (float* out, int numSamples, const SubBlock& block);

        bool isActive() const noexcept { return active; }
        bool isGateOn() const noexcept { return active && gate; }
        bool isStealing() const noexcept { return pendingStart; }
        int getNote() const noexcept { return pendingStart ? pendingNote : targetNote; }
        uint64_t getStartOrder() const noexcept { return startOrder; }
        uint64_t getReleaseOrder() const noexcept { return releaseOrder; }
        float getLevel() const noexcept { return active ? ampEnv.getLevel() * fade : 0.0f; }
        /** Number of oscillator 1 cycles (wraps / sync resets) rendered so far, for tests. */
        uint64_t getOsc1Cycles() const noexcept { return osc1Cycles; }
        /** Raw envelope levels (before velocity) after the last rendered sample, for tests. */
        float getFilterEnvLevel() const noexcept { return filterEnv.getLevel(); }
        float getAmpEnvLevel() const noexcept { return ampEnv.getLevel(); }

        bool sustained = false;          // released while the sustain pedal was down

    private:
        void begin();
        void updateEnvelopes (const Settings& s);

        double sampleRate = 48000.0;
        Random random;
        Oscillator osc1, osc2;
        SubOscillator sub;
        LadderFilter lowPass;
        HalfbandDecimator decimator4, decimator2;   // 4x -> 2x, 2x -> 1x
        int oversample = 0;
        HighPassFilter highPass;
        Envelope filterEnv, ampEnv;

        bool active = false, gate = false, pendingStart = false, killing = false;
        int targetNote = 60, pendingNote = 60;
        float velocity = 1.0f, pendingVelocity = 1.0f, detune = 0.0f, pendingDetune = 0.0f, gain = 1.0f, pendingGain = 1.0f;
        float pitch = 60.0f, target = 60.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        uint64_t startOrder = 0, releaseOrder = 0;

        // Vintage: fixed per-voice offsets (-1..1) and a slow random drift.
        float slopPitch1 = 0.0f, slopPitch2 = 0.0f, slopCutoff = 0.0f, slopEnv = 0.0f;
        float drift1 = 0.0f, drift2 = 0.0f, driftTarget1 = 0.0f, driftTarget2 = 0.0f;
        int driftCountdown = 0;
        float slopScale = 0.0f, warm1 = 0.0f, warm2 = 0.0f, warmCutoff = 0.0f, skew1 = 0.0f, skew2 = 0.0f;
        float stageSlop[4] {};
        float lastSkewVintage = -1.0f;
        // Cutoff / resonance CV feedthrough (high-passed)
        float feedState = 0.0f, lastCv = 0.0f;
        bool feedPrimed = false;
        uint64_t osc1Cycles = 0;
        float lastEnvVintage = -1.0f;
        EnvSettings lastFilterEnv, lastAmpEnv;
    };

    /**
        The polyphonic engine: up to 10 voices, poly / unison / mono allocation, a global LFO
        with wheel and aftertouch amount, pitch bend, sustain pedal. Real-time safe: no
        allocation or locks after prepare().
    */
    class SynthEngine
    {
    public:
        SynthEngine();

        void prepare (double sampleRate, int maximumBlockSize);
        void reset();
        void setSettings (const Settings& newSettings);
        const Settings& getSettings() const noexcept { return settings; }

        /** Renders numSamples into left / right (overwritten), applying the MIDI sample-accurately. */
        void process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const HostTime& time);

        void handleMidi (const juce::MidiMessage& message);
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void allSoundOff();

        int getNumActiveVoices() const noexcept;
        const Voice& getVoice (int index) const noexcept { return voices[(size_t) index]; }
        float getModWheel() const noexcept { return modWheel; }

        /** For the editor's voice display: per-voice note (-1 idle) and level, written per block. */
        struct VoiceTap
        {
            std::array<std::atomic<int>, maxVoices> notes;
            std::array<std::atomic<float>, maxVoices> levels;
            std::atomic<float> lfo { 0.0f };
            VoiceTap() { for (auto& n : notes) n = -1; for (auto& l : levels) l = 0.0f; }
        };
        VoiceTap& getVoiceTap() noexcept { return tap; }

    private:
        void renderSubBlock (float* left, float* right, int numSamples);
        int voicesInUse() const noexcept;
        void releaseVoice (Voice& voice);
        void startUnison (int note, float velocity);
        void updateLfoRate (const HostTime& time);
        float nextLfo() noexcept;
        void removeFromStack (int note) noexcept;

        Settings settings;
        double sampleRate = 48000.0;
        std::array<Voice, maxVoices> voices;
        uint64_t order = 0;

        // Unison / mono note stack (last-note priority)
        std::array<int, 32> stack {};
        int stackSize = 0;
        bool sustainPedal = false, monoSustained = false;
        std::array<bool, 128> heldBySustain {};

        // Controllers
        float bendTarget = 0.0f, bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
        std::array<float, 128> polyPressure {};

        // LFO
        double lfoPhase = 0.0, lfoIncrement = 0.0;
        float lfoHeld = 0.0f, lfoSmoothed = 0.0f;
        Random lfoRandom, noise;

        // Smoothed controls
        SubBlock block;
        bool smoothersPrimed = false;
        double secondsSincePrepare = 0.0;
        float outputGain = 1.0f;

        VoiceMode lastMode = VoiceMode::poly;
        VoiceTap tap;
    };
}
