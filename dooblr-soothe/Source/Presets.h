#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrsoothe::presets
{
    /** A factory preset: parameter values (real units, choices/ints by index) over the defaults. */
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    /** Default first, then the factory presets (all original settings). */
    const std::vector<Factory>& factory();

    /** Sets every parameter to its default, then applies the preset (no gestures: host-safe). */
    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch);

    inline const char* fileExtension = ".dsoothe";
    juce::File userDirectory();
    juce::Array<juce::File> userPresets();

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name);
    /** Loads parameters from a preset file. Returns an error message, empty on success. */
    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file);
}
