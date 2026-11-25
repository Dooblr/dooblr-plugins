#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrl::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID). */
    namespace id
    {
        inline constexpr const char* threshold     = "threshold";
        inline constexpr const char* attack        = "attack";
        inline constexpr const char* release       = "release";
        inline constexpr const char* knee          = "knee";
        inline constexpr const char* enhance       = "enhance";
        inline constexpr const char* style         = "style";
        inline constexpr const char* oversampling  = "oversampling";
        inline constexpr const char* lookahead     = "lookahead";
        inline constexpr const char* transientLink = "transientLink";
        inline constexpr const char* releaseLink   = "releaseLink";
        inline constexpr const char* inputGain     = "inputGain";
        inline constexpr const char* ceiling       = "ceiling";
        inline constexpr const char* unityGain     = "unityGain";
        inline constexpr const char* delta         = "delta";
        inline constexpr const char* autoComp      = "autoComp";
        inline constexpr const char* dither        = "dither";
        inline constexpr const char* shaping       = "shaping";
        inline constexpr const char* lufsTarget    = "lufsTarget";
        inline constexpr const char* bypass        = "bypass";
    }

    inline constexpr int numStyles = 8;
    inline constexpr int defaultStyleIndex = 3;          // Allround
    inline constexpr int numOversampling = 6;
    inline constexpr int defaultOversamplingIndex = 2;   // 4x
    inline constexpr int numDither = 3;
    inline constexpr int numShaping = 4;
    inline constexpr int defaultShapingIndex = 1;        // Low
    inline constexpr int numLufsTargets = 5;

    inline constexpr float thresholdDefault = -3.0f;
    inline constexpr float attackDefaultMs = 0.5f;
    inline constexpr float releaseDefaultMs = 50.0f;
    inline constexpr float kneeDefaultDb = 3.0f;
    inline constexpr float enhanceDefault = 0.0f;
    inline constexpr float lookaheadDefaultMs = 2.0f;
    inline constexpr float lookaheadMaxMs = 20.0f;
    inline constexpr float transientLinkDefault = 100.0f;
    inline constexpr float releaseLinkDefault = 70.0f;
    inline constexpr float inputGainDefault = 0.0f;
    inline constexpr float ceilingDefault = -0.3f;

    juce::StringArray styleNames();
    juce::StringArray styleShortNames();
    juce::StringArray oversamplingNames();
    juce::StringArray ditherNames();
    juce::StringArray shapingNames();
    juce::StringArray lufsTargetNames();

    int oversamplingFactor (int index);
    float lufsTargetValue (int index); // NaN when Off

    /** Brings a saved state tree / preset value list from an older version to this one. */
    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Value text with a fixed number of decimals: never exponent notation, never "-0". */
    juce::String formatValue (double value, int decimals);
}
