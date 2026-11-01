#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrfeedback::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). 1: 0.1.0. */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Detection
        inline constexpr const char* detect      = "detect";      // Auto: find and pull down feedback
        inline constexpr const char* ringOut     = "ringOut";     // Ring-out: new notches are fixed
        inline constexpr const char* mode        = "mode";        // Safe | Aggressive
        inline constexpr const char* sensitivity = "sensitivity";
        inline constexpr const char* sidechain   = "sidechain";   // detect on the sidechain (reference mic)
        inline constexpr const char* rangeLow    = "rangeLow";
        inline constexpr const char* rangeHigh   = "rangeHigh";

        // Notches
        inline constexpr const char* maxCut      = "maxCut";
        inline constexpr const char* notchQ      = "notchQ";
        inline constexpr const char* liveNotches = "liveNotches"; // Float | Hold
        inline constexpr const char* release     = "release";

        // Output
        inline constexpr const char* output      = "output";
        inline constexpr const char* bypass      = "bypass";
    }

    juce::StringArray modeNames();        // Safe, Aggressive
    juce::StringArray liveNotchNames();   // Float, Hold

    /** The parameter layout, in the order hosts list it. */
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Value text with a fixed number of decimals: never exponent notation, never "-0". */
    juce::String formatValue (double value, int decimals);
    /** "86 Hz", "1.25 kHz", "12.5 kHz". */
    juce::String formatFrequency (double hz);
    /** Nearest equal-tempered note (A4 = 440 Hz), e.g. "D#6"; cents is set to the offset (-50..+50). */
    juce::String noteName (double hz, int* cents = nullptr);
    /** "D#6 +12c" (or "D#6" when within 2 cents). */
    juce::String noteWithCents (double hz);
}
