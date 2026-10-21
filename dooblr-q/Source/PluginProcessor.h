#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class OctaEQAudioProcessor final : public juce::AudioProcessor
{
public:
    OctaEQAudioProcessor();
    ~OctaEQAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Identifiers of the serialised state, shared with the tests. */
    static inline const juce::Identifier stateType { "OctaEQState" };
    static inline const juce::Identifier stateVersionProperty { "stateVersion" };
    static constexpr int currentStateVersion = 1;

    /** Version found in the most recently restored state, or 0 if none has been restored. */
    int getLastRestoredStateVersion() const noexcept { return lastRestoredStateVersion; }

private:
    static BusesProperties createBusesProperties();

    /** Pass-through: input samples are left untouched in place; any output channel
        that has no matching input channel is silenced. Real-time safe. */
    void passThrough (juce::AudioBuffer<float>& buffer) const noexcept;

    int lastRestoredStateVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaEQAudioProcessor)
};
