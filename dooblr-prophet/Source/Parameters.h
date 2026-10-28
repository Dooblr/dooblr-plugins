#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrprophet::params
{
    /** State-tree version written with every saved state (bump when a migration is needed). */
    inline constexpr int stateVersion = 1;

    /** Parameter IDs. Append-only: never rename or reorder (hosts store automation by ID).
        Every ID below was added in 0.1.0. */
    namespace id
    {
        // Voices
        inline constexpr const char* voices       = "voices";
        inline constexpr const char* voiceMode    = "voiceMode";
        inline constexpr const char* unisonVoices = "unisonVoices";
        inline constexpr const char* unisonDetune = "unisonDetune";
        inline constexpr const char* glide        = "glide";
        inline constexpr const char* vintage      = "vintage";
        inline constexpr const char* bendRange    = "bendRange";
        inline constexpr const char* atFilter     = "atFilter";
        inline constexpr const char* atLfo        = "atLfo";
        inline constexpr const char* output       = "output";

        // Oscillator 1
        inline constexpr const char* osc1Mode   = "osc1Mode";
        inline constexpr const char* osc1Coarse = "osc1Coarse";
        inline constexpr const char* osc1Fine   = "osc1Fine";
        inline constexpr const char* osc1Shape  = "osc1Shape";
        inline constexpr const char* osc1PW     = "osc1PW";
        inline constexpr const char* osc1Saw    = "osc1Saw";
        inline constexpr const char* osc1Tri    = "osc1Tri";
        inline constexpr const char* osc1Pulse  = "osc1Pulse";
        inline constexpr const char* osc1Sync   = "osc1Sync";

        // Oscillator 2
        inline constexpr const char* osc2Mode     = "osc2Mode";
        inline constexpr const char* osc2Coarse   = "osc2Coarse";
        inline constexpr const char* osc2Fine     = "osc2Fine";
        inline constexpr const char* osc2Shape    = "osc2Shape";
        inline constexpr const char* osc2PW       = "osc2PW";
        inline constexpr const char* osc2Saw      = "osc2Saw";
        inline constexpr const char* osc2Tri      = "osc2Tri";
        inline constexpr const char* osc2Pulse    = "osc2Pulse";
        inline constexpr const char* osc2LowFreq  = "osc2LowFreq";
        inline constexpr const char* osc2KeyTrack = "osc2KeyTrack";

        // Mixer
        inline constexpr const char* mixOsc1  = "mixOsc1";
        inline constexpr const char* mixOsc2  = "mixOsc2";
        inline constexpr const char* mixSub   = "mixSub";
        inline constexpr const char* mixNoise = "mixNoise";

        // Low-pass (4-pole ladder) and high-pass (2-pole)
        inline constexpr const char* lpfCutoff   = "lpfCutoff";
        inline constexpr const char* lpfReso     = "lpfReso";
        inline constexpr const char* lpfDrive    = "lpfDrive";
        inline constexpr const char* lpfEnvAmt   = "lpfEnvAmt";
        inline constexpr const char* lpfKeyTrack = "lpfKeyTrack";
        inline constexpr const char* hpfCutoff   = "hpfCutoff";
        inline constexpr const char* hpfReso     = "hpfReso";
        inline constexpr const char* hpfEnvAmt   = "hpfEnvAmt";

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
        inline constexpr const char* aenvVelAmt  = "aenvVelAmt";

        // Poly-Mod
        inline constexpr const char* pmodFiltEnv    = "pmodFiltEnv";
        inline constexpr const char* pmodOsc2       = "pmodOsc2";
        inline constexpr const char* pmodDestFreq1  = "pmodDestFreq1";
        inline constexpr const char* pmodDestShape1 = "pmodDestShape1";
        inline constexpr const char* pmodDestFilter = "pmodDestFilter";

        // LFO / wheel mod
        inline constexpr const char* lfoWave       = "lfoWave";
        inline constexpr const char* lfoRate       = "lfoRate";
        inline constexpr const char* lfoSync       = "lfoSync";
        inline constexpr const char* lfoDivision   = "lfoDivision";
        inline constexpr const char* lfoInitial    = "lfoInitial";
        inline constexpr const char* lfoWheel      = "lfoWheel";
        inline constexpr const char* lfoNoiseMix   = "lfoNoiseMix";
        inline constexpr const char* lfoDestFreq1  = "lfoDestFreq1";
        inline constexpr const char* lfoDestFreq2  = "lfoDestFreq2";
        inline constexpr const char* lfoDestShape1 = "lfoDestShape1";
        inline constexpr const char* lfoDestShape2 = "lfoDestShape2";
        inline constexpr const char* lfoDestLpf    = "lfoDestLpf";
        inline constexpr const char* lfoDestHpf    = "lfoDestHpf";
        inline constexpr const char* lfoDestAmp    = "lfoDestAmp";

        // Appended (IDs are append-only: never reorder or reuse)
        inline constexpr const char* lpfModel      = "lpfModel";      // 0 CEM3320 style, 1 SSM2040 style
    }

    enum class VoiceMode { poly = 0, unison, mono };
    enum class OscMode { variable = 0, switched };
    enum class LfoWave { triangle = 0, saw, reverseSaw, square, sampleHold };

    inline constexpr int maxVoices = 10;

    /** Low-pass models (docs/analog-reference.md): clean CEM3320 cells or the softer SSM2040. */
    inline const juce::StringArray& lpfModelNames()
    {
        static const juce::StringArray names { "3320 (Rev3)", "2040 (Rev2 / 6)" };
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
