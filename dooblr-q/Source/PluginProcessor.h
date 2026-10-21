#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/DynamicEQBand.h"
#include "Parameters.h"

class DooblrQAudioProcessor final : public juce::AudioProcessor
{
public:
    DooblrQAudioProcessor();
    ~DooblrQAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

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

    static inline const juce::Identifier stateType { "DooblrQState" };
    static inline const juce::Identifier stateVersionProperty { "stateVersion" };

    //==============================================================================
    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }

    /** Live dynamic gain offset of a band in dB, for display. Lock-free. */
    float getBandDynamicGainDb (int bandIndex) const noexcept;

    /** Reads the current parameter values of a band (any thread). */
    dooblrq::dsp::BandSettings getBandSettings (int bandIndex) const noexcept;

    /** Sample rate for response analysis: the prepared rate, or 48 kHz before preparation. */
    double getAnalysisSampleRate() const noexcept;

    /** Coefficients are recomputed every this many samples. */
    static constexpr int coefficientUpdateInterval = 16;

private:
    static BusesProperties createBusesProperties();
    void clearOutputOnlyChannels (juce::AudioBuffer<float>& buffer) const noexcept;
    void updateBandSettings (bool snap) noexcept;

    struct BandParameterPointers
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* dynamicsEnabled = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
    };

    juce::AudioProcessorValueTreeState parameters;

    std::array<BandParameterPointers, dooblrq::params::numBands> bandParameters;
    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* stereoLink = nullptr;

    std::array<dooblrq::dsp::DynamicEQBand, dooblrq::params::numBands> bands;
    juce::SmoothedValue<float> outputGain { 1.0f };

    std::atomic<double> preparedSampleRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrQAudioProcessor)
};
