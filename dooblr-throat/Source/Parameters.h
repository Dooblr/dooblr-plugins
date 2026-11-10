#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrthroat::params
{
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        inline constexpr const char* vowel      = "vowel";
        inline constexpr const char* height     = "height";
        inline constexpr const char* back       = "back";
        inline constexpr const char* nasal      = "nasal";
        inline constexpr const char* female     = "female";
        inline constexpr const char* child      = "child";
        inline constexpr const char* breath     = "breath";
        inline constexpr const char* tension    = "tension";
        inline constexpr const char* brightness = "brightness";
        inline constexpr const char* fry        = "fry";
        inline constexpr const char* roughness  = "roughness";
        inline constexpr const char* pressure   = "pressure";
        inline constexpr const char* voiced     = "voiced";
        inline constexpr const char* vibratoDepth = "vibratoDepth";
        inline constexpr const char* vibratoRate  = "vibratoRate";
        inline constexpr const char* glide        = "glide";
        inline constexpr const char* bendRange    = "bendRange";
        inline constexpr const char* attack       = "attack";
        inline constexpr const char* decay        = "decay";
        inline constexpr const char* sustain      = "sustain";
        inline constexpr const char* release      = "release";
        inline constexpr const char* output       = "output";
    }

    /** Choice 0 is the vowel pad; 1..N_VOWELS are the named vowels. */
    inline const juce::StringArray& vowelNames()
    {
        static const juce::StringArray names {
            "Pad",
            "heed", "hid", "hayed", "head", "had", "patte",
            "hod", "lot", "hawed", "hoed", "hood", "who'd",
            "hud", "sofa", "nurs", "heard"
        };
        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
