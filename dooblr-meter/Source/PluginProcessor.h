#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Analyzer.h"
#include "DSP/Profiles.h"
#include "Parameters.h"

namespace dooblrmeter
{
    /**
        dooblr-meter: an analysis-only master-bus meter. Audio passes through untouched (bit for
        bit, zero latency) unless the Listen audition or the Mono audition is engaged; both always
        load Off. The audio thread only feeds dsp::Analyzer; a background thread does the analysis.
    */
    class DooblrMeterProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrMeterProcessor();
        ~DooblrMeterProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
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

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        dsp::Analyzer& getAnalyzer() noexcept { return analyzer; }

        /** Current parameter values as analysis settings. */
        dsp::Analyzer::Settings readSettings() const;
        /** Hands the settings to the analyzer and runs one analysis pass (thread or tests). */
        void pumpAnalysis (bool forceFull);
        /** Tests: call before prepareToPlay to drive pumpAnalysis() by hand. */
        void setAnalysisThreadEnabled (bool enabled) noexcept { threadEnabled = enabled; }
        /** Clears the measurements (the Reset button). */
        void resetMeasurements() noexcept { analyzer.requestReset(); }

        /** The audition currently heard: -1 none, else the profile index; plus mono. */
        int getActiveListenProfile() const noexcept { return activeProfile.load(); }
        float getListenMatchGainDb() const noexcept { return matchGainDb.load(); }

    private:
        class AnalysisThread;

        juce::AudioProcessorValueTreeState state;
        dsp::Analyzer analyzer;
        dsp::ProfileAudition audition;
        std::unique_ptr<AnalysisThread> thread;
        bool threadEnabled = true;

        std::atomic<float>* listenParam = nullptr;
        std::atomic<float>* monoParam = nullptr;
        std::atomic<float>* matchParam = nullptr;

        juce::AudioBuffer<float> wet;
        float mix = 0.0f, mixStep = 0.0f;
        int runningProfile = -1;   // audition the audio thread is fading in or out
        bool runningMono = false;
        std::atomic<int> activeProfile { -1 };
        std::atomic<float> matchGainDb { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrMeterProcessor)
    };
}
