#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrbass::randomiser
{
    /** A playable random bass patch as one undo step. Never touches Output, Bend Range,
        Note Priority or the LFO sync settings. Returns the number of parameters changed. */
    int apply (juce::AudioProcessorValueTreeState& state, juce::Random& random);
}
