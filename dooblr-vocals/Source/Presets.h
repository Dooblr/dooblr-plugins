#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrvocals::presets
{
    using ValueList = std::vector<std::pair<juce::String, float>>;

    struct FactoryPreset
    {
        juce::String name;
        ValueList values;
    };

    const std::vector<FactoryPreset>& getFactoryPresets();
    ValueList resolve (juce::AudioProcessorValueTreeState& state, const ValueList& overrides);
    void apply (juce::AudioProcessorValueTreeState& state, const ValueList& values, bool asGesture);
    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& random);

    juce::File getUserPresetFolder();
    inline constexpr const char* userPresetExtension = ".dvocal";
}
