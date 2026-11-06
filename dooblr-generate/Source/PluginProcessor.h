#pragma once

#include "Engine/Sample.h"
#include "Engine/Sampler.h"
#include "Generation/GenerationService.h"
#include "Generation/History.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/**
    dooblr-generate: types a description ("bright clap", "909 kick"), runs a local text-to-audio
    model in a separate worker process, and plays the result as a one-shot sampler from MIDI.
    Generation never runs on, or waits for, the audio thread.
*/
class DooblrGenerateProcessor final : public juce::AudioProcessor
{
public:
    DooblrGenerateProcessor();
    /** Tests: use a private root folder instead of ~/Library/Application Support/dooblr/generate. */
    explicit DooblrGenerateProcessor (dooblrgenerate::gen::Paths paths);
    ~DooblrGenerateProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    dooblrgenerate::gen::GenerationService& getService() noexcept { return service; }
    dooblrgenerate::gen::History& getHistory() noexcept { return history; }
    const dooblrgenerate::gen::Paths& getPaths() const noexcept { return paths; }

    /** The generation settings (prompt, seed, seconds, model); message thread. */
    juce::ValueTree getSettings() { return state.state.getOrCreateChildWithName (dooblrgenerate::params::setting::tree, nullptr); }
    juce::String getPrompt() { return getSettings()[dooblrgenerate::params::setting::prompt].toString(); }
    juce::int64 getSeed() { return (juce::int64) getSettings()[dooblrgenerate::params::setting::seed]; }
    double getSeconds();
    juce::String getModel();
    void setPrompt (const juce::String&);
    void setSeed (juce::int64);
    void setSeconds (double);
    void setModel (const juce::String&);

    /** Starts a generation with the current settings (a new random seed first if variation). */
    bool generate (bool variation);
    static juce::int64 randomSeed();

    /** Loads a history entry (or any audio file) as the current sample. Message thread. */
    bool loadHistoryEntry (const juce::String& id, juce::String& error);
    bool loadFile (const juce::File& file, juce::String& error);

    /** The sample being played (nullptr: none). */
    std::shared_ptr<const dooblrgenerate::engine::Sample> getCurrentSample() const { return slot.current(); }
    /** Plays the current sample at the root key (the editor's Play button); audio thread picks it up. */
    void requestPreview() noexcept { previewRequested.store (true); }

    /** Increments whenever the current sample changes (the editor watches it). */
    int getSampleSerial() const noexcept { return sampleSerial.load(); }

    dooblrgenerate::engine::SampleSlot& getSlotForTests() noexcept { return slot; }
    int getActiveVoicesForTests() const noexcept { return sampler.numActiveVoices(); }

private:
    void onGenerated (const dooblrgenerate::gen::HistoryEntry&);
    void publish (std::shared_ptr<dooblrgenerate::engine::Sample>);
    dooblrgenerate::engine::PlaybackSettings readSettings() const noexcept;

    dooblrgenerate::gen::Paths paths;
    juce::AudioProcessorValueTreeState state;
    dooblrgenerate::gen::History history;
    dooblrgenerate::engine::SampleSlot slot;
    dooblrgenerate::engine::Sampler sampler;
    std::atomic<bool> previewRequested { false };
    std::atomic<int> sampleSerial { 0 };

    std::atomic<float>* output = nullptr;
    std::atomic<float>* tune = nullptr;
    std::atomic<float>* fine = nullptr;
    std::atomic<float>* start = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* velocity = nullptr;

    // Last: its thread may call onGenerated until it is destroyed, so it goes first.
    dooblrgenerate::gen::GenerationService service;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrGenerateProcessor)
};
