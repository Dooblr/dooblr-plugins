#pragma once

#include "Sample.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace dooblrgenerate::engine
{
    /** Playback settings, read from the parameters once per block. */
    struct PlaybackSettings
    {
        float outputDb = 0.0f;
        int tuneSemitones = 0;
        float fineCents = 0.0f;
        float startPercent = 0.0f;
        float attackMs = 0.0f;
        float decayMs = 20000.0f;   // >= decayOffMs: no decay
        float velocityPercent = 100.0f;
    };

    /**
        A one-shot sampler: every MIDI note-on starts a voice that plays the current sample from the
        Start point to its end (note-offs are ignored, like a drum sampler). Pitch follows the note
        relative to the root key C3 (MIDI 60) plus Tune and Fine. Monophonic: a new trigger (MIDI,
        Play, history, source preview) fades out the sounding voice over stealFadeMs and starts the
        new one, so at most one voice is playing. Linear interpolation; real-time safe (no allocation,
        no locks).
    */
    class Sampler
    {
    public:
        static constexpr int maxPolyphony = 1;
        static constexpr float stealFadeMs = 5.0f;
        static constexpr int rootNote = 60;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Click-free stop: every sounding voice fades out over stealFadeMs. */
        void stopAll();

        /** Renders into out (adds nothing first: clears the region it writes).
            extraKeep: a second sample whose voices must stay alive (the dropped source, while previewing). */
        void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Sample* sample,
                      const PlaybackSettings& settings, const Sample* extraKeep = nullptr);

        /** Starts a voice (used for MIDI notes and the editor's Play button).
            If another voice is playing, that voice is faded out over stealFadeMs. */
        void noteOn (int note, float velocity, const Sample* sample, const PlaybackSettings& settings);

        /** Voices that are playing (not merely fading out). 0 or 1. */
        int numActiveVoices() const noexcept;
        /** True while any voice still produces audio, including a steal / Stop All fade. */
        bool isSounding() const noexcept;

    private:
        static constexpr int numVoiceSlots = 2; // 1 playing + 1 releasing

        struct Voice
        {
            bool active = false, releasing = false;
            const Sample* sample = nullptr;
            double position = 0.0, increment = 1.0;
            float gain = 1.0f, env = 0.0f, attackStep = 1.0f, decayMul = 1.0f;
            float releaseGain = 1.0f, releaseStep = 0.0f;
            juce::uint64 age = 0;
        };

        void startRelease (Voice&) noexcept;
        void render (Voice& v, juce::AudioBuffer<float>& out, int start, int num);

        std::array<Voice, numVoiceSlots> voices {};
        double hostRate = 44100.0;
        juce::uint64 counter = 0;
        juce::SmoothedValue<float> outputGain { 1.0f };
    };
}
