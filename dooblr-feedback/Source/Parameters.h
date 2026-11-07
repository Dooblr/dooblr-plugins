#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrfeedback::params
{
    /** State-tree version written with every saved state (bump when a migration is needed).
        1: 0.1.0. 2: 0.1.1 (Range high default 16 -> 20 kHz, Max cut gains an infinite top, Safety). */
    inline constexpr int stateVersion = 2;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        IDs up to bypass were added in 0.1.0; the safety IDs in 0.1.1; recoverSpeed in 0.1.2;
        bellQ with the parametric bell; hpf / hpfFreq in 0.1.3. liveNotches keeps its values:
        0 releases, 1 holds. The displayed name is Hold (Off / On); sessions and automation
        are unchanged. */
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
        inline constexpr const char* liveNotches = "liveNotches"; // Hold: Off (0, release) | On (1, was Hold)
        inline constexpr const char* release     = "release";     // Recover after (s of quiet)

        // Output
        inline constexpr const char* output      = "output";
        inline constexpr const char* bypass      = "bypass";

        // Safety duck (0.1.1): the last resort when the notches cannot hold a howl
        inline constexpr const char* safety          = "safety";          // on / off
        inline constexpr const char* safetyTrigger   = "safetyTrigger";   // Feedback | Always
        inline constexpr const char* safetyThreshold = "safetyThreshold"; // peak dBFS after the notches
        inline constexpr const char* safetyDepth     = "safetyDepth";     // dB of reduction; top = mute
        inline constexpr const char* safetyRelease   = "safetyRelease";   // s

        // Auto-recover (0.1.2)
        inline constexpr const char* recoverSpeed    = "recoverSpeed";    // Slow | Normal | Fast

        // Parametric bell in front of each notch
        inline constexpr const char* bellQ          = "bellQ";

        // High-pass (0.1.3): a rumble cut in front of the notches. Default off, so 0.1.2
        // sessions stay bit-identical. IDs appended; no stateVersion bump.
        inline constexpr const char* hpf            = "hpf";
        inline constexpr const char* hpfFreq        = "hpfFreq";
    }

    /** Max cut: 3 - 30 dB, and the top of the knob (maxCutInfinite) is a full-depth notch. */
    inline constexpr float maxCutFinite = 30.0f, maxCutInfinite = 31.0f;
    /** Safety depth: 3 - 48 dB, and the top of the knob (safetyDepthMute) mutes. */
    inline constexpr float safetyDepthFinite = 48.0f, safetyDepthMute = 49.0f;
    /** The 0.1.0 default for Range high: a 0.1.0 session that still has it is moved to 20 kHz. */
    inline constexpr float legacyRangeHigh = 16000.0f, defaultRangeHigh = 20000.0f;
    /** High-pass (0.1.3): 20-400 Hz, log, default 80 Hz. Off by default. */
    inline constexpr float hpfMinHz = 20.0f, hpfMaxHz = 400.0f, hpfDefaultHz = 80.0f;

    bool isInfiniteCut (float maxCutValue);
    bool isMuteDepth (float safetyDepthValue);
    /** The engine's cut for a Max cut parameter value (the infinite top -> FeedbackEngine::fullCutDb). */
    float maxCutToEngineDb (float maxCutValue);
    float safetyDepthToEngineDb (float safetyDepthValue);
    /** "18.0 dB", "-18.0 dB" (negative = true), or the infinite forms "inf dB" / "-inf dB" (as unicode). */
    juce::String formatCut (double cutDb, bool negative, int decimals = 1);
    /** Brings a saved state tree / preset value list from an older version to this one. */
    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);
    juce::StringArray safetyTriggerNames(); // Feedback, Always

    juce::StringArray modeNames();        // Safe, Aggressive
    juce::StringArray liveNotchNames();   // Off, On (Hold)
    juce::StringArray recoverSpeedNames(); // Slow, Normal, Fast

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
