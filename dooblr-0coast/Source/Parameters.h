#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblr0coast::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        inline constexpr const char* pitch            = "pitch";
        inline constexpr const char* fine             = "fine";
        inline constexpr const char* portamento       = "portamento";
        inline constexpr const char* linFm            = "linFm";
        inline constexpr const char* overtone         = "overtone";
        inline constexpr const char* overtoneCv       = "overtoneCv";
        inline constexpr const char* multiply         = "multiply";
        inline constexpr const char* multiplyCv       = "multiplyCv";
        inline constexpr const char* slopeCycle       = "slopeCycle";
        inline constexpr const char* slopeRise        = "slopeRise";
        inline constexpr const char* slopeFall        = "slopeFall";
        inline constexpr const char* slopeResponse    = "slopeResponse";
        inline constexpr const char* contourOnset     = "contourOnset";
        inline constexpr const char* contourSustain   = "contourSustain";
        inline constexpr const char* contourDecay     = "contourDecay";
        inline constexpr const char* contourResponse  = "contourResponse";
        inline constexpr const char* balance          = "balance";
        inline constexpr const char* balanceCv        = "balanceCv";
        inline constexpr const char* dynamics         = "dynamics";
        inline constexpr const char* level            = "level";
        inline constexpr const char* arpOn            = "arpOn";
        inline constexpr const char* arpType          = "arpType";
        inline constexpr const char* legato           = "legato";
        inline constexpr const char* midiBCv          = "midiBCv";
        inline constexpr const char* midiBGate        = "midiBGate";
        inline constexpr const char* clockDiv         = "clockDiv";
        inline constexpr const char* mathAmt          = "mathAmt";
        inline constexpr const char* midiBLfoRate     = "midiBLfoRate";
        inline constexpr const char* bendRange        = "bendRange";
        inline constexpr const char* output           = "output";
    }

    inline constexpr int parameterCount = 30;

    enum class ArpType { traditional = 0, latch };
    enum class MidiBCv { note = 0, velocity, mod, lfo };
    enum class MidiBGate { note = 0, velocity, mod, lfo };

    inline const juce::StringArray& arpTypeNames()
    {
        static const juce::StringArray names { "Traditional", "Latch" };
        return names;
    }

    inline const juce::StringArray& midiBCvNames()
    {
        static const juce::StringArray names { "Note", "Velocity", "Mod", "LFO" };
        return names;
    }

    inline const juce::StringArray& midiBGateNames()
    {
        static const juce::StringArray names { "Note", "Velocity", "Mod", "LFO" };
        return names;
    }

    inline const juce::StringArray& clockDivNames()
    {
        static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16" };
        return names;
    }

    /** Length of each clock division in quarter notes. */
    inline double clockDivQuarters (int index)
    {
        static constexpr double q[] { 4.0, 2.0, 1.0, 0.5, 0.25 };
        return q[juce::jlimit (0, 4, index)];
    }

    juce::String formatValue (double value, int decimals);
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);
}
