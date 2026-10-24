#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/AnalogModels.h"
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
        inline constexpr const char* slope           = "slope"; // added in 0.7.0
        inline constexpr const char* model           = "model";   // added in 0.8.0
        inline constexpr const char* stepped         = "stepped"; // added in 0.8.0
        inline constexpr const char* drive           = "drive";   // added in 0.8.0
        inline constexpr const char* atten           = "atten";   // added in 0.8.0
    }

    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* stereoLink = "stereo_link";
    inline constexpr const char* phaseMode  = "phase_mode"; // added in 0.4.0

    /** Processing modes (choice index of phase_mode). */
    enum class PhaseMode { zeroLatency = 0, naturalPhase, linearPhase };
    juce::StringArray phaseModeNames();

    /** A band's Q shown as a bandwidth in octaves ("Width" in the editor):
        BW = 2 / ln 2 * asinh (1 / (2 Q)), and back. Q 1.414 = 1 octave, Q 0.667 = 2 octaves. */
    inline double qToOctaves (double q)       { return 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q)); }
    inline double octavesToQ (double octaves) { return 1.0 / (2.0 * std::sinh (std::log (2.0) * octaves * 0.5)); }

    inline constexpr float minFrequency = 20.0f,   maxFrequency = 20000.0f;
    inline constexpr float minGainDb    = -24.0f,  maxGainDb    = 24.0f;
    inline constexpr float minQ         = 0.1f,    maxQ         = 18.0f;
    inline constexpr float minRangeDb   = -24.0f,  maxRangeDb   = 24.0f;
    inline constexpr float minThresholdDb = -60.0f, maxThresholdDb = 0.0f;
    inline constexpr float minAttackMs  = 0.1f,    maxAttackMs  = 200.0f;
    inline constexpr float minReleaseMs = 5.0f,    maxReleaseMs = 2000.0f;
    inline constexpr float minDriveDb   = -12.0f,  maxDriveDb   = 24.0f;
    inline constexpr float minAttenDb   = 0.0f,    maxAttenDb   = 18.0f;

    /** Parameter ID for a band (0-based index), e.g. bandId (0, band::gain) == "b1_gain". */
    juce::String bandId (int bandIndex, const char* suffix);

    /** Host-visible names of the filter types, in dsp::FilterType order. */
    juce::StringArray filterTypeNames();

    /** Names of the slope choices, in dsp::Slope order. */
    juce::StringArray slopeNames();

    /** Names of the analogue models, in dsp::Model order. */
    juce::StringArray modelNames();

    struct BandDefaults
    {
        dsp::FilterType type;
        float frequency;
        float q;
    };

    BandDefaults bandDefaults (int bandIndex);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
