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
    /** Section presets: they load only that section's parameters (and switch it on). */
    const std::vector<FactoryPreset>& getReverbPresets();
    const std::vector<FactoryPreset>& getDelayPresets();
    /** Every parameter whose id starts with prefix at its default, then the overrides; prefix + "on" = 1. */
    ValueList resolveSection (juce::AudioProcessorValueTreeState& state, const juce::String& prefix, const ValueList& overrides);

    /** Parameters that describe the singer (the learned range) and survive factory preset loads. */
    bool isSessionOnlyParameter (const juce::String& id);

    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& random);

    juce::File getUserPresetFolder();
    inline constexpr const char* userPresetExtension = ".dvocal";
}
