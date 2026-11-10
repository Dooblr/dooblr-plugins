#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/OutputPeakTap.h"
#include "DSP/SynthEngine.h"
#include "UndoHistory.h"

namespace dooblrm
{
    class DooblrMProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrMProcessor();
        ~DooblrMProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 5.0; }

        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram; }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }
        dsp::SynthEngine& getEngine() noexcept { return engine; }
        dsp::OutputPeakTap& getPeakTap() noexcept { return peakTap; }

        dsp::Settings readSettings() const;

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);

        juce::ChangeBroadcaster presetChanged;

    private:
        struct RawParameters;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::SynthEngine engine;
        dsp::OutputPeakTap peakTap;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrMProcessor)
    };
}
