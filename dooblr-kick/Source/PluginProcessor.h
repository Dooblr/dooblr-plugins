#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/KickEngine.h"
#include "DSP/OutputPeakTap.h"
#include "DSP/PlayheadTap.h"
#include "Parameters.h"
#include "Samples/SampleBank.h"
#include "UndoHistory.h"

class DooblrKickAudioProcessor final : public juce::AudioProcessor,
                                       private juce::AudioProcessorParameter::Listener,
                                       private juce::Timer
{
public:
    DooblrKickAudioProcessor();
    ~DooblrKickAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    /** The host program list: "Init" and the six factory kicks. */
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }
    dooblrkick::ParameterUndoHistory& getUndoHistory() noexcept { return undoHistory; }

    /** The current parameters with the sample pointers resolved (any thread). */
    dooblrkick::dsp::KickSettings getCurrentSettings() const noexcept;

    /** One hit at the root note, full velocity (message thread; allocates). */
    juce::AudioBuffer<float> renderPreview (double sampleRate) const;

    /** Message thread: captures the current settings into a job that renders the preview on
        any thread. User samples it uses are kept alive until the job has been destroyed. */
    std::function<juce::AudioBuffer<float>()> makePreviewJob (double sampleRate);

    /** Renders one hit at the root note and writes a 24-bit stereo WAV. */
    bool exportWav (const juce::File& file, double sampleRate, juce::String& error) const;
    static juce::File getDefaultExportFolder(); // ~/Music/dooblr-kick

    /** v0.3.5 Export: the file a chosen save path writes to (".wav" appended when the name
        has no .wav extension; an empty choice stays empty). */
    static juce::File exportDestinationFor (const juce::File& chosen);
    static constexpr const char* defaultExportFileName = "dooblr-kick.wav";
    /** Where the Export dialog starts: dooblr-kick.wav in the last folder exported to (kept in
        the plugin state), else in the default folder. */
    juce::File getExportStartFile() const;
    juce::File getLastExportFolder() const { return lastExportFolder; }
    /** Renders the hit and writes it to the chosen path (see exportDestinationFor); on success
        the folder becomes the last-used one. Same format as before (24-bit, given rate). */
    bool exportTo (const juce::File& chosen, double sampleRate, juce::File& written, juce::String& error);

    /** v0.3.5: the editor's envelope / waveform split (the envelope's share of the display
        width), UI-only, kept in the plugin state; not a parameter, not undoable. */
    static constexpr float defaultEnvelopeSplit = 0.6f, minEnvelopeSplit = 0.3f, maxEnvelopeSplit = 0.8f;
    float getEnvelopeSplit() const noexcept { return envelopeSplit; }
    void setEnvelopeSplit (float ratio) noexcept;

    /** Factory presets (with gestures they are one undo step). */
    void loadFactoryPreset (int index, bool asGesture);
    void randomise (juce::Random& random);

    bool saveUserPreset (const juce::File& file, juce::String& error);
    bool loadUserPreset (const juce::File& file, juce::String& error);
    juce::String getPresetName() const { return presetName; }

    /** User WAV / AIFF for the "User file" sample choice of a layer (message thread). */
    bool loadUserSample (int layer, const juce::File& file, juce::String& error);
    void clearUserSample (int layer);
    juce::String getUserSamplePath (int layer) const;
    const dooblrkick::dsp::SampleData* getUserSample (int layer) const noexcept;
    const dooblrkick::samples::FactoryBank& getFactoryBank() const noexcept { return *factoryBank; }

    /** Plays one hit at the root note from the editor (picked up by the next audio block). */
    void triggerAudition() noexcept { auditionRequested.store (true); }

    /** Increments on every parameter or user-sample change (for the waveform display). */
    juce::uint32 getChangeCount() const noexcept { return changeCount.load(); }

    float getGainReductionDb() const noexcept { return gainReductionDb.load(); }

    /** Lock-free meter / spectrum feed the editor's EQ, Dynamics and Limiter views read. */
    dooblrkick::dsp::MeterTap& getMeterTap() noexcept { return meterTap; }
    /** v0.3.4: the live hit's position for the envelope graphs' playhead (lock free). */
    dooblrkick::dsp::PlayheadTap& getPlayheadTap() noexcept { return playheadTap; }
    /** v0.3.4: the final output's peaks for the header meter (lock free). */
    dooblrkick::dsp::OutputPeakTap& getOutputPeakTap() noexcept { return outputPeakTap; }

private:
    void parameterValueChanged (int, float) override { ++changeCount; }
    void parameterGestureChanged (int, bool) override {}
    void timerCallback() override;

    void resolveSamples (dooblrkick::dsp::KickSettings& settings) const noexcept;
    juce::ValueTree createStateTree();
    void restoreUserSamples (const juce::ValueTree& state);
    void retire (std::unique_ptr<dooblrkick::dsp::SampleData> data);

    juce::AudioProcessorValueTreeState parameters;
    dooblrkick::params::SettingsReader settingsReader;
    dooblrkick::ParameterUndoHistory undoHistory;
    juce::SharedResourcePointer<dooblrkick::samples::FactoryBank> factoryBank;

    dooblrkick::dsp::MeterTap meterTap;
    dooblrkick::dsp::PlayheadTap playheadTap;
    dooblrkick::dsp::OutputPeakTap outputPeakTap;
    dooblrkick::dsp::KickEngine engine;

    struct UserSlot
    {
        std::atomic<const dooblrkick::dsp::SampleData*> current { nullptr };
        std::unique_ptr<dooblrkick::dsp::SampleData> owned;
        juce::String path;
    };

    std::array<UserSlot, dooblrkick::params::numLayers> userSlots;

    struct Retired
    {
        std::unique_ptr<dooblrkick::dsp::SampleData> data;
        juce::uint32 time = 0;
    };

    std::vector<Retired> retired;
    std::shared_ptr<std::atomic<int>> previewJobsInFlight = std::make_shared<std::atomic<int>> (0);

    std::atomic<bool> auditionRequested { false };
    int auditionNote = -1, auditionSamplesLeft = 0;

    std::atomic<juce::uint32> changeCount { 1 };
    std::atomic<float> gainReductionDb { 0.0f };

    int currentProgram = 0;
    juce::String presetName { "Init" };
    juce::File lastExportFolder;
    float envelopeSplit = defaultEnvelopeSplit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrKickAudioProcessor)
};
