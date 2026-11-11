#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrm::params
{
    /** State-tree version written with every saved state (bump when a migration is needed).
        1: 0.1.0–0.1.1. 2: 0.1.2 dropped the A-440 parameter (old sessions that still store
        it load; the extra PARAM is ignored). 0.1.5 adds Osc 3 LFO parameters; missing PARAM
        children are filled with defaults, so the version stays 2. */
    inline constexpr int stateVersion = 2;

    /** Parameter IDs. Never rename. a440 was removed in 0.1.2 (old states/presets that still
        contain it load; remaining IDs keep their meaning). */
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
        inline constexpr const char* osc3Lfo      = "osc3Lfo";
        inline constexpr const char* lfoRate      = "lfoRate";
        inline constexpr const char* lfoSync      = "lfoSync";
        inline constexpr const char* lfoDivision  = "lfoDivision";
        inline constexpr const char* lfoRetrigger = "lfoRetrigger";

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
        inline constexpr const char* emphasis     = "emphasis"; // host/UI name: Resonance (0.1.3)
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

    inline constexpr int parameterCount = 50;

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

    inline const juce::StringArray& lfoDivisionNames()
    {
        static const juce::StringArray names { "1", "1/2", "1/4", "1/8", "1/16", "1/32" };
        return names;
    }

    /** Beats per LFO cycle in 4/4 (whole note = 4 beats). */
    inline float lfoDivisionBeats (int index) noexcept
    {
        static constexpr float beats[] { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
        return beats[juce::jlimit (0, 5, index)];
    }

    inline const juce::StringArray& osc3WaveNames()
    {
        static const juce::StringArray names { "Tri", "Shk", "Saw", "Sqr", "PW", "PN" };
        return names;
    }

    inline const juce::StringArray& osc3LfoWaveNames()
    {
        static const juce::StringArray names { "Tri", "Dn", "Up", "Sqr", "S/H", "Rnd" };
        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
