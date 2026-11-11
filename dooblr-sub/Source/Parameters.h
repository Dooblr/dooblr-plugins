#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrsub::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

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
        inline constexpr const char* subLimit      = "subLimit";
        inline constexpr const char* target        = "target";

        inline constexpr const char* bypass        = "bypass";
    }

    inline constexpr int numHostParameters = 19;

    inline constexpr float freqMinHz = 30.0f, freqMaxHz = 120.0f, freqDefaultHz = 60.0f;
    inline constexpr float monoMinHz = 60.0f, monoMaxHz = 150.0f, monoDefaultHz = 100.0f;
    inline constexpr float infraMinHz = 15.0f, infraMaxHz = 40.0f, infraDefaultHz = 25.0f;
    inline constexpr float shiftMinCents = -1200.0f, shiftMaxCents = 1200.0f;

    juce::StringArray octaveNames(); // -1 oct, Unison, Both
    juce::StringArray targetNames(); // Club, Festival / PA, Home / Studio

    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::String formatValue (double value, int decimals);
    juce::String formatFrequency (double hz);
    juce::String formatCents (double cents);
}
