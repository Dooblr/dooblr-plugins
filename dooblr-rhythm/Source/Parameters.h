#pragma once

#include "Engine/Types.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrrhythm::params
{
    inline constexpr int stateVersion = 1;

    namespace id
    {
        inline constexpr const char* style          = "style";
        inline constexpr const char* patternLength  = "patternLength";
        inline constexpr const char* timeSignature  = "timeSignature";
        inline constexpr const char* claveDirection = "claveDirection";
        inline constexpr const char* complexity     = "complexity";
        inline constexpr const char* syncopation    = "syncopation";
        inline constexpr const char* humanize       = "humanize";
        inline constexpr const char* swing          = "swing";
        inline constexpr const char* pushPull       = "pushPull";
        inline constexpr const char* section        = "section";
        inline constexpr const char* output         = "output";

        inline constexpr const char* suffixes[] {
            "density", "complexity", "syncopation", "velocity", "humanize",
            "timing", "volume", "pan", "mute", "solo", "lock", "pitch", "decay"
        };
        inline constexpr int numInstrumentParams = 13;
    }

    inline constexpr int numGlobalParameters = 11;
    inline constexpr int numHostParameters = numGlobalParameters + numInstruments * id::numInstrumentParams;

    juce::String instrumentParam (int instrument, const char* suffix);
    int instrumentIndexFromParam (const juce::String& parameterId);

    juce::StringArray styleChoices();
    juce::StringArray timeSignatureChoices();
    juce::StringArray claveChoices();
    juce::StringArray sectionChoices();

    void migrateParameterTree (juce::ValueTree& tree, int fromVersion);
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::String formatValue (double value, int decimals);
    juce::String formatDb (double db);
    juce::String formatPan (double pan);
}
