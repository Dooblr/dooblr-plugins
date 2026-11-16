#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrrhythm::presets
{
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    const std::vector<Factory>& factory();
    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch);

    inline const char* fileExtension = ".drhythm";
    juce::File userDirectory();
    juce::Array<juce::File> userPresets();

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name);
    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file);
}
