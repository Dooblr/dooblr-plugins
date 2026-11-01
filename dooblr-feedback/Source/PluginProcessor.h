#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/FeedbackEngine.h"
#include "DSP/OutputPeakTap.h"
#include "UndoHistory.h"
#include "UI/DisplayBallistics.h"

namespace dooblrfeedback
{
    /**
        dooblr-feedback: a live feedback suppressor (see DSP/FeedbackEngine.h). Stereo or mono in
        / out, plus an optional stereo sidechain input for a reference mic that can drive the
        detection. Zero latency: the notches are IIR filters in the signal path, the FFT
        detection runs beside it.

        Room data (the notch bank and the notes) is saved with the session and in user presets;
        it is not part of A / B or the undo history (those cover the settings).
    */
    class DooblrFeedbackProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrFeedbackProcessor();
        ~DooblrFeedbackProcessor() override;

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
        dsp::FeedbackEngine& getEngine() noexcept { return engine; }
        dsp::OutputPeakTap& getPeakTap() noexcept { return peakTap; }

        dsp::FeedbackEngine::Settings readSettings() const;
        /** True when the host feeds the sidechain input (the SC switch has a signal to use). */
        bool isSidechainConnected() const;

        // ---- room data (message thread) ----
        juce::String getNotes() const { return notes; }
        void setNotes (const juce::String& text);
        dsp::NotchArray getNotches() const { return engine.getNotches(); }
        bool clearNotch (int notchId);
        void clearAllNotches();
        bool setNotchFixed (int notchId, bool fixed);
        /** Bumped whenever notches or notes are changed from outside the audio thread. */
        juce::ChangeBroadcaster roomChanged;

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);
        /** Renames a user preset file; returns a status line. */
        juce::String renameUserPreset (const juce::File& file, const juce::String& newName);

        juce::ChangeBroadcaster presetChanged;

        /** Analyzer Speed (ui::AnalyzerSpeed index): a display preference saved with the session,
            kept out of the parameters (not automatable, not in presets, A / B or undo). */
        int getAnalyzerSpeed() const noexcept { return analyzerSpeed.load (std::memory_order_relaxed); }
        void setAnalyzerSpeed (int index) noexcept { analyzerSpeed.store ((int) ui::analyzerSpeedFromIndex (index), std::memory_order_relaxed); }

        /** A / B compare of the settings (not the notches / notes). Clears undo. */
        int getCompareSlot() const noexcept { return compareSlot; }
        void selectCompareSlot (int slot);
        void copyToOtherCompareSlot();

        static constexpr int maxChunk = 512;

    private:
        struct RawParameters;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::FeedbackEngine engine;
        dsp::OutputPeakTap peakTap;
        std::unique_ptr<RawParameters> raw;
        juce::String notes;
        int currentProgram = 0;
        int compareSlot = 0;
        std::array<juce::ValueTree, 2> compareStore;
        std::atomic<int> analyzerSpeed { (int) ui::defaultAnalyzerSpeed };

        juce::AudioBuffer<float> dryBuffer, detectorBuffer;
        juce::SmoothedValue<float> outGain, bypassMix;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrFeedbackProcessor)
    };
}
