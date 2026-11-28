#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrmeter::params
{
    /** State-tree version written with every saved state. 1: 0.1.0. */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename, reorder or reuse (sessions store them).
        Every ID below was added in 0.1.0. None is automatable: a meter has nothing to
        automate, and the audition states must never be left on by automation. */
    namespace id
    {
        // Main page
        inline constexpr const char* target  = "target";
        inline constexpr const char* curve   = "curve";
        inline constexpr const char* listen  = "listen";   // always loads Off
        inline constexpr const char* mode    = "mode";

        // Advanced page
        inline constexpr const char* customLufs   = "custom_lufs";
        inline constexpr const char* customTp     = "custom_tp";
        inline constexpr const char* historySpan  = "history_span";
        inline constexpr const char* slope        = "slope";
        inline constexpr const char* averaging    = "averaging";
        inline constexpr const char* sensitivity  = "sensitivity";
        inline constexpr const char* monoAudition = "mono_audition"; // always loads Off
        inline constexpr const char* listenMatch  = "listen_match";
        inline constexpr const char* freeze       = "freeze";
    }

    juce::StringArray historySpanNames();
    juce::StringArray slopeNames();
    juce::StringArray averagingNames();
    juce::StringArray sensitivityNames();
    juce::StringArray modeNames();
    /** Display tilt in dB/oct for a slope choice. */
    float slopeDbPerOctave (int index) noexcept;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
