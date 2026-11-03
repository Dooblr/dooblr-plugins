#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrbass::presets
{
    /** A factory patch: parameter values (real units, choices/ints by index) over the defaults. */
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    /** Init first, then the factory patches (all original sounds). */
    const std::vector<Factory>& factory();

    /** Sets every parameter to its default, then applies the patch (no gestures: host-safe). */
    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch);

    inline const char* fileExtension = ".dbass";
    juce::File userDirectory();
    juce::Array<juce::File> userPresets();

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name);
    /** Loads parameters from a preset file. Returns an error message, empty on success. */
    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file);
}
