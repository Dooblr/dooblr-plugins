#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace params = dooblrq::params;
namespace Palette = dooblrq::ui::Palette;

namespace
{
    void styleLabel (juce::Label& label, const juce::String& text, float fontSize, juce::Colour colour,
                     juce::Justification justification, bool bold = false)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (fontSize, bold ? juce::Font::bold : juce::Font::plain)));
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (justification);
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
DooblrQAudioProcessorEditor::DooblrQAudioProcessorEditor (DooblrQAudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      audioProcessor (processorToEdit),
      responseCurve (processorToEdit)
{
    setLookAndFeel (&lookAndFeel);

    styleLabel (titleLabel, JucePlugin_Name, 24.0f, Palette::textPrimary, juce::Justification::centredLeft, true);
    styleLabel (subtitleLabel, "Dynamic parametric EQ", 13.0f, Palette::textMuted, juce::Justification::centredLeft);
    styleLabel (versionLabel, "v" JucePlugin_VersionString, 12.0f, Palette::textMuted, juce::Justification::centredRight);
    styleLabel (outputCaption, "Output", 13.0f, Palette::textMuted, juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &subtitleLabel, &versionLabel, &outputCaption })
        addAndMakeVisible (label);

    outputSlider.setComponentID (params::outputGain);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.7f));
    outputSlider.setColour (juce::Slider::backgroundColourId, Palette::divider);
    addAndMakeVisible (outputSlider);

    stereoLinkButton.setComponentID (params::stereoLink);
    stereoLinkButton.setTooltip ("Detect dynamics from both channels together");
    addAndMakeVisible (stereoLinkButton);

    auto& state = audioProcessor.getParameterState();
    outputAttachment = std::make_unique<SliderAttachment> (state, params::outputGain, outputSlider);
    stereoLinkAttachment = std::make_unique<ButtonAttachment> (state, params::stereoLink, stereoLinkButton);

    responseCurve.onBandSelected = [this] (int bandIndex) { selectBand (bandIndex); };
    addAndMakeVisible (responseCurve);

    for (int b = 0; b < params::numBands; ++b)
    {
        auto& button = bandButtons[(size_t) b];
        button.setButtonText ("Band " + juce::String (b + 1));
        button.setComponentID ("band" + juce::String (b + 1));
        button.setRadioGroupId (1001);
        button.setClickingTogglesState (true);
        button.onClick = [this, b] { if (bandButtons[(size_t) b].getToggleState()) selectBand (b); };
        addAndMakeVisible (button);
    }

    enabledButton.setComponentID (params::band::enabled);
    addAndMakeVisible (enabledButton);

    typeBox.setComponentID (params::band::type);
    typeBox.addItemList (params::filterTypeNames(), 1);
    addAndMakeVisible (typeBox);

    dynamicsButton.setComponentID (params::band::dynamicsEnabled);
    addAndMakeVisible (dynamicsButton);

    setUpKnob (frequencyKnob, "Frequency", params::band::frequency);
    setUpKnob (gainKnob, "Gain", params::band::gain);
    setUpKnob (qKnob, "Q", params::band::q);
    setUpKnob (rangeKnob, "Range", params::band::range);
    setUpKnob (thresholdKnob, "Threshold", params::band::threshold);
    setUpKnob (attackKnob, "Attack", params::band::attack);
    setUpKnob (releaseKnob, "Release", params::band::release);

    gainKnob.slider.setDoubleClickReturnValue (true, 0.0);
    rangeKnob.slider.setDoubleClickReturnValue (true, 0.0);
    rangeKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, Palette::dynamic);

    selectBand (0);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    setSize (defaultWidth, defaultHeight);

    startTimerHz (15);
}

DooblrQAudioProcessorEditor::~DooblrQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DooblrQAudioProcessorEditor::setUpKnob (Knob& knob, const juce::String& caption, const char* parameterSuffix)
{
    knob.slider.setComponentID (parameterSuffix);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    addAndMakeVisible (knob.slider);

    styleLabel (knob.caption, caption, 12.0f, Palette::textMuted, juce::Justification::centred);
    addAndMakeVisible (knob.caption);
}

//==============================================================================
void DooblrQAudioProcessorEditor::selectBand (int bandIndex)
{
    selectedBand = juce::jlimit (0, params::numBands - 1, bandIndex);

    // Drop the old bindings before attaching the controls to the new band.
    enabledAttachment.reset();
    dynamicsAttachment.reset();
    typeAttachment.reset();
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    rangeAttachment.reset();
    thresholdAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();

    auto& state = audioProcessor.getParameterState();
    const auto id = [this] (const char* suffix) { return params::bandId (selectedBand, suffix); };

    enabledAttachment   = std::make_unique<ButtonAttachment>   (state, id (params::band::enabled), enabledButton);
    typeAttachment      = std::make_unique<ComboBoxAttachment> (state, id (params::band::type), typeBox);
    frequencyAttachment = std::make_unique<SliderAttachment>   (state, id (params::band::frequency), frequencyKnob.slider);
    gainAttachment      = std::make_unique<SliderAttachment>   (state, id (params::band::gain), gainKnob.slider);
    qAttachment         = std::make_unique<SliderAttachment>   (state, id (params::band::q), qKnob.slider);
    dynamicsAttachment  = std::make_unique<ButtonAttachment>   (state, id (params::band::dynamicsEnabled), dynamicsButton);
    rangeAttachment     = std::make_unique<SliderAttachment>   (state, id (params::band::range), rangeKnob.slider);
    thresholdAttachment = std::make_unique<SliderAttachment>   (state, id (params::band::threshold), thresholdKnob.slider);
    attackAttachment    = std::make_unique<SliderAttachment>   (state, id (params::band::attack), attackKnob.slider);
    releaseAttachment   = std::make_unique<SliderAttachment>   (state, id (params::band::release), releaseKnob.slider);

    bandButtons[(size_t) selectedBand].setToggleState (true, juce::dontSendNotification);
    responseCurve.setSelectedBand (selectedBand);

    const auto colour = dooblrq::ui::bandColour (selectedBand);
    for (auto* knob : { &frequencyKnob, &gainKnob, &qKnob })
        knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);

    updateBandIndicators();
    repaint();
}

