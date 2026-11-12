#pragma once
// Monophonic / legato instrument wrapper around HumanVoice.

#include "HumanVoice.h"
#include "Phonetics/Phrase.h"
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
        int speechMode = 0;     // 0 Sing, 1 Speak
        float speechRate = 1.0f;
        float effort = 0.5f;
        float formantTune = 0.0f;
        int clarityMode = 6; // 0 waveguide, 1 A, 2 B, 3 C, 4 K, 5 E espeak, 6 P piper (default), 7 M
    };

    class ThroatEngine
    {
    public:
        ThroatEngine() = default;
        ~ThroatEngine();

        void prepare (double sampleRate, int maxBlock);
        void reset();

        /** Apply parameter values. Never allocates or frees a voice — safe on the audio thread. */
        void setSettings (const ThroatSettings& s);
        const ThroatSettings& settings() const { return settings_; }

        /** Build or replace the airway from a settings snapshot. Message thread / tests only.
            Does not write settings_ (that stays audio-thread-only). The previous voice
            is freed only after the audio callback is no longer using it. */
        void rebuildVoiceOffAudioThread (const ThroatSettings& incoming, bool force = false);

        float appliedFemale() const noexcept { return appliedFemale_.load (std::memory_order_acquire); }
        float appliedChild() const noexcept { return appliedChild_.load (std::memory_order_acquire); }

        /** Publish a parsed phrase (or empty to restore vowel-pad behaviour).
            Message thread / tests only. Freed only after the audio callback is done. */
        void publishPhrase (std::unique_ptr<phonetics::PublishedPhrase> next);

        /** Reset syllable index / stop a spoken phrase. Safe on any thread. */
        void requestRestart() noexcept { restartFlag_.store (1u, std::memory_order_release); }

        const phonetics::PublishedPhrase* livePhrase() const noexcept
        {
            return livePhrase_.load (std::memory_order_acquire);
        }

        int syllableIndex() const noexcept { return sylIndex_.load (std::memory_order_acquire); }
        int syllableCount() const noexcept { return sylCount_.load (std::memory_order_acquire); }
        void copyReadout (char* buf, size_t cap) const noexcept;

        void noteOn (int midiNote, float velocity);
        void noteOff (int midiNote);
        void allNotesOff();
        void pitchBend (float semis);
        void setOffline (bool offline) noexcept { offline_.store (offline, std::memory_order_release); proven_.setOffline (offline); }
        ProvenEngine& proven() noexcept { return proven_; }
        void prepareProven (bool wait);
        bool waitProvenReady() { return proven_.waitReady(); }

        void process (float* left, float* right, int n);

        void beginAudioCallback() noexcept;
        void endAudioCallback() noexcept;

        float lastPeak() const { return lastPeak_; }
        bool isSounding() const {
            if (gate_ > 1e-4f || ! held_.empty() || talk_ != Talk::Off) return true;
            return settings_.clarityMode >= 5 && proven_.engaged() && ! proven_.finished();
        }
        const HumanVoice* voice() const { return live_.load (std::memory_order_acquire); }
        bool phraseActive() const noexcept { return talk_ != Talk::Off; }

    private:
        enum class Talk { Off, Speak, Hold, Coda, CodaThenNext };

        void applySource (HumanVoice& voice, const ThroatSettings& s, bool useLiveNote = true);
        void applyVowel (HumanVoice& voice, const ThroatSettings& s);
        void publishVoice (std::unique_ptr<HumanVoice> next);
        static void sanitizeSettings (ThroatSettings& s);
        HumanVoice* live() const noexcept { return live_.load (std::memory_order_acquire); }
        void applyToLive();
        void handleRestart();
        void startSpeak (HumanVoice& v, const phonetics::PublishedPhrase& ph, int midi);
        void startHold (HumanVoice& v, const phonetics::PublishedPhrase& ph, int syl, int midi);
        void startCoda (HumanVoice& v, const phonetics::PublishedPhrase& ph, int syl, int midi);
        void clearTalk (HumanVoice* v);
        void updateReadout (const phonetics::PublishedPhrase* ph);
        void tickPhrase (HumanVoice& v, const phonetics::PublishedPhrase* ph);
        double currentHz() const;

        double sampleRate_ = 48000.0;
        ThroatSettings settings_;
        std::atomic<float> appliedFemale_ { 0.0f };
        std::atomic<float> appliedChild_ { 0.0f };
        std::unique_ptr<HumanVoice> owned_;
        std::atomic<HumanVoice*> live_ { nullptr };
        std::unique_ptr<phonetics::PublishedPhrase> ownedPhrase_;
        std::atomic<const phonetics::PublishedPhrase*> livePhrase_ { nullptr };
        std::atomic<uint32_t> audioBusy_ { 0 };
        std::atomic<uint32_t> restartFlag_ { 0 };
        std::mutex swapMutex_;
        std::vector<int> held_;
        int currentNote_ = -1;
        float bendSemis_ = 0.0f, vel_ = 1.0f;
        float gate_ = 0.0f, env_ = 0.0f, lastPeak_ = 0.0f;
        enum class Stage { idle, attack, decay, sustain, release } stage_ = Stage::idle;
        std::vector<float> tmp_;

        Talk talk_ = Talk::Off;
        int syl_ = -1, nextSyl_ = 0, codaSyl_ = 0;
        std::atomic<int> sylIndex_ { 0 };
        std::atomic<int> sylCount_ { 0 };
        std::atomic<uint32_t> readoutSeq_ { 0 };
        char readout_[64] {};
        ProvenEngine proven_;
        std::atomic<bool> offline_ { false };
        void attachProven (HumanVoice& v) noexcept { v.proven = &proven_; }
        static ProvenKind provenKind (int mode) noexcept;
        void startProven (HumanVoice& v, int midi, bool syllable);
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
