#pragma once

#include <juce_core/juce_core.h>

namespace dooblrreverb::dsp
{
    /**
        The room models the Space knob sweeps through. Each anchor sits at a Space position;
        between anchors every value is interpolated, so turning Space glides from one model to
        the next with no switching (and no clicks). The decay time itself comes from
        params::spaceToSeconds (0.2 s .. 10 s); a model adds the room's size and its early
        reflections, diffusion and frequency balance.
    */
    struct RoomModel
    {
        float spacePercent = 0.0f;
        float sizeMs = 20.0f;      // mean late-reverb delay line length
        float erSpanMs = 30.0f;    // time span of the early reflections
        float erDensity = 0.8f;    // 0..1: how many of the 24 reflection taps are used
        float erLevel = 0.8f;      // early reflections level (before Distance / Character)
        float diffusion = 0.65f;   // input diffuser feedback
        float bassLog2 = 0.2f;     // longer decay below ~300 Hz (log2 of the multiplier)
        float airLog2 = -0.2f;     // shorter decay above ~8 kHz (air absorption)
    };

    /** The anchors, sorted by Space. */
    const juce::Array<RoomModel>& roomAnchors();
    const juce::StringArray& roomNames();

    /** The model at a Space position (interpolated between the anchors). */
    RoomModel roomAt (float spacePercent) noexcept;
    /** The name of the nearest anchor (shown under the Space knob). */
    juce::String roomNameAt (float spacePercent);
}
