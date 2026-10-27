#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/VocalSettings.h"

namespace dooblrvocals::params
{
    inline constexpr int stateVersion = 1;

    juce::StringArray chainOrderChoices();
    juce::StringArray keyChoices();
    juce::StringArray scaleChoices();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::String formatHz (float hz);

    /** v0.2 harmonizer: "harm<N>_<suffix>" for voice index 0..3, e.g. harm1_degree. */
    juce::String harmonyId (int voice, const char* suffix);
    const dsp::HarmonyVoice& defaultHarmonyVoice (int voice);
    juce::String degreeName (int steps);   // +2 -> "+3rd", -7 -> "-Oct", 0 -> "Unison"
    int degreeFromName (const juce::String& text);
    juce::String panName (float pan);      // "L35", "C", "R70"
    juce::String noteName (int midiNote);

    /** Reads every parameter into a dsp::VocalSettings (allocation free; any thread). */
    class SettingsReader
    {
    public:
        explicit SettingsReader (juce::AudioProcessorValueTreeState& state);
        dsp::VocalSettings read() const noexcept;

    private:
        using Value = std::atomic<float>*;

        juce::AudioProcessorValueTreeState& state;
        Value pitchOn = nullptr, corrMode = nullptr, corrKey = nullptr, corrScale = nullptr, corrMask = nullptr;
        Value retune = nullptr, humanize = nullptr, amount = nullptr, a4 = nullptr, formant = nullptr;
        Value inputType = nullptr, quality = nullptr;
        Value gateOn = nullptr, gateThreshold = nullptr, gateAttack = nullptr, gateHold = nullptr, gateRelease = nullptr, gateRange = nullptr;
        Value eqOn = nullptr, eqLowFreq = nullptr, eqLowGain = nullptr, eqMidFreq = nullptr, eqMidGain = nullptr, eqMidQ = nullptr;
        Value eqHighFreq = nullptr, eqHighGain = nullptr;
        Value compOn = nullptr, compThreshold = nullptr, compRatio = nullptr, compAttack = nullptr, compRelease = nullptr, compMakeup = nullptr;
        Value limMode = nullptr, limCeiling = nullptr, limRelease = nullptr, outputGain = nullptr, chainOrder = nullptr;
        Value latencyMode = nullptr, harmOn = nullptr, harmLevel = nullptr, harmFormant = nullptr;

        struct VoiceValues { Value on = nullptr, mode = nullptr, degree = nullptr, semis = nullptr, level = nullptr,
                             pan = nullptr, detune = nullptr, delay = nullptr; };
        std::array<VoiceValues, dsp::numHarmonyVoices> harmVoices;
    };
}
