#pragma once

#include <array>

#include <juce_core/juce_core.h>

#include "DSP/PitchTap.h"
#include "DSP/VocalSettings.h"

namespace dooblrvocals
{
    /** A learned vocal register. */
    struct LearnedRange
    {
        bool valid = false;
        float lowHz = 0.0f, highHz = 0.0f;           // robust 5th / 95th percentile of the sung pitch
        float searchLowHz = 0.0f, searchHighHz = 0.0f; // with the margin: what the detector uses
        float voicedSeconds = 0.0f;
        dsp::InputType type = dsp::InputType::tenor;
        juce::String message;
    };

    /**
        Message-thread side of "Learn Range": accumulates the confident, voiced pitch readings
        the audio thread pushes through a lock-free FIFO into a quarter-semitone histogram
        (weighted by the time each reading covers) and turns it into a range from percentiles,
        so breaths, octave errors and stray readings at the edges don't move it.
        Fixed-size storage; nothing here touches the audio thread.
    */
    class RangeLearner
    {
    public:
        static constexpr float minConfidence = 0.6f;
        static constexpr float minVoicedSeconds = 1.5f;
        static constexpr float lowPercentile = 0.05f, highPercentile = 0.95f;
        static constexpr float marginSemitones = 2.0f;
        static constexpr int binsPerSemitone = 4, lowestNote = 24, highestNote = 100;
        static constexpr int numBins = (highestNote - lowestNote) * binsPerSemitone;

        void clear() noexcept;
        void add (const dsp::PitchFrame* frames, int numFrames) noexcept;

        float getVoicedSeconds() const noexcept { return (float) totalSeconds; }
        float getLastVoicedHz() const noexcept { return lastHz; }

        LearnedRange compute() const;

        /** The voice type whose range overlaps a register best (log-frequency overlap). */
        static dsp::InputType typeFor (float lowHz, float highHz) noexcept;
        static juce::String rangeText (float lowHz, float highHz);

    private:
        std::array<double, numBins> histogram {};
        double totalSeconds = 0.0;
        float lastHz = 0.0f;
    };
}
