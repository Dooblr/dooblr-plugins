#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrm::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Controllers
        inline constexpr const char* tune         = "tune";
        inline constexpr const char* glide        = "glide";
        inline constexpr const char* modMix       = "modMix";
        inline constexpr const char* osc3Control  = "osc3Control";
        inline constexpr const char* oscMod       = "oscMod";
        inline constexpr const char* filterMod    = "filterMod";

        // Oscillator bank
        inline constexpr const char* osc1Range    = "osc1Range";
        inline constexpr const char* osc1Wave     = "osc1Wave";
        inline constexpr const char* osc2Range    = "osc2Range";
        inline constexpr const char* osc2Wave     = "osc2Wave";
        inline constexpr const char* osc2Freq     = "osc2Freq";
        inline constexpr const char* osc3Range    = "osc3Range";
        inline constexpr const char* osc3Wave     = "osc3Wave";
        inline constexpr const char* osc3Freq     = "osc3Freq";

        // Mixer
        inline constexpr const char* mixOsc1      = "mixOsc1";
        inline constexpr const char* mixOsc1On    = "mixOsc1On";
        inline constexpr const char* mixOsc2      = "mixOsc2";
        inline constexpr const char* mixOsc2On    = "mixOsc2On";
        inline constexpr const char* mixOsc3      = "mixOsc3";
        inline constexpr const char* mixOsc3On    = "mixOsc3On";
        inline constexpr const char* mixExt       = "mixExt";
        inline constexpr const char* mixExtOn     = "mixExtOn";
        inline constexpr const char* mixNoise     = "mixNoise";
        inline constexpr const char* mixNoiseOn   = "mixNoiseOn";
        inline constexpr const char* noiseType    = "noiseType";

        // Filter (modifiers)
        inline constexpr const char* cutoff       = "cutoff";
        inline constexpr const char* emphasis     = "emphasis";
        inline constexpr const char* contourAmt   = "contourAmt";
        inline constexpr const char* fAttack      = "fAttack";
        inline constexpr const char* fDecay       = "fDecay";
        inline constexpr const char* fSustain     = "fSustain";
        inline constexpr const char* kybd1        = "kybd1";
        inline constexpr const char* kybd2        = "kybd2";

        // Loudness contour
        inline constexpr const char* aAttack      = "aAttack";
        inline constexpr const char* aDecay       = "aDecay";
        inline constexpr const char* aSustain     = "aSustain";
        inline constexpr const char* decaySwitch  = "decaySwitch";

        // Output
        inline constexpr const char* volume       = "volume";
        inline constexpr const char* a440         = "a440";

        // Voice (plugin extras that the original panel does not name)
        inline constexpr const char* notePriority = "notePriority";
        inline constexpr const char* legato       = "legato";
        inline constexpr const char* bendRange    = "bendRange";
        inline constexpr const char* velocityOn   = "velocityOn";
        inline constexpr const char* output       = "output";

        // Wheels (performance; also MIDI pitch-bend / CC1)
        inline constexpr const char* modWheel     = "modWheel";
        inline constexpr const char* pitchBend    = "pitchBend";
    }

    inline constexpr int parameterCount = 46;

    enum class Wave
    {
        triangle = 0,
        shark,
        saw,
        square,
        pulseWide,
        pulseNarrow
    };

    enum class Range
    {
        ft32 = 0,
        ft16,
        ft8,
        ft4,
        ft2,
        lo
    };

    enum class NoiseType { white = 0, pink };
    enum class NotePriority { low = 0, last };

    inline const juce::StringArray& rangeNames()
    {
        static const juce::StringArray names { "32'", "16'", "8'", "4'", "2'", "LO" };
        return names;
    }

    /** Semitone offset from 8' unison. LO is 6 octaves down (a slow LFO with Osc 3 Control off). */
    inline int rangeSemitones (int index) noexcept
    {
        static constexpr int st[] { -24, -12, 0, 12, 24, -72 };
        return st[juce::jlimit (0, 5, index)];
    }

    inline const juce::StringArray& waveNames()
    {
        static const juce::StringArray names { "Triangle", "Shark", "Saw", "Square", "Pulse Wide", "Pulse Narrow" };
        return names;
    }

    inline const juce::StringArray& noiseNames()
    {
        static const juce::StringArray names { "White", "Pink" };
        return names;
    }

    inline const juce::StringArray& priorityNames()
    {
        static const juce::StringArray names { "Low", "Last" };
        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
