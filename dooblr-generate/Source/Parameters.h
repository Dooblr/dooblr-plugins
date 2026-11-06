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
        inline constexpr const char* velocity = "velocity";
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
        inline constexpr const char* currentWav  = "currentWav";
        inline constexpr const char* currentId   = "currentId";
    }

    inline constexpr double minSeconds = 0.25, maxSeconds = 10.0, defaultSeconds = 2.0;
}
