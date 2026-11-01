#pragma once

#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/FeedbackEngine.h"

namespace dooblrfeedback::presets
{
    /** A factory preset: parameter values (real units, choices by index) over the defaults.
        Factory presets are settings only: they leave the room's notches and notes alone. */
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    /** Default first, then the factory presets (all original settings). */
    const std::vector<Factory>& factory();

    /** Sets every parameter to its default, then applies the preset (no gestures: host-safe). */
    void apply (juce::AudioProcessorValueTreeState& state, const Factory& patch);

    inline const char* fileExtension = ".dfeedback";
    juce::File userDirectory();
    juce::Array<juce::File> userPresets();

    /** The notch bank as a ValueTree ("NOTCHES" with one "NOTCH" per active notch). */
    juce::ValueTree notchesToTree (const dsp::NotchArray& notches);
    dsp::NotchArray notchesFromTree (const juce::ValueTree& tree);

    /** A room preset: every parameter, the notes and the notch bank. */
    bool save (const juce::AudioProcessorValueTreeState& state, const juce::String& notes, const dsp::NotchArray& notches,
               const juce::File& file, const juce::String& name);
    /** Loads parameters (and returns notes / notches) from a preset file. Returns an error
        message, empty on success. */
    juce::String load (juce::AudioProcessorValueTreeState& state, const juce::File& file, juce::String& notes, dsp::NotchArray& notches);
    /** Renames a user preset file (and the name stored in it). Returns the new file, or an
        invalid File with `error` set. */
    juce::File rename (const juce::File& file, const juce::String& newName, juce::String& error);
}
