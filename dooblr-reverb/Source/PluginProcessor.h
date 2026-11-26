#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/OutputPeakTap.h"
#include "DSP/ReverbEngine.h"
#include "DSP/SpectrumTap.h"
#include "UndoHistory.h"

namespace dooblrreverb
{
    /**
        dooblr-reverb: an algorithmic reverb (see DSP/ReverbEngine.h). Stereo or mono, zero
        latency. Signal: Input gain -> dry + reverb (predelay, early reflections, modulated FDN,
        width, Post EQ) mixed by Mix -> Output gain. Bypass crossfades to the untouched input.
    */
    class DooblrReverbProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrReverbProcessor();
        ~DooblrReverbProcessor() override;

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
        dsp::ReverbEngine& getEngine() noexcept { return engine; }
        dsp::OutputPeakTap& getPeakTap() noexcept { return peakTap; }
        dsp::SpectrumTap& getPreTap() noexcept { return preTap; }
        dsp::SpectrumTap& getPostTap() noexcept { return postTap; }

        /** The engine settings for the current parameter values (the editor draws from these). */
        dsp::ReverbEngine::Settings readSettings() const;
        dsp::DecayBands readDecayBands() const;
        dsp::EqBands readEqBands() const;
        /** The predelay in ms (the synced time when Sync is on). */
        double currentPredelayMs() const;
        double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
        double getCurrentSampleRate() const noexcept { return engine.getSampleRate(); }

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);
        juce::ChangeBroadcaster presetChanged;

        /** Analyzer: 0 Off, 1 Reverb (the wet output), 2 Input + Reverb. A display preference
            saved with the session; not a parameter (not automatable, not in presets, A / B or undo). */
        static juce::StringArray analyzerModeNames() { return { "Off", "Reverb", "Input + Reverb" }; }
        int getAnalyzerMode() const noexcept { return analyzerMode.load (std::memory_order_relaxed); }
        void setAnalyzerMode (int mode) noexcept { analyzerMode.store (juce::jlimit (0, 2, mode), std::memory_order_relaxed); }

        /** A / B compare: two complete settings snapshots (not saved with the session). */
        int getCompareSlot() const noexcept { return compareSlot; }
        void selectCompareSlot (int slot);
        void copyToOtherCompareSlot();

        static constexpr int maxChunk = 512;

    private:
        struct RawParameters;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::ReverbEngine engine;
        dsp::OutputPeakTap peakTap;
        dsp::SpectrumTap preTap, postTap;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;
        int compareSlot = 0;
        std::array<juce::ValueTree, 2> compareStore;
        std::atomic<int> analyzerMode { 1 };
        std::atomic<double> hostBpm { 120.0 };

        juce::AudioBuffer<float> dryIn, original, wet;
        std::array<float, maxChunk> monoScratch {};
        juce::SmoothedValue<float> inGain, outGain, dryGain, wetGain, bypassMix;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrReverbProcessor)
    };
}
