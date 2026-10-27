#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/MeterTap.h"
#include "DSP/OutputPeakTap.h"
#include "DSP/PitchTap.h"
#include "DSP/VocalEngine.h"
#include "Parameters.h"
#include "UndoHistory.h"

class DooblrVocalsAudioProcessor final : public juce::AudioProcessor
{
public:
    DooblrVocalsAudioProcessor();
    ~DooblrVocalsAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }
    dooblrvocals::ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }

    dooblrvocals::dsp::VocalSettings getCurrentSettings() const noexcept { return settingsReader.read(); }

    void loadFactoryPreset (int index, bool asGesture);
    void randomise (juce::Random& random);

    bool saveUserPreset (const juce::File& file, juce::String& error);
    bool loadUserPreset (const juce::File& file, juce::String& error);
    juce::String getPresetName() const { return presetName; }

    float getGainReductionDb() const noexcept { return gainReductionDb.load(); }

    dooblrvocals::dsp::MeterTap& getMeterTap() noexcept { return meterTap; }
    dooblrvocals::dsp::PitchTap& getPitchTap() noexcept { return pitchTap; }
    dooblrvocals::dsp::OutputPeakTap& getOutputPeakTap() noexcept { return outputPeakTap; }

private:
    juce::ValueTree createStateTree();
    void updateLatency (const dooblrvocals::dsp::VocalSettings& s);

    juce::AudioProcessorValueTreeState parameters;
    dooblrvocals::params::SettingsReader settingsReader;
    dooblrvocals::ParameterUndoHistory undoHistory;
    dooblrvocals::dsp::MeterTap meterTap;
    dooblrvocals::dsp::PitchTap pitchTap;
    dooblrvocals::dsp::OutputPeakTap outputPeakTap;
    dooblrvocals::dsp::VocalEngine engine;

    std::atomic<float> gainReductionDb { 0.0f };
    int currentProgram = 0;
    juce::String presetName { "Init" };
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrVocalsAudioProcessor)
};
