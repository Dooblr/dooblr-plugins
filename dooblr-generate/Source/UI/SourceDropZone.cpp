#include "SourceDropZone.h"
#include "Theme.h"

namespace dooblrgenerate::ui
{
    SourceDropZone::SourceDropZone (DooblrGenerateProcessor& p)
        : generator (p)
    {
        setComponentID ("sourceDropZone");
        setOpaque (false);

        emptyLabel.setText ("Drop sample", juce::dontSendNotification);
        emptyLabel.setFont (juce::FontOptions (13.0f));
        emptyLabel.setColour (juce::Label::textColourId, Palette::textMuted);
        emptyLabel.setJustificationType (juce::Justification::centred);
        emptyLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (emptyLabel);

        waveform.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (waveform);

        nameLabel.setFont (juce::FontOptions (11.0f));
        nameLabel.setColour (juce::Label::textColourId, Palette::textMuted);
        nameLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (nameLabel);

        statusLabel.setFont (juce::FontOptions (11.0f));
        statusLabel.setColour (juce::Label::textColourId, Palette::textMuted);
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (statusLabel);

        playButton.setTooltip ("Play source");
        playButton.onClick = [this] { generator.requestSourcePreview(); };
        addAndMakeVisible (playButton);

        clearButton.setTooltip ("Clear source");
        clearButton.onClick = [this]
        {
            generator.clearSource();
            refresh();
        };
        addAndMakeVisible (clearButton);

        amountCaption.setText ("Amount", juce::dontSendNotification);
        amountCaption.setFont (juce::FontOptions (12.0f));
        amountCaption.setColour (juce::Label::textColourId, Palette::textMuted);
        amountCaption.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (amountCaption);

        amount.setComponentID ("variationAmount");
        amount.setSliderStyle (juce::Slider::LinearBar);
        amount.setTextValueSuffix (" %");
        amount.setNumDecimalPlacesToDisplay (1);
        amount.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.35f));
        amount.setColour (juce::Slider::backgroundColourId, Palette::panelRaised);
        amount.setTooltip ("How far the variation is from the source sample");
        amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            generator.getState(), params::id::variationAmount, amount);
        addAndMakeVisible (amount);

        refresh();
    }

    void SourceDropZone::setHighlighted (bool on)
    {
        if (highlighted == on)
            return;
        highlighted = on;
        repaint();
    }

    void SourceDropZone::loadPath (const juce::String& path)
    {
        if (! juce::File::isAbsolutePath (path))
            return;

        juce::String error;
        generator.loadSourceFile (juce::File (path), error);
        refresh();
    }

    bool SourceDropZone::isInterestedInFileDrag (const juce::StringArray& files)
    {
        return ! files.isEmpty();
    }

    void SourceDropZone::fileDragEnter (const juce::StringArray&, int, int) { setHighlighted (true); }
    void SourceDropZone::fileDragExit (const juce::StringArray&) { setHighlighted (false); }

    void SourceDropZone::filesDropped (const juce::StringArray& files, int, int)
    {
        setHighlighted (false);
        juce::String lastError = "Not a readable audio file";

        for (const auto& path : files)
        {
            const juce::File file (path);
            juce::String error;
            if (generator.loadSourceFile (file, error))
            {
                refresh();
                return;
            }
            lastError = error;
        }

        refresh();
        if (generator.getSourceSample() == nullptr && lastError.isNotEmpty())
            statusLabel.setText (lastError, juce::dontSendNotification);
    }

    bool SourceDropZone::isInterestedInDragSource (const SourceDetails& details)
    {
        const auto path = details.description.toString();
        return juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile();
    }

    void SourceDropZone::itemDragEnter (const SourceDetails&) { setHighlighted (true); }
    void SourceDropZone::itemDragExit (const SourceDetails&) { setHighlighted (false); }

    void SourceDropZone::itemDropped (const SourceDetails& details)
    {
        setHighlighted (false);
        loadPath (details.description.toString());
    }

    void SourceDropZone::refresh()
    {
        const auto serial = generator.getSourceSerial();
        const auto source = generator.getSourceSample();
        const auto hasSource = source != nullptr;
        const auto canVary = hasSource && generator.currentModelSupportsSourceVariation();

        if (serial != lastSourceSerial)
        {
            lastSourceSerial = serial;
            waveform.setSample (source);
        }

        emptyLabel.setVisible (! hasSource);
        waveform.setVisible (hasSource);
        nameLabel.setVisible (hasSource);
        playButton.setVisible (hasSource);
        clearButton.setVisible (hasSource);
        amountCaption.setVisible (hasSource);
        amount.setVisible (hasSource);

        playButton.setEnabled (hasSource);
        clearButton.setEnabled (hasSource);
        amount.setEnabled (canVary);

        if (hasSource)
        {
            juce::String name = generator.getSourceName();
            if (name.isEmpty() && source->file.existsAsFile())
                name = source->file.getFileName();
            name << "  " << juce::String (source->lengthSeconds(), 2) << " s";
            if (generator.wasSourceTrimmed())
                name << "  trimmed";
            nameLabel.setText (name, juce::dontSendNotification);

            amount.setTooltip (canVary ? "How far the variation is from the source sample"
                                       : "AudioLDM has no audio-to-audio path; use Stable Audio Open Small");
        }
        else
        {
            nameLabel.setText ({}, juce::dontSendNotification);
            amount.setTooltip ("Drop a sample to use Variation Amount");
        }

        const auto message = generator.getSourceMessage();
        const auto showError = generator.isSourceMessageError() && message.isNotEmpty();
        statusLabel.setText (showError ? message : juce::String(), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0574f));
        statusLabel.setVisible (showError);

        resized();
        repaint();
    }

    void SourceDropZone::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (highlighted ? Palette::accent.withAlpha (0.10f) : Palette::background);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (highlighted ? Palette::accent : Palette::divider);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, highlighted ? 1.6f : 1.0f);
    }

    void SourceDropZone::resized()
    {
        auto area = getLocalBounds().reduced (8, 6);
        const auto hasSource = generator.getSourceSample() != nullptr;

        if (statusLabel.isVisible())
        {
            statusLabel.setBounds (area.removeFromBottom (16));
            area.removeFromBottom (4);
        }
        else
        {
            statusLabel.setBounds ({});
        }

        if (hasSource)
        {
            auto amountRow = area.removeFromBottom (22);
            amountCaption.setBounds (amountRow.removeFromLeft (64));
            amount.setBounds (amountRow);
            area.removeFromBottom (6);

            auto buttons = area.removeFromBottom (26);
            playButton.setBounds (buttons.removeFromLeft (72).withSizeKeepingCentre (72, 26));
            buttons.removeFromLeft (8);
            clearButton.setBounds (buttons.removeFromLeft (26).withSizeKeepingCentre (26, 26));
            area.removeFromBottom (6);

            nameLabel.setBounds (area.removeFromBottom (16));
            area.removeFromBottom (4);
            waveform.setBounds (area);
            emptyLabel.setBounds ({});
        }
        else
        {
            emptyLabel.setBounds (area);
            waveform.setBounds ({});
            nameLabel.setBounds ({});
            playButton.setBounds ({});
            clearButton.setBounds ({});
            amount.setBounds ({});
            amountCaption.setBounds ({});
        }
    }
}
