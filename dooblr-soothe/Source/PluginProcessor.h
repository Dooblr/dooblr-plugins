#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/OutputPeakTap.h"
#include "DSP/ResonanceEngine.h"
#include "UndoHistory.h"
#include "UI/DisplayBallistics.h"

namespace dooblrsoothe
{
    /**
        dooblr-soothe: a dynamic resonance reducer (see DSP/ResonanceEngine.h). Stereo or mono
        in / out, plus an optional stereo sidechain input that can drive the detection.
        Latency: the FFT size of the chosen Quality (reported to the host; Standard 1024,
        High / Ultra 2048 samples at 44.1 / 48 kHz, doubled at 88.2 / 96 kHz, x4 above).
    */
    class DooblrSootheProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrSootheProcessor();
        ~DooblrSootheProcessor() override;

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
        double getTailLengthSeconds() const override;
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
        dsp::ResonanceEngine& getEngine() noexcept { return engine; }
        dsp::OutputPeakTap& getPeakTap() noexcept { return peakTap; }

        /** The engine settings for the current parameter values (also used by the editor's curve). */
        dsp::ResonanceEngine::Settings readSettings() const;
        dsp::Bands readBands() const;

        /** True when the host feeds the sidechain input (the Sidechain switch has a signal to use). */
        bool isSidechainConnected() const;

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);

        juce::ChangeBroadcaster presetChanged;

        /** Analyzer Speed (ui::AnalyzerSpeed index): a display preference saved with the session,
            kept out of the parameters (not automatable, not in presets, A / B or undo). */
        int getAnalyzerSpeed() const noexcept { return analyzerSpeed.load (std::memory_order_relaxed); }
        void setAnalyzerSpeed (int index) noexcept { analyzerSpeed.store ((int) ui::analyzerSpeedFromIndex (index), std::memory_order_relaxed); }

        /** A / B compare: two complete settings snapshots (not saved with the session). The
            current one is live; selecting the other stores the live settings in this slot and
            recalls the other (the first time, the other slot starts as a copy). Clears undo. */
        int getCompareSlot() const noexcept { return compareSlot; }
        void selectCompareSlot (int slot);
        /** Copies the live settings into the other slot (A -> B while on A, B -> A on B). */
        void copyToOtherCompareSlot();

        static constexpr int maxChunk = 512;

    private:
        struct RawParameters;
        std::pair<float, float> mixTargets() const noexcept;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::ResonanceEngine engine;
        dsp::OutputPeakTap peakTap;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;
        int compareSlot = 0;
        std::array<juce::ValueTree, 2> compareStore;
        std::atomic<int> analyzerSpeed { (int) ui::defaultAnalyzerSpeed };
        int preparedQuality = -1;

        juce::AudioBuffer<float> procIn, detIn, wetOut, dryOut;
        juce::SmoothedValue<float> dryGain, wetGain, outGain;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrSootheProcessor)
    };
}
