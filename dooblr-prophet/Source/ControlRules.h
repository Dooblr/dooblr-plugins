#pragma once

#include <functional>

#include <juce_core/juce_core.h>

#include "DSP/SynthEngine.h"

namespace dooblrprophet::rules
{
    /** A parameter's current plain value, by parameter ID. */
    using ValueOf = std::function<float (const char* parameterId)>;

    /**
        The controls the current settings ignore, shown dimmed (never disabled): parameter IDs plus
        the non-parameter controls' IDs (layout::voicePresetsId, echoPresetPickerId,
        reverbPresetPickerId). Shared by the editor and the remote link so both dim the same
        controls (0.3.0).
    */
    juce::StringArray dimmedControls (const ValueOf& valueOf);

    /** A page's status line (empty when the page has none). */
    juce::String pageStatus (int page, const dsp::Settings& settings, int activeVoices);
}
