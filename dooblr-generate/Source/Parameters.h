#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** The automatable parameters (all show up in Ableton's device parameter list) and the
    non-automatable generation settings kept in the saved state. */
namespace dooblrgenerate::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    namespace id
    {
        inline constexpr const char* output   = "output";
        inline constexpr const char* tune     = "tune";
        inline constexpr const char* fine     = "fine";
        inline constexpr const char* start    = "start";
        inline constexpr const char* attack   = "attack";
        inline constexpr const char* decay    = "decay";
        inline constexpr const char* velocity         = "velocity";
        inline constexpr const char* variationAmount  = "variationAmount"; // added in 0.2.0; no editor control from 0.3.0
        inline constexpr const char* sync             = "sync";            // added in 0.3.0
        inline constexpr const char* tempo            = "tempo";           // added in 0.3.0
    }

    /** Decay at its maximum means no decay (the sample plays to its end at full level). */
    inline constexpr float decayOffMs = 20000.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Every automatable parameter id, in display order. */
    juce::StringArray allIds();

    /** Non-automatable generation settings (stored in the "generate" child of the state). */
    namespace setting
    {
        inline constexpr const char* tree        = "generate";
        inline constexpr const char* prompt      = "prompt";
        inline constexpr const char* seed        = "seed";
        inline constexpr const char* seconds     = "seconds";
        inline constexpr const char* model       = "model";
        inline constexpr const char* currentWav     = "currentWav";
        inline constexpr const char* currentId      = "currentId";
        inline constexpr const char* sourceWav      = "sourceWav";      // added in 0.2.0; ignored from 0.3.0
        inline constexpr const char* sourceName     = "sourceName";     // ignored from 0.3.0
        inline constexpr const char* sourceTrimmed  = "sourceTrimmed";  // ignored from 0.3.0
        inline constexpr const char* lengthUnit     = "lengthUnit";     // added in 0.3.0: BeatSync::LengthUnit
    }

    inline constexpr double minSeconds = 0.25, maxSeconds = 30.0, defaultSeconds = 2.0;
    inline constexpr float minTempoBpm = 40.0f, maxTempoBpm = 240.0f, defaultTempoBpm = 120.0f;
}
