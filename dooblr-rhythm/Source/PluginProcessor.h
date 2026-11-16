#pragma once

#include "DSP/SynthKit.h"
#include "Engine/Pattern.h"
#include "Engine/RhythmEngine.h"
#include "UndoHistory.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace dooblrrhythm
{
    class DooblrRhythmProcessor final : public juce::AudioProcessor
    {
    public:
        DooblrRhythmProcessor();
        ~DooblrRhythmProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.5; }

        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram; }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }
        dsp::SynthKit& getKit() noexcept { return kit; }
        PatternSlot& getPatternSlot() noexcept { return patternSlot; }

        Pattern generateArrangement (uint32_t seed);
        Pattern generateArrangement();
        Pattern variation();
        Pattern regenerate();
        void setPattern (Pattern p);
        Pattern currentPattern() const;

        void toggleHit (int instrument, int step, float velocity);
        void setHitVelocity (int instrument, int step, float velocity);
        void clearHit (int instrument, int step);

        juce::File writeMidiFile (int instrumentOnly = -1) const;
        juce::File writeWavFile() const;
        juce::String loadReplacement (int instrument, int articBucket, const juce::File& file);
        void clearReplacement (int instrument, int articBucket);

        void setPreviewPlaying (bool shouldPlay);
        bool isPreviewPlaying() const noexcept { return previewPlay.load(); }
        int playheadStep() const noexcept { return playheadStepAtomic.load (std::memory_order_relaxed); }
        double hostBpm() const noexcept { return hostBpmAtomic.load (std::memory_order_relaxed); }
        bool hostPlaying() const noexcept { return hostPlayingAtomic.load (std::memory_order_relaxed); }

        juce::String getPresetName() const;
        void setPresetName (const juce::String& name);
        juce::String saveUserPreset (const juce::File& file);
        juce::String loadUserPreset (const juce::File& file);

        juce::ChangeBroadcaster presetChanged;
        juce::ChangeBroadcaster patternChanged;

        GenerateRequest readRequest() const;
        dsp::KitSettings readKitSettings() const;
        uint32_t currentSeed() const;

        static juce::File tempFolder();

    private:
        struct RawParameters;
        void storePatternInState (const Pattern& p);
        void restorePatternFromState();
        void fireStep (const Pattern& pattern, int step, juce::MidiBuffer& midi, int sampleOffset);
        double stepsPerQuarter (const Pattern& pattern) const noexcept;

        juce::AudioProcessorValueTreeState state;
        ParameterUndoHistory undoHistory;
        std::unique_ptr<RawParameters> raw;
        dsp::SynthKit kit;
        PatternSlot patternSlot;
        int currentProgram = 0;

        std::atomic<bool> previewPlay { false };
        std::atomic<int> playheadStepAtomic { 0 };
        std::atomic<double> hostBpmAtomic { 120.0 };
        std::atomic<bool> hostPlayingAtomic { false };
        std::atomic<uint32_t> seedAtomic { 1 };

        double sampleRate = 44100.0;
        double phaseSteps = 0.0; // running step position when self-clocking
        int lastFiredStep = -1;
        bool lastHostPlaying = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrRhythmProcessor)
    };
}