void DooblrQAudioProcessorEditor::updateBandIndicators()
{
    for (int b = 0; b < params::numBands; ++b)
    {
        const auto settings = audioProcessor.getBandSettings (b);
        auto& button = bandButtons[(size_t) b];
        const auto colour = dooblrq::ui::bandColour (b);

        button.setColour (juce::TextButton::textColourOffId, settings.enabled ? colour : Palette::textMuted);
        button.setColour (juce::TextButton::textColourOnId, settings.enabled ? colour.brighter (0.3f) : Palette::textPrimary);
    }

    // Controls that have no audible effect for the current settings are dimmed but stay usable.
    const auto settings = audioProcessor.getBandSettings (selectedBand);
    const auto gainUsed = dooblrq::dsp::hasGain (settings.type);
    const auto dynamicsUsed = settings.dynamicsEnabled && dooblrq::dsp::supportsDynamics (settings.type);

    gainKnob.slider.setAlpha (gainUsed ? 1.0f : 0.4f);
    dynamicsButton.setAlpha (dooblrq::dsp::supportsDynamics (settings.type) ? 1.0f : 0.4f);

    for (auto* knob : { &rangeKnob, &thresholdKnob, &attackKnob, &releaseKnob })
        knob->slider.setAlpha (dynamicsUsed ? 1.0f : 0.4f);
}

void DooblrQAudioProcessorEditor::timerCallback()
{
    updateBandIndicators();
}

//==============================================================================
void DooblrQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::panel);
    g.fillRect (headerArea);
    g.fillRect (controlsArea);

    g.setColour (Palette::divider);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
    g.fillRect (controlsArea.getX(), controlsArea.getY(), controlsArea.getWidth(), 1);
    g.fillRect (dynamicsSectionArea.getX() - 10, controlsArea.getY() + 14, 1, controlsArea.getHeight() - 28);

    g.setColour (Palette::accent);
    g.fillRect (headerArea.getX() + 20, headerArea.getY() + 14, 3, headerArea.getHeight() - 28);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (dooblrq::ui::bandColour (selectedBand));
    g.drawText ("BAND " + juce::String (selectedBand + 1), eqSectionArea.withHeight (16), juce::Justification::centredLeft);
    g.setColour (Palette::dynamic);
    g.drawText ("DYNAMICS", dynamicsSectionArea.withHeight (16), juce::Justification::centredLeft);
}

void DooblrQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    headerArea = bounds.removeFromTop (60);
    {
        auto header = headerArea.reduced (32, 10);
        auto titleColumn = header.removeFromLeft (260);
        titleLabel.setBounds (titleColumn.removeFromTop (titleColumn.getHeight() * 3 / 5));
        subtitleLabel.setBounds (titleColumn);

        versionLabel.setBounds (header.removeFromRight (60));
        header.removeFromRight (16);
        outputSlider.setBounds (header.removeFromRight (230).withSizeKeepingCentre (230, 24));
        outputCaption.setBounds (header.removeFromRight (56));
        header.removeFromRight (20);
        stereoLinkButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 24));
    }

    controlsArea = bounds.removeFromBottom (180);

    auto bandRow = bounds.removeFromBottom (40).reduced (16, 6);
    const auto buttonWidth = bandRow.getWidth() / params::numBands;
    for (auto& button : bandButtons)
        button.setBounds (bandRow.removeFromLeft (buttonWidth).reduced (3, 0));

    responseCurve.setBounds (bounds.reduced (16, 10));

    auto controls = controlsArea.reduced (20, 12);
    const auto switchColumnWidth = 120;
    const auto knobWidth = (controls.getWidth() - 2 * switchColumnWidth - 24) / 7;

    eqSectionArea = controls.removeFromLeft (switchColumnWidth + 3 * knobWidth);
    controls.removeFromLeft (24);
    dynamicsSectionArea = controls;

    const auto layoutSection = [knobWidth] (juce::Rectangle<int> area,
                                                                juce::Component& toggle,
                                                                juce::Component* secondControl,
                                                                std::initializer_list<Knob*> knobs)
    {
        area.removeFromTop (20);
        auto switches = area.removeFromLeft (switchColumnWidth).reduced (0, 6);
        toggle.setBounds (switches.removeFromTop (26));

        if (secondControl != nullptr)
        {
            switches.removeFromTop (10);
            secondControl->setBounds (switches.removeFromTop (26).withTrimmedRight (12));
        }

        for (auto* knob : knobs)
        {
            auto column = area.removeFromLeft (knobWidth);
            knob->caption.setBounds (column.removeFromTop (18));
            knob->slider.setBounds (column.reduced (4, 0));
        }
    };

    layoutSection (eqSectionArea, enabledButton, &typeBox, { &frequencyKnob, &gainKnob, &qKnob });
    layoutSection (dynamicsSectionArea, dynamicsButton, nullptr, { &rangeKnob, &thresholdKnob, &attackKnob, &releaseKnob });
}
