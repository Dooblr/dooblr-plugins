#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Analysis/SpectrumAnalyzer.h"
#include "DSP/DynamicEQBand.h"
#include "DSP/FirEqEngine.h"
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

    //==============================================================================
    // Spectrum analyzer. The audio thread only pushes samples into the FIFO, and only
    // while the analyzer is running (an editor is showing it). The analyzer itself and the
    // UI settings below belong to the message thread. None of this is saved in the
    // plug-in state.

    enum class AnalyzerSource { postEq = 0, preEq };

    void setAnalyzerRunning (bool shouldRun) noexcept;
    bool isAnalyzerRunning() const noexcept { return analyzerRunning.load (std::memory_order_relaxed); }
    void setAnalyzerSource (AnalyzerSource source) noexcept { analyzerSource.store ((int) source, std::memory_order_relaxed); }
    AnalyzerSource getAnalyzerSource() const noexcept { return (AnalyzerSource) analyzerSource.load (std::memory_order_relaxed); }

    dooblrq::analysis::AnalyzerFifo& getAnalyzerFifo() noexcept { return analyzerFifo; }
    dooblrq::analysis::SpectrumAnalyzer& getSpectrumAnalyzer() noexcept { return spectrumAnalyzer; }

    struct AnalyzerViewSettings
    {
        bool visible = true;
        float tiltDbPerOctave = 4.5f;
        bool showPeak = false;
    };

    AnalyzerViewSettings& getAnalyzerViewSettings() noexcept { return analyzerViewSettings; }

    /** Coefficients are recomputed every this many samples. */
    static constexpr int coefficientUpdateInterval = 16;

    /** Phase mode currently running on the audio thread (may lag the parameter by a block). */
    dooblrq::params::PhaseMode getActivePhaseMode() const noexcept
    {
        return (dooblrq::params::PhaseMode) activePhaseMode.load (std::memory_order_relaxed);
    }

    /** Latency of a phase mode at a sample rate (what setLatencySamples reports). */
    static int latencyFor (dooblrq::params::PhaseMode mode, double sampleRate) noexcept;

    /** In Natural/Linear phase, kernels follow parameter and dynamic-gain changes at most
        this often (samples at 44.1/48 kHz; scaled with the rate). */
    static constexpr int kernelUpdateInterval = 512;

    /** For tests: the FIR engine (valid in Natural/Linear phase). */
    const dooblrq::dsp::FirEqEngine& getFirEngine() const noexcept { return firEngine; }

private:
    static BusesProperties createBusesProperties();
    void clearOutputOnlyChannels (juce::AudioBuffer<float>& buffer) const noexcept;
    void updateBandSettings (bool snap) noexcept;
    void applyPhaseMode (int mode, bool fadeIn) noexcept;
    void updateKernel (bool immediate) noexcept;
    void processFirBlock (float* const* channels, int numChannels, int numSamples, bool linked, bool outputDry) noexcept;
    void applySwitchFade (float* const* channels, int numChannels, int numSamples) noexcept;

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
    std::atomic<float>* phaseMode = nullptr;

    std::array<dooblrq::dsp::DynamicEQBand, dooblrq::params::numBands> bands;
    juce::SmoothedValue<float> outputGain { 1.0f };

    std::atomic<double> preparedSampleRate { 0.0 };

    // Natural / Linear phase (audio thread only, preallocated in prepareToPlay).
    static constexpr int firChunkSize = dooblrq::dsp::FirEqEngine::partitionSize;
    static constexpr int switchFadeLength = 256;
    dooblrq::dsp::FirEqEngine firEngine;
    std::atomic<int> activePhaseMode { 0 };
    std::array<dooblrq::dsp::KernelBand, dooblrq::params::numBands> appliedKernel {};
    int samplesSinceKernel = 0, switchFadeRemaining = 0;
    juce::AudioBuffer<float> detectionScratch, dryScratch;
    std::array<std::vector<float>, 2> dryRing;
    int dryRingPosition = 0;

    dooblrq::analysis::AnalyzerFifo analyzerFifo;
    dooblrq::analysis::SpectrumAnalyzer spectrumAnalyzer;
    std::atomic<bool> analyzerRunning { false };
    std::atomic<int> analyzerSource { (int) AnalyzerSource::postEq };
    AnalyzerViewSettings analyzerViewSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrQAudioProcessor)
};
