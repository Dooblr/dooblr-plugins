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

    /** A musically constrained random kick: the Click and Body layers (every sound parameter,
        including Synth / Sample source and sample choice; not their key tracking or solo), the
        pitch and amp envelopes, Sub, Tail, Noise and the Drive stage. EQ, Dynamics, Limiter,
        the Envelope page, the Key and the output gain are left alone. Every value is inside its
        parameter's range.

        On / off: each of the five layers is switched by its own draw (Click and Body 80 %, Sub
        60 %, Tail and Noise 30 %); a soloed layer stays on; if all five come up off, one of them
        (at random) is switched on, so at least one layer is always on. A layer heard on its own
        gets a level of at least soloLevelFloorDb. */
    inline constexpr float soloLevelFloorDb = -9.0f;

    ValueList randomise (juce::AudioProcessorValueTreeState& state, juce::Random& random);

    /** ~/Music/dooblr-kick/Presets */
    juce::File getUserPresetFolder();
    inline constexpr const char* userPresetExtension = ".dkick";
}
