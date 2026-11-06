#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrdistressor::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID). */
    namespace id
    {
        inline constexpr const char* input      = "input";
        inline constexpr const char* attack     = "attack";
        inline constexpr const char* release    = "release";
        inline constexpr const char* output     = "output";
        inline constexpr const char* ratio      = "ratio";
        inline constexpr const char* audioHp    = "audioHp";
        inline constexpr const char* distMode   = "distMode";
        inline constexpr const char* detHp      = "detHp";
        inline constexpr const char* detBand    = "detBand";
        inline constexpr const char* stereoLink = "stereoLink";
        inline constexpr const char* headroom   = "headroom";
        inline constexpr const char* mix        = "mix";
        inline constexpr const char* bypass     = "bypass";
    }

    inline constexpr float knobMin = 0.0f, knobMax = 10.5f, knobDefault = 5.0f;
    inline constexpr float mixDefault = 100.0f;
    inline constexpr int defaultRatioIndex = 4;      // 6:1
    inline constexpr int defaultHeadroomIndex = 3;   // 16 dB
    inline constexpr int numRatios = 8;
    inline constexpr int numHeadroomSteps = 7;

    juce::StringArray ratioNames();      // 1:1, 2:1, 3:1, 4:1, 6:1, 10:1, 20:1, Nuke
    juce::StringArray distModeNames();   // Off, Dist 2, Dist 3
    juce::StringArray headroomNames();   // 4 dB .. 28 dB

    int headroomDbForIndex (int index);
    int indexForHeadroomDb (int db);

    /** Brings a saved state tree / preset value list from an older version to this one. */
    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Value text with a fixed number of decimals: never exponent notation, never "-0". */
    juce::String formatValue (double value, int decimals);
}
