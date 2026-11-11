#pragma once
// Monophonic / legato instrument wrapper around HumanVoice.

#include "HumanVoice.h"
#include "Vowels.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace dooblrthroat::dsp
{
    struct ThroatSettings
    {
        BodyMix body {};
        float vowelHeight = 0.5f; // 0 close .. 1 open
        float vowelBack = 0.5f;   // 0 front .. 1 back
        int vowelIndex = 13;      // -1 = use the pad
        float breathiness = 0.15f, tension = 0.5f, brightness = 0.25f;
        float fry = 0.0f, roughness = 0.1f, pressure = 8.0f;
        float vibratoDepth = 0.0f, vibratoRate = 6.0f, glide = 0.06f;
        float nasal = -1.0f; // < 0 keeps the vowel velum
        float attack = 0.03f, decay = 0.08f, sustain = 0.85f, release = 0.12f;
        float outputDb = -6.0f;
        bool voiced = true, tipFree = true;
    };

    class ThroatEngine
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset();

        /** Apply parameter values. Never allocates or frees a voice — safe on the audio thread. */
        void setSettings (const ThroatSettings& s);
        const ThroatSettings& settings() const { return settings_; }

        /** Build or replace the airway. Message thread / tests only.
            The previous voice is published with an atomic pointer swap and is freed
            only after the audio callback is no longer using it. */
        /** Build or replace the airway from a settings snapshot. Message thread / tests only.
            Does not write settings_ (that stays audio-thread-only). The previous voice
            is freed only after the audio callback is no longer using it. */
        void rebuildVoiceOffAudioThread (const ThroatSettings& incoming, bool force = false);

        float appliedFemale() const noexcept { return appliedFemale_.load (std::memory_order_acquire); }
        float appliedChild() const noexcept { return appliedChild_.load (std::memory_order_acquire); }

        void noteOn (int midiNote, float velocity);
        void noteOff (int midiNote);
        void allNotesOff();
        void pitchBend (float semis);

        void process (float* left, float* right, int n);

        /** Wrap a processBlock (or a test audio loop) so a concurrent rebuild cannot
            free the live voice until the callback returns. */
        void beginAudioCallback() noexcept;
        void endAudioCallback() noexcept;

        float lastPeak() const { return lastPeak_; }
        bool isSounding() const { return gate_ > 1e-4f || ! held_.empty(); }
        const HumanVoice* voice() const { return live_.load (std::memory_order_acquire); }

    private:
        void applySource (HumanVoice& voice, const ThroatSettings& s, bool useLiveNote = true);
        void applyVowel (HumanVoice& voice, const ThroatSettings& s);
        void publishVoice (std::unique_ptr<HumanVoice> next);
        static void sanitizeSettings (ThroatSettings& s);
        HumanVoice* live() const noexcept { return live_.load (std::memory_order_acquire); }
        void applyToLive();

        double sampleRate_ = 48000.0;
        ThroatSettings settings_;
        std::atomic<float> appliedFemale_ { 0.0f };
        std::atomic<float> appliedChild_ { 0.0f };
        std::unique_ptr<HumanVoice> owned_;
        std::atomic<HumanVoice*> live_ { nullptr };
        std::atomic<uint32_t> audioBusy_ { 0 };
        std::mutex swapMutex_;
        std::vector<int> held_;
        int currentNote_ = -1;
        float bendSemis_ = 0.0f, vel_ = 1.0f;
        float gate_ = 0.0f, env_ = 0.0f, lastPeak_ = 0.0f;
        enum class Stage { idle, attack, decay, sustain, release } stage_ = Stage::idle;
        std::vector<float> tmp_;
    };

    struct AudioCallbackScope
    {
        ThroatEngine& engine;
        explicit AudioCallbackScope (ThroatEngine& e) noexcept : engine (e) { engine.beginAudioCallback(); }
        ~AudioCallbackScope() noexcept { engine.endAudioCallback(); }
        AudioCallbackScope (const AudioCallbackScope&) = delete;
        AudioCallbackScope& operator= (const AudioCallbackScope&) = delete;
    };
}
