#pragma once
// Monophonic / legato instrument wrapper around HumanVoice.

#include "HumanVoice.h"
#include "Vowels.h"
#include <array>
#include <cstdint>
#include <memory>
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
        void setSettings (const ThroatSettings& s); // may allocate if the body changes (call off the audio thread)
        const ThroatSettings& settings() const { return settings_; }

        void noteOn (int midiNote, float velocity);
        void noteOff (int midiNote);
        void allNotesOff();
        void pitchBend (float semis);

        void process (float* left, float* right, int n);

        float lastPeak() const { return lastPeak_; }
        bool isSounding() const { return gate_ > 1e-4f || ! held_.empty(); }
        const HumanVoice* voice() const { return voice_.get(); }

    private:
        void applySource();
        void applyVowel();
        void rebuildVoice();

        double sampleRate_ = 48000.0;
        ThroatSettings settings_, appliedBody_;
        std::unique_ptr<HumanVoice> voice_;
        std::vector<int> held_;
        int currentNote_ = -1;
        float bendSemis_ = 0.0f, vel_ = 1.0f;
        float gate_ = 0.0f, env_ = 0.0f, lastPeak_ = 0.0f;
        enum class Stage { idle, attack, decay, sustain, release } stage_ = Stage::idle;
        bool bodyDirty_ = false;
        std::vector<float> tmp_;
    };
}
