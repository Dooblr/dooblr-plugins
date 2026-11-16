#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrsub::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 2;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID). */
    namespace id
    {
        // Sub generator
        inline constexpr const char* subAmount     = "subAmount";
        inline constexpr const char* loAmount      = "loAmount";
        inline constexpr const char* frequency     = "frequency";
        inline constexpr const char* octaveMode    = "octaveMode";
        inline constexpr const char* trackingSpeed = "trackingSpeed";
        inline constexpr const char* drive         = "drive";
        inline constexpr const char* mix           = "mix";
        inline constexpr const char* output        = "output";

        // Tone shift
        inline constexpr const char* shift         = "shift";
        inline constexpr const char* transient     = "transient";
        inline constexpr const char* bodyMix       = "bodyMix";
        inline constexpr const char* shiftMix      = "shiftMix";

        // Sub safety / system check
        inline constexpr const char* monoBelow     = "monoBelow";
        inline constexpr const char* monoFreq      = "monoFreq";
        inline constexpr const char* infraHpf      = "infraHpf";
        inline constexpr const char* infraFreq     = "infraFreq";
        inline constexpr const char* infraSlope    = "infraSlope";
        inline constexpr const char* subLimit      = "subLimit";
        inline constexpr const char* target        = "target";

        inline constexpr const char* bypass        = "bypass";

        // Low-end EQ (v0.2.0). Defaults are 0 dB / musical centres so v0.1.0 sessions
        // load unchanged: the stage is a no-op until a gain moves.
        inline constexpr const char* eqShelfGain     = "eqShelfGain";
        inline constexpr const char* eqShelfFreq     = "eqShelfFreq";
        inline constexpr const char* eqPeakGain      = "eqPeakGain";
        inline constexpr const char* eqPeakFreq      = "eqPeakFreq";
        inline constexpr const char* eqPeakQ         = "eqPeakQ";
        inline constexpr const char* eqPresenceGain  = "eqPresenceGain";
        inline constexpr const char* eqPresenceFreq  = "eqPresenceFreq";
        inline constexpr const char* eqPresenceQ     = "eqPresenceQ";

        // Harmonic exciter (v0.2.0). Amount 0 is a no-op.
        inline constexpr const char* harmAmount      = "harmAmount";

        // Phase-align (v0.2.0). Invert off + 0 ms is a no-op.
        inline constexpr const char* phaseInvert     = "phaseInvert";
        inline constexpr const char* phaseDelay      = "phaseDelay";
    }

    inline constexpr int numHostParameters = 31;

    inline constexpr float freqMinHz = 30.0f, freqMaxHz = 120.0f, freqDefaultHz = 60.0f;
    inline constexpr float monoMinHz = 60.0f, monoMaxHz = 150.0f, monoDefaultHz = 100.0f;
    inline constexpr float infraMinHz = 15.0f, infraMaxHz = 40.0f, infraDefaultHz = 25.0f;
    inline constexpr float shiftMinCents = -1200.0f, shiftMaxCents = 1200.0f;
    inline constexpr float eqShelfMinHz = 20.0f, eqShelfMaxHz = 80.0f, eqShelfDefaultHz = 32.0f;
    inline constexpr float eqPeakMinHz = 50.0f, eqPeakMaxHz = 180.0f, eqPeakDefaultHz = 90.0f;
    inline constexpr float eqPresenceMinHz = 100.0f, eqPresenceMaxHz = 250.0f, eqPresenceDefaultHz = 160.0f;
    inline constexpr float eqQMin = 0.4f, eqQMax = 4.0f, eqQDefault = 1.0f;
    inline constexpr float eqGainMinDb = -18.0f, eqGainMaxDb = 12.0f;
    inline constexpr float phaseDelayMaxMs = 12.0f;

    juce::StringArray octaveNames(); // -1 oct, Unison, Both
    juce::StringArray targetNames(); // Club, Festival / PA, Home / Studio
    juce::StringArray infraSlopeNames(); // 6 dB, 12 dB, 24 dB

    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::String formatValue (double value, int decimals);
    juce::String formatFrequency (double hz);
    juce::String formatCents (double cents);
    juce::String formatMs (double ms);
}
