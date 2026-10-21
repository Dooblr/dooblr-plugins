#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/SvfFilter.h"

/** Parameter IDs, ranges and defaults shared by the processor, editor and tests. */
namespace dooblrq::params
{
    /** Number of EQ bands. Changing this is the only edit needed to add or remove bands. */
    inline constexpr int numBands = 6;

    /** Version written into saved state; bump when the parameter set changes incompatibly. */
    inline constexpr int stateVersion = 1;

    namespace band
    {
        inline constexpr const char* enabled         = "enabled";
        inline constexpr const char* type            = "type";
        inline constexpr const char* frequency       = "freq";
        inline constexpr const char* gain            = "gain";
        inline constexpr const char* q               = "q";
        inline constexpr const char* dynamicsEnabled = "dyn_on";
        inline constexpr const char* range           = "dyn_range";
        inline constexpr const char* threshold       = "dyn_threshold";
        inline constexpr const char* attack          = "dyn_attack";
        inline constexpr const char* release         = "dyn_release";
    }

    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* stereoLink = "stereo_link";

    inline constexpr float minFrequency = 20.0f,   maxFrequency = 20000.0f;
    inline constexpr float minGainDb    = -24.0f,  maxGainDb    = 24.0f;
    inline constexpr float minQ         = 0.1f,    maxQ         = 18.0f;
    inline constexpr float minRangeDb   = -24.0f,  maxRangeDb   = 24.0f;
    inline constexpr float minThresholdDb = -60.0f, maxThresholdDb = 0.0f;
    inline constexpr float minAttackMs  = 0.1f,    maxAttackMs  = 200.0f;
    inline constexpr float minReleaseMs = 5.0f,    maxReleaseMs = 2000.0f;

    /** Parameter ID for a band (0-based index), e.g. bandId (0, band::gain) == "b1_gain". */
    juce::String bandId (int bandIndex, const char* suffix);

    /** Host-visible names of the filter types, in dsp::FilterType order. */
    juce::StringArray filterTypeNames();

    struct BandDefaults
    {
        dsp::FilterType type;
        float frequency;
        float q;
    };

    BandDefaults bandDefaults (int bandIndex);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
