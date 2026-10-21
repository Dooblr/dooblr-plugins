#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "UI/ResponseCurveComponent.h"
#include "UI/Theme.h"

class DooblrQAudioProcessor;

class DooblrQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit DooblrQAudioProcessorEditor (DooblrQAudioProcessor& processorToEdit);
    ~DooblrQAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Binds the band controls to the parameters of the given band (0-based). */
    void selectBand (int bandIndex);
    int getSelectedBand() const noexcept { return selectedBand; }

    /** Refreshes the Freeze button and the "Averaging / Frozen: N s" readout (timer; public for tests). */
    void updateAnalyzerControls();

    static constexpr int defaultWidth  = 980;
    static constexpr int defaultHeight = 640;
    static constexpr int minimumWidth  = 860;
    static constexpr int minimumHeight = 580;
    static constexpr int maximumWidth  = 2400;
    static constexpr int maximumHeight = 1400;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
    };

    void timerCallback() override;
    void setUpKnob (Knob& knob, const juce::String& caption, const char* parameterSuffix);
    void updateBandIndicators();
    void setUpAnalyzerControls();

    DooblrQAudioProcessor& audioProcessor;
    dooblrq::ui::DooblrLookAndFeel lookAndFeel;

    juce::Label titleLabel, subtitleLabel, versionLabel, outputCaption;
    juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton stereoLinkButton { "Stereo link" };

    dooblrq::ui::ResponseCurveComponent responseCurve;

    // Analyzer toolbar (UI state kept in the processor, not saved with the plug-in state).
    juce::ToggleButton spectrumButton { "Spectrum" }, peakButton { "Peak" };
    juce::ComboBox sourceBox, tiltBox;
    juce::TextButton freezeButton { "Freeze" }, clearButton { "Clear" };
    juce::Label analyzerReadout;

    std::array<juce::TextButton, dooblrq::params::numBands> bandButtons;

    juce::ToggleButton enabledButton { "Band on" };
    juce::ComboBox typeBox;
    Knob frequencyKnob, gainKnob, qKnob;

    juce::ToggleButton dynamicsButton { "Dynamic" };
    Knob rangeKnob, thresholdKnob, attackKnob, releaseKnob;

    juce::Rectangle<int> toolbarArea;
    juce::Rectangle<int> headerArea, controlsArea, eqSectionArea, dynamicsSectionArea;
    int selectedBand = 0;

    // Attachments are declared last so they are destroyed before the controls they bind.
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ButtonAttachment> stereoLinkAttachment;

    std::unique_ptr<ButtonAttachment> enabledAttachment, dynamicsAttachment;
    std::unique_ptr<ComboBoxAttachment> typeAttachment;
    std::unique_ptr<SliderAttachment> frequencyAttachment, gainAttachment, qAttachment,
                                      rangeAttachment, thresholdAttachment, attackAttachment, releaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DooblrQAudioProcessorEditor)
};
