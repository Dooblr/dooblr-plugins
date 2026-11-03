#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrbass::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Oscillator 1
        inline constexpr const char* osc1Wave   = "osc1Wave";
        inline constexpr const char* osc1Octave = "osc1Octave";
        inline constexpr const char* osc1PW     = "osc1PW";

        // Oscillator 2
        inline constexpr const char* osc2Wave   = "osc2Wave";
        inline constexpr const char* osc2Octave = "osc2Octave";
        inline constexpr const char* osc2Tune   = "osc2Tune";
        inline constexpr const char* osc2PW     = "osc2PW";

        // Oscillator 3 (audio, or a slow LFO-range oscillator)
        inline constexpr const char* osc3Wave     = "osc3Wave";
        inline constexpr const char* osc3Octave   = "osc3Octave";
        inline constexpr const char* osc3Tune     = "osc3Tune";
        inline constexpr const char* osc3LoFreq   = "osc3LoFreq";
        inline constexpr const char* osc3KeyTrack = "osc3KeyTrack";

        // Sub oscillator (square below osc 1)
        inline constexpr const char* subOctave = "subOctave";

        // Mixer
        inline constexpr const char* mixOsc1     = "mixOsc1";
        inline constexpr const char* mixOsc2     = "mixOsc2";
        inline constexpr const char* mixOsc3     = "mixOsc3";
        inline constexpr const char* mixSub      = "mixSub";
        inline constexpr const char* mixNoise    = "mixNoise";
        inline constexpr const char* mixDrive    = "mixDrive";
        inline constexpr const char* mixOverload = "mixOverload";

        // 24 dB/oct transistor ladder low-pass
        inline constexpr const char* lpfCutoff   = "lpfCutoff";
        inline constexpr const char* lpfReso     = "lpfReso";
        inline constexpr const char* lpfEnvAmt   = "lpfEnvAmt";
        inline constexpr const char* lpfKeyTrack = "lpfKeyTrack";

        // Filter envelope and amp envelope
        inline constexpr const char* fenvAttack  = "fenvAttack";
        inline constexpr const char* fenvDecay   = "fenvDecay";
        inline constexpr const char* fenvSustain = "fenvSustain";
        inline constexpr const char* fenvRelease = "fenvRelease";
        inline constexpr const char* fenvVelAmt  = "fenvVelAmt";
        inline constexpr const char* aenvAttack  = "aenvAttack";
        inline constexpr const char* aenvDecay   = "aenvDecay";
        inline constexpr const char* aenvSustain = "aenvSustain";
        inline constexpr const char* aenvRelease = "aenvRelease";
        inline constexpr const char* sharedDecay = "sharedDecay"; // release follows decay (one-shot / Mini-style)

        // Voice
        inline constexpr const char* glide        = "glide";
        inline constexpr const char* legato       = "legato";
        inline constexpr const char* notePriority = "notePriority";
        inline constexpr const char* bendRange    = "bendRange";
        inline constexpr const char* output       = "output";

        // LFO
        inline constexpr const char* lfoWave      = "lfoWave";
        inline constexpr const char* lfoRate      = "lfoRate";
        inline constexpr const char* lfoSync      = "lfoSync";
        inline constexpr const char* lfoDivision  = "lfoDivision";
        inline constexpr const char* lfoToPitch   = "lfoToPitch";
        inline constexpr const char* lfoToFilter  = "lfoToFilter";
        inline constexpr const char* lfoWheel     = "lfoWheel";
    }

    enum class Wave { saw = 0, square };
    enum class Osc3Wave { triangle = 0, saw, square };
    enum class LfoWave { triangle = 0, saw, reverseSaw, square, sampleHold };
    enum class NotePriority { last = 0, low };

    /** Footage names and the semitone offset of each (32' / 16' / 8' / 4'). */
    inline const juce::StringArray& octaveNames()
    {
        static const juce::StringArray names { "32'", "16'", "8'", "4'" };
        return names;
    }

    inline int octaveSemitones (int index) noexcept
    {
        static constexpr int st[] { -24, -12, 0, 12 };
        return st[juce::jlimit (0, 3, index)];
    }

    inline const juce::StringArray& subOctaveNames()
    {
        static const juce::StringArray names { "-1 oct", "-2 oct" };
        return names;
    }

    /** LFO sync divisions: the choice names and their length in quarter notes. */
    inline const juce::StringArray& lfoDivisionNames()
    {
        static const juce::StringArray names { "4 bars", "2 bars", "1/1", "1/2", "1/2T", "1/4.", "1/4", "1/4T",
                                               "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        return names;
    }

    inline double lfoDivisionQuarters (int index)
    {
        static constexpr double q[] { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                      0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return q[juce::jlimit (0, (int) std::size (q) - 1, index)];
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
