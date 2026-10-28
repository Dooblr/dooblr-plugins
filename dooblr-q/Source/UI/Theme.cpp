#include "Theme.h"

namespace dooblrq::ui
{
    juce::Colour bandColour (int bandIndex)
    {
        static const juce::Colour colours[] {
            juce::Colour (0xff6fa8dc), juce::Colour (0xff5fb3a1), juce::Colour (0xffa3c46c),
            juce::Colour (0xffd9a35b), juce::Colour (0xffd9776b), juce::Colour (0xffb08fd0),
        };

        return colours[(size_t) bandIndex % std::size (colours)];
    }

    DooblrLookAndFeel::DooblrLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
        setColour (juce::Slider::textBoxTextColourId, Palette::textPrimary);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Palette::accent.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId, Palette::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, Palette::divider);
        setColour (juce::Slider::thumbColourId, Palette::textPrimary);
        setColour (juce::Label::textColourId, Palette::textMuted);
        setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
        setColour (juce::ComboBox::outlineColourId, Palette::divider);
        setColour (juce::ComboBox::textColourId, Palette::textPrimary);
        setColour (juce::ComboBox::arrowColourId, Palette::textMuted);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panelRaised);
        setColour (juce::PopupMenu::textColourId, Palette::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.35f));
        setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, Palette::divider);
        setColour (juce::TextButton::textColourOffId, Palette::textMuted);
        setColour (juce::TextButton::textColourOnId, Palette::textPrimary);
        setColour (juce::ToggleButton::textColourId, Palette::textPrimary);
        setColour (juce::ToggleButton::tickColourId, Palette::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, Palette::textMuted);
        setColour (juce::TextEditor::backgroundColourId, Palette::panelRaised);
        setColour (juce::TextEditor::textColourId, Palette::textPrimary);
        setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, Palette::textPrimary);
    }

    KnobGeometry knobGeometry (juce::Slider& slider)
    {
        // The same rectangle JUCE passes to drawRotarySlider (the slider minus its text box).
        const auto bounds = slider.getLookAndFeel().getSliderLayout (slider).sliderBounds.toFloat().reduced (4.0f);
        // A slider in a wider column (Width, for its long readout) can cap its knob so every knob matches.
        const auto maxDiameter = (float) (double) slider.getProperties().getWithDefault ("maxKnobDiameter", 1.0e6);

        KnobGeometry g;
        g.centre = bounds.getCentre();
        g.outerRadius = juce::jmax (0.0f, juce::jmin (bounds.getWidth(), bounds.getHeight(), maxDiameter - 8.0f) * 0.5f);
        g.knobRadius = g.outerRadius;

        if ((bool) slider.getProperties().getWithDefault ("rangeRing", false))
        {
            // Give up a little of the knob for a ring that reaches a few pixels into the padding.
            const auto inset = juce::jlimit (5.0f, 10.0f, g.outerRadius * 0.25f);
            g.knobRadius = g.outerRadius - inset;
            g.ringWidth = juce::jmax (3.5f, inset * 0.6f);
            g.ringRadius = g.knobRadius + 3.0f + g.ringWidth * 0.5f;
        }

        return g;
    }

    void DooblrLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
    {
        juce::ignoreUnused (x, y, width, height); // knobGeometry derives the same rectangle from the slider
        const auto geometry = knobGeometry (slider);
        const auto radius = geometry.knobRadius;
        const auto centre = geometry.centre;
        const auto lineWidth = juce::jmax (2.0f, radius * 0.12f);
        const auto arcRadius = radius - lineWidth * 0.5f;
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar parameters fill from the centre, unipolar ones from the start.
        const auto range = slider.getRange();
        const auto bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const auto originAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                         : startAngle;

        if (slider.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (Palette::panelRaised);
        g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));

        const auto pointerLength = radius * 0.55f;
        const juce::Point<float> tip (centre.x + pointerLength * std::sin (angle),
                                      centre.y - pointerLength * std::cos (angle));
        g.setColour (slider.isEnabled() ? Palette::textPrimary : Palette::textMuted);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.15f, angle), tip }, 2.0f);
    }

    void DooblrLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool highlighted, bool down)
    {
        juce::ignoreUnused (down);

        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto on = button.getToggleState();

        auto pill = bounds.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 14.0f);
        g.setColour (on ? Palette::accent.withAlpha (button.isEnabled() ? 0.9f : 0.4f) : Palette::divider);
        g.fillRoundedRectangle (pill, 7.0f);

        const auto knob = juce::Rectangle<float> (10.0f, 10.0f)
                              .withCentre ({ on ? pill.getRight() - 7.0f : pill.getX() + 7.0f, pill.getCentreY() });
        g.setColour (highlighted ? Palette::textPrimary : Palette::textPrimary.withAlpha (0.85f));
        g.fillEllipse (knob);

        g.setColour (button.isEnabled() ? Palette::textPrimary : Palette::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (button.getButtonText(), bounds.reduced (6.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }
}
