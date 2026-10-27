#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrkick::presets
{
    /** Parameter id -> plain (denormalised) value. */
    using ValueList = std::vector<std::pair<juce::String, float>>;

    struct FactoryPreset
    {
        juce::String name;
        ValueList values; // applied on top of the parameter defaults
    };

    /** Index 0 is "Init" (all defaults), then the six original factory kicks. */
    const std::vector<FactoryPreset>& getFactoryPresets();

    /** Every parameter: the defaults, overridden by the preset's values. */
    ValueList resolve (juce::AudioProcessorValueTreeState& state, const ValueList& overrides);

    /**
        Sets the values. With asGesture, every changed parameter is wrapped in a begin / end
        change gesture so the host and the undo history see one edit.
    */
    void apply (juce::AudioProcessorValueTreeState& state, const ValueList& values, bool asGesture);

    /** A musically constrained random kick (always the full set of sound parameters). */
    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& random);

    /** ~/Music/dooblr-kick/Presets */
    juce::File getUserPresetFolder();
    inline constexpr const char* userPresetExtension = ".dkick";
}
