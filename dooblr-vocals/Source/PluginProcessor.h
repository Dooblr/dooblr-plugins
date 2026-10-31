#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/KeyDetector.h"
#include "DSP/MeterTap.h"
#include "DSP/OutputPeakTap.h"
#include "DSP/PitchTap.h"
#include "DSP/VocalEngine.h"
#include "Parameters.h"
#include "RangeLearner.h"
#include "UndoHistory.h"

class DooblrVocalsAudioProcessor final : public juce::AudioProcessor
{
public:
    DooblrVocalsAudioProcessor();
    ~DooblrVocalsAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
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

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }
    dooblrvocals::ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }

    dooblrvocals::dsp::VocalSettings getCurrentSettings() const noexcept { return settingsReader.read(); }

    void loadFactoryPreset (int index, bool asGesture);
    void randomise (juce::Random& random);

    bool saveUserPreset (const juce::File& file, juce::String& error);
    bool loadUserPreset (const juce::File& file, juce::String& error);
    juce::String getPresetName() const { return presetName; }

    float getGainReductionDb() const noexcept { return gainReductionDb.load(); }

    dooblrvocals::dsp::MeterTap& getMeterTap() noexcept { return meterTap; }
    dooblrvocals::dsp::PitchTap& getPitchTap() noexcept { return pitchTap; }
    dooblrvocals::dsp::OutputPeakTap& getOutputPeakTap() noexcept { return outputPeakTap; }

    float getReverbDuckDb() const noexcept { return reverbDuckDb.load(); }
    float getDelayDuckDb() const noexcept { return delayDuckDb.load(); }
    double getHostBpm() const noexcept { return hostBpm.load(); }

    //== Learn Range (message thread). The armed flag is plain state, not a parameter. ========
    void startRangeLearn();
    /** Drains the audio thread's readings into the learner; call from a timer while learning. */
    void pollRangeLearn();
    /** Stops, computes and (if there was enough voice) applies the range as one undo step. */
    dooblrvocals::LearnedRange stopRangeLearn();
    void cancelRangeLearn();
    bool isLearningRange() const noexcept { return learnArmed.load(); }
    const dooblrvocals::RangeLearner& getRangeLearner() const noexcept { return rangeLearner; }
    /** Applies a learned range (search range, Range type) as one gesture / undo step. */
    void applyLearnedRange (const dooblrvocals::LearnedRange& range);
    void resetLearnedRange();
    dooblrvocals::dsp::PitchTap& getLearnTap() noexcept { return learnTap; }

    /** Voiced time (s) a take needs to complete on its own: progress = voiced / this. */
    static constexpr float learnTargetSeconds = 8.0f;
    /** How long after the last confident voiced reading "hearing voice" stays true. */
    static constexpr float learnVoiceHoldSeconds = 0.3f;
    /**
        Learn Range progress, written by the audio thread (atomics, any thread may read).
        Voiced time counts the samples of blocks whose pitch estimate is voiced with at least
        RangeLearner::minConfidence - the same test the learner applies - so it never goes
        backwards while a take runs and wall-clock time without voice adds nothing.
    */
    struct LearnProgress
    {
        float voicedSeconds = 0.0f;
        float progress = 0.0f;   // 0..1, voicedSeconds / learnTargetSeconds
        bool hearingVoice = false;
    };
    LearnProgress getLearnProgress() const noexcept;

    //== Key Follow ============================================================================
    /** What the editor shows (any thread). appliedKey is -1 unless Key Follow is overriding. */
    struct KeyFollowStatus
    {
        int detectedKey = -1, candidateKey = -1, appliedKey = -1;
        float confidence = 0.0f;
        int activeSource = 0;           // 0 holding / silent, 1 sidechain, 2 vocal
        bool sidechainConnected = false, sidechainSignal = false;
    };
    KeyFollowStatus getKeyFollowStatus() const noexcept;
    const dooblrvocals::dsp::KeyDetector& getKeyDetector() const noexcept { return keyDetector; }
    /** True while the host has the optional sidechain input bus enabled with at least one channel. */
    bool isSidechainConnected() const noexcept;

private:
    juce::ValueTree createStateTree();
    void updateLatency (const dooblrvocals::dsp::VocalSettings& s);
    /** Runs the detector and, when Key Follow is on, overrides the Keyed key / scale in s. */
    void applyKeyFollow (juce::AudioBuffer<float>& buffer, dooblrvocals::dsp::VocalSettings& s) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    dooblrvocals::params::SettingsReader settingsReader;
    dooblrvocals::ParameterUndoHistory undoHistory;
    dooblrvocals::dsp::MeterTap meterTap;
    dooblrvocals::dsp::PitchTap pitchTap;
    dooblrvocals::dsp::OutputPeakTap outputPeakTap;
    dooblrvocals::dsp::PitchTap learnTap;
    dooblrvocals::RangeLearner rangeLearner;
    std::vector<dooblrvocals::dsp::PitchFrame> learnScratch = std::vector<dooblrvocals::dsp::PitchFrame> (512);
    std::atomic<bool> learnArmed { false };
    std::atomic<juce::int64> learnVoicedSamples { 0 }, learnSamplesSinceVoice { 1 << 30 };
    std::atomic<double> learnSampleRate { 48000.0 };
    std::atomic<float> reverbDuckDb { 0.0f }, delayDuckDb { 0.0f };
    std::atomic<double> hostBpm { 120.0 };
    dooblrvocals::dsp::VocalEngine engine;
    dooblrvocals::dsp::KeyDetector keyDetector;
    double keyDetectorRate = 0.0;
    int lockedKey = -1;
    std::atomic<int> appliedKey { -1 };

    std::atomic<float> gainReductionDb { 0.0f };
    int currentProgram = 0;
    juce::String presetName { "Init" };
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrVocalsAudioProcessor)
};
