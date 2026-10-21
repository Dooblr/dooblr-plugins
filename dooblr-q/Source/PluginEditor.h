#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class OctaEQAudioProcessor;

class OctaEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OctaEQAudioProcessorEditor (OctaEQAudioProcessor& processorToEdit);
    ~OctaEQAudioProcessorEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int defaultWidth  = 900;
    static constexpr int defaultHeight = 520;
    static constexpr int minimumWidth  = 640;
    static constexpr int minimumHeight = 380;
    static constexpr int maximumWidth  = 2400;
    static constexpr int maximumHeight = 1400;

    static constexpr const char* statusText = "Audio pass-through prototype";

private:
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label versionLabel;

    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaEQAudioProcessorEditor)
};
