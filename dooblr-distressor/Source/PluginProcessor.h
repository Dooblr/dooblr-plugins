#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/DistressorEngine.h"
#include "UndoHistory.h"

namespace dooblrdistressor
{
    /**
        dooblr-distressor: an independently designed compressor modelled on the Empirical Labs
        EL8 Distressor (see DSP/DistressorEngine.h). Stereo or mono in / out. Zero latency.
        Bypass still runs the detector so the gain-reduction meter keeps moving.
    */
    class DooblrDistressorProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrDistressorProcessor();
        ~DooblrDistressorProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        juce::AudioProcessorParameter* getBypassParameter() const override;

        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram; }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }
        dsp::DistressorEngine& getEngine() noexcept { return engine; }

        dsp::Settings readSettings() const;
        dsp::Meters takeMeters() noexcept { return engine.takeMeters(); }

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);

        juce::ChangeBroadcaster presetChanged;

        static constexpr int maxChunk = 512;

    private:
        struct RawParameters;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::DistressorEngine engine;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrDistressorProcessor)
    };
}
