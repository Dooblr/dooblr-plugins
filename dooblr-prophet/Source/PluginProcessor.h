#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/OutputPeakTap.h"
#include "DSP/SynthEngine.h"
#include "UndoHistory.h"

namespace dooblrprophet
{
    namespace link { class RemoteLink; }

    class DooblrProphetProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrProphetProcessor();
        ~DooblrProphetProcessor() override;

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
        double getTailLengthSeconds() const override { return 15.0; }

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
        /** The Max for Live device's link (see Link/RemoteLink.h); never null. */
        link::RemoteLink& getRemoteLink() noexcept { return *remoteLink; }
        /** The host tempo at the last block (120 until a host reports one); for the FX graph. */
        double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

        /** The engine settings for the current parameter values (also used by the displays). */
        dsp::Settings readSettings() const;

        /** Name shown in the preset box: a factory name, a user preset name, or empty. */
        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);
        /** The Random button: a new random patch (see Randomiser.h) as one undo step, shown as
            "Random" in the preset box. Returns the number of parameters changed. */
        int randomisePatch (juce::Random& random);
        /** Picks an FX preset (0 Echo, 1 Reverb): sets the preset and the macros it starts from
            (Mix, Time / Sync / Division, Feedback and Ping-Pong; or Mix, Size and Tone) as one undo step. */
        void applyFxPreset (int module, int index);

        /** Messages from the processor to the editor (preset changes): a change broadcaster. */
        juce::ChangeBroadcaster presetChanged;

    private:
        struct RawParameters;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        dsp::SynthEngine engine;
        dsp::FxChain fx;
        std::atomic<double> hostBpm { 120.0 };
        dsp::OutputPeakTap peakTap;
        std::unique_ptr<RawParameters> raw;
        int currentProgram = 0;

        // Last, so it is destroyed first (it uses everything above).
        std::unique_ptr<link::RemoteLink> remoteLink;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrProphetProcessor)
    };
}
