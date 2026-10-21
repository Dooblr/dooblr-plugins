#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    namespace Palette
    {
        const juce::Colour background   { 0xff16181c };
        const juce::Colour panel        { 0xff1d2026 };
        const juce::Colour divider      { 0xff2c3038 };
        const juce::Colour textPrimary  { 0xffe6e8eb };
        const juce::Colour textMuted    { 0xff8a919c };
        const juce::Colour accent       { 0xff5fb3a1 };
    }

    void styleLabel (juce::Label& label, const juce::Font& font, juce::Colour colour,
                     juce::Justification justification)
    {
        label.setFont (font);
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (justification);
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
OctaEQAudioProcessorEditor::OctaEQAudioProcessorEditor (OctaEQAudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit)
{
    titleLabel.setText ("OctaEQ", juce::dontSendNotification);
    styleLabel (titleLabel,
                juce::Font (juce::FontOptions (26.0f, juce::Font::bold)),
                Palette::textPrimary,
                juce::Justification::centredLeft);

    subtitleLabel.setText ("Parametric equaliser", juce::dontSendNotification);
    styleLabel (subtitleLabel,
                juce::Font (juce::FontOptions (13.0f)),
                Palette::textMuted,
                juce::Justification::centredLeft);

    statusLabel.setText (statusText, juce::dontSendNotification);
    styleLabel (statusLabel,
                juce::Font (juce::FontOptions (13.0f)),
                Palette::textMuted,
                juce::Justification::centred);

    versionLabel.setText ("v" JucePlugin_VersionString, juce::dontSendNotification);
    styleLabel (versionLabel,
                juce::Font (juce::FontOptions (12.0f)),
                Palette::textMuted,
                juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &versionLabel })
        addAndMakeVisible (label);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    setSize (defaultWidth, defaultHeight);
}

//==============================================================================
void OctaEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::panel);
    g.fillRect (headerArea);

    g.setColour (Palette::divider);
    g.fillRect (headerArea.getX(), headerArea.getBottom(), headerArea.getWidth(), 1);

    // Thin accent mark beside the product title.
    const auto accentBar = juce::Rectangle<int> (headerArea.getX() + 20, headerArea.getY() + 18,
                                                 3, headerArea.getHeight() - 36);
    g.setColour (Palette::accent);
    g.fillRect (accentBar);
}

void OctaEQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    headerArea = bounds.removeFromTop (72);

    auto header = headerArea.reduced (32, 14);
    versionLabel.setBounds (header.removeFromRight (120));

    auto titleRow = header.removeFromLeft (360);
    titleLabel.setBounds (titleRow.removeFromTop (titleRow.getHeight() * 3 / 5));
    subtitleLabel.setBounds (titleRow);

    statusLabel.setBounds (bounds.withSizeKeepingCentre (bounds.getWidth(), 24));
}
