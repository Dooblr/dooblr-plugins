#pragma once

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
    };
}
