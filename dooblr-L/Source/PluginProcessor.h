#pragma once

#include <array>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/HistoryTap.h"
#include "DSP/LearnSetup.h"
#include "DSP/LimiterEngine.h"
#include "UndoHistory.h"

namespace dooblrl
{
    class DooblrLProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrLProcessor();
        ~DooblrLProcessor() override;

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
        dsp::LimiterEngine& getEngine() noexcept { return engine; }

        dsp::Settings readSettings() const;
        dsp::Meters takeMeters() noexcept { return engine.takeMeters(); }
        void resetMeters() noexcept { engine.resetMeters(); history.discardAll(); }
        dsp::HistoryTap& getHistoryTap() noexcept { return history; }
        dsp::LearnSetup& getLearnSetup() noexcept { return learn; }

        void startLearn (float seconds = dsp::LearnSetup::defaultSeconds);
        void cancelLearn();
        void pollLearn();
        void revertLearn();
        bool isLearning() const noexcept { return learn.isBusy(); }
        bool canRevertLearn() const noexcept { return haveLearnRevert; }
        float learnProgress() const noexcept { return learn.progress(); }
        float lastLearnTargetLufs() const noexcept { return learnedLufs; }
        juce::String getLearnStatus() const { return learnStatus; }

        /** Tests: analyse a buffer and apply as one undoable change. */
        dsp::LearnResult runLearnOnBuffer (const float* left, const float* right, int numSamples, int numChannels);

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
        dsp::LimiterEngine engine;
        dsp::HistoryTap history;
        dsp::LearnSetup learn;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;

        void applyLearnResult (const dsp::LearnResult& result, bool snapshotRevert);
        void applyDenormalised (const juce::String& pid, float value);
        void beginAllLearnGestures();
        void endAllLearnGestures();

        std::vector<std::pair<juce::RangedAudioParameter*, float>> learnRevert;
        bool haveLearnRevert = false;
        float learnedLufs = -70.0f;
        juce::String learnStatus;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrLProcessor)
    };
}
