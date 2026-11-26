#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrreverb::presets
{
    /** A factory preset: parameter values (real units, choices by index) over the defaults. */
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    /** Default first, then rooms, chambers, plates, halls and special spaces (all original settings). */
    const std::vector<Factory>& factory();

    /** Parameters a preset never changes (like a mix lock): Mix, Input, Output and Bypass keep
        their current values when a factory or user preset loads, so browsing never jumps the
        balance or the level. */
    bool isKeptOnPresetLoad (const juce::String& parameterId);

    /** Sets every other parameter to its default, then applies the preset (no gestures: host-safe). */
    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch);

    inline const char* fileExtension = ".dreverb";
    juce::File userDirectory();
    juce::Array<juce::File> userPresets();

    bool save (const juce::AudioProcessorValueTreeState& state, const juce::File& file, const juce::String& name);
    /** Loads parameters from a preset file. Returns an error message, empty on success. */
    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file);
}
