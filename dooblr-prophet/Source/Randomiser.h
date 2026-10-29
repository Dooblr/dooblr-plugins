#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrprophet::randomiser
{
    /** Parameters Random never touches: output level, voice count and mode, unison settings,
        bend range, aftertouch routing, Osc A tuning (the patch's pitch), and the LFO's host
        sync / division. */
    const juce::StringArray& excludedParameters();

    /**
        Writes a new random patch: every other sound-shaping parameter gets a value from a
        musically usable range (Osc A always in the mix, amp sustain at least 40 %, a closed
        cutoff always gets envelope, resonance mostly below 60 %, Osc B tuned to a musical
        interval). All changes are one gesture, so one undo step. Message thread only.
        Returns the number of parameters that changed.
    */
    int apply (juce::AudioProcessorValueTreeState& state, juce::Random& random);
}
