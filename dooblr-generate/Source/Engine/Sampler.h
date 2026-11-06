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
        relative to the root key C3 (MIDI 60) plus Tune and Fine. 16 voices; the oldest is stolen.
        Linear interpolation; real-time safe (no allocation, no locks).
    */
    class Sampler
    {
    public:
        static constexpr int numVoices = 16;
        static constexpr int rootNote = 60;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Renders into out (adds nothing first: clears the region it writes). */
        void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Sample* sample,
                      const PlaybackSettings& settings);

        /** Starts a voice (used for MIDI notes and the editor's Play button). */
        void noteOn (int note, float velocity, const Sample* sample, const PlaybackSettings& settings);

        int numActiveVoices() const noexcept;

    private:
        struct Voice
        {
            bool active = false;
            const Sample* sample = nullptr;
            double position = 0.0, increment = 1.0;
            float gain = 1.0f, env = 0.0f, attackStep = 1.0f, decayMul = 1.0f;
            juce::uint64 age = 0;
        };

        void render (Voice& v, juce::AudioBuffer<float>& out, int start, int num);

        std::array<Voice, numVoices> voices {};
        double hostRate = 44100.0;
        juce::uint64 counter = 0;
        juce::SmoothedValue<float> outputGain { 1.0f };
    };
}
