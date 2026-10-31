#include "Theme.h"

namespace dooblrsoothe::ui
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

    void DooblrLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        // A slider in a wider column (Width, for its long readout) can cap its knob so every knob matches.
        const auto maxDiameter = (float) (double) slider.getProperties().getWithDefault ("maxKnobDiameter", 1.0e6);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight(), maxDiameter - 8.0f) * 0.5f;
        const auto centre = bounds.getCentre();
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

        if (! isDimmed (slider))
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
        g.setColour (! isDimmed (slider) ? Palette::textPrimary : Palette::textMuted);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.15f, angle), tip }, 2.0f);
    }

    void DooblrLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                              float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto faderScaleShift = (bool) slider.getProperties().getWithDefault ("faderScale", false) ? 5.0f : 0.0f;
        const auto cx = area.getCentreX() + faderScaleShift; // room for the dB labels on the left
        const auto fill = slider.findColour (juce::Slider::trackColourId);
        const auto top = area.getY(), bottom = area.getBottom();

        const auto range = slider.getRange();
        const auto faderScale = (bool) slider.getProperties().getWithDefault ("faderScale", false);
        auto capWidth = juce::jmin (30.0f, area.getWidth() - 4.0f);

        if (faderScale)
        {
            // v0.3.5 layer fader: labelled dB marks for the console taper, labels left of the
            // groove, ticks both sides; 0 dB longer and brighter.
            capWidth = juce::jmin (22.0f, area.getWidth() - 4.0f);
            g.setFont (juce::FontOptions (9.0f));
            for (const auto v : { 12.0, 6.0, 0.0, -6.0, -12.0, -24.0, -48.0, range.getStart() })
            {
                if (v > range.getEnd() + 1.0e-6 || v < range.getStart() - 1.0e-6)
                    continue;
                const auto ty = juce::jmap ((float) slider.valueToProportionOfLength (v), 0.0f, 1.0f, bottom, top);
                const auto zero = std::abs (v) < 1.0e-6;
                const auto len = zero ? 7.0f : 4.0f;
                g.setColour (zero ? Palette::textMuted : Palette::divider.brighter (0.2f));
                g.fillRect (juce::Rectangle<float> (cx - capWidth * 0.5f - len + 2.0f, ty - 0.5f, len, 1.0f));
                g.fillRect (juce::Rectangle<float> (cx + capWidth * 0.5f - 2.0f, ty - 0.5f, len, 1.0f));
                const auto text = v <= range.getStart() + 1.0e-6 ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"))
                                                                  : (v > 0.0 ? "+" : "") + juce::String ((int) v);
                g.setColour (zero ? Palette::textPrimary : Palette::textMuted);
                g.drawText (text, juce::Rectangle<float> (area.getX(), ty - 6.0f, cx - capWidth * 0.5f - len - area.getX(), 12.0f),
                            juce::Justification::centredRight, false);
            }
        }

        // Scale ticks (every 12 dB for a dB range), drawn left and right of the groove.
        g.setColour (Palette::divider);
        for (auto v = std::ceil (range.getStart() / 12.0) * 12.0; ! faderScale && v <= range.getEnd() + 1.0e-6; v += 12.0)
        {
            const auto ty = juce::jmap ((float) slider.valueToProportionOfLength (v), 0.0f, 1.0f, bottom, top);
            const auto len = std::abs (v) < 1.0e-6 ? 9.0f : 5.0f;
            g.fillRect (juce::Rectangle<float> (cx - 7.0f - len, ty - 0.5f, len, 1.0f));
            g.fillRect (juce::Rectangle<float> (cx + 7.0f, ty - 0.5f, len, 1.0f));
        }

        const auto groove = juce::Rectangle<float> (cx - 2.5f, top, 5.0f, area.getHeight());
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.fillRoundedRectangle (groove, 2.5f);

        if (! isDimmed (slider))
        {
            g.setColour (fill);
            g.fillRoundedRectangle (groove.withTop (sliderPos), 2.5f);
        }

        const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ cx, sliderPos });
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (slider.isMouseOverOrDragging() ? fill : Palette::divider.brighter (0.4f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (! isDimmed (slider) ? Palette::textPrimary : Palette::textMuted);
        g.fillRect (juce::Rectangle<float> (cap.getWidth() - 10.0f, 1.5f).withCentre (cap.getCentre()));
    }

    juce::Label* DooblrLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setMinimumHorizontalScale (0.6f);
        return label;
    }

    void DooblrLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool highlighted, bool down)
    {
        juce::ignoreUnused (down);

        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto on = button.getToggleState();

        auto pill = bounds.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 14.0f);
        g.setColour (on ? Palette::accent.withAlpha (! isDimmed (button) ? 0.9f : 0.4f) : Palette::divider);
        g.fillRoundedRectangle (pill, 7.0f);

        const auto knob = juce::Rectangle<float> (10.0f, 10.0f)
                              .withCentre ({ on ? pill.getRight() - 7.0f : pill.getX() + 7.0f, pill.getCentreY() });
        g.setColour (highlighted ? Palette::textPrimary : Palette::textPrimary.withAlpha (0.85f));
        g.fillEllipse (knob);

        g.setColour (! isDimmed (button) ? Palette::textPrimary : Palette::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (button.getButtonText(), bounds.reduced (6.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }

    void setDimmed (juce::Component& c, bool dimmed)
    {
        if ((bool) c.getProperties().getWithDefault ("dimmed", false) == dimmed)
            return;

        c.getProperties().set ("dimmed", dimmed);

        // A knob's value box greys out with it.
        if (auto* slider = dynamic_cast<juce::Slider*> (&c))
        {
            if (dimmed)
                slider->setColour (juce::Slider::textBoxTextColourId, Palette::textMuted);
            else
                slider->removeColour (juce::Slider::textBoxTextColourId);
        }

        // Plain buttons (e.g. the voice-count presets) fade.
        if (dynamic_cast<juce::TextButton*> (&c) != nullptr)
            c.setAlpha (dimmed ? 0.45f : 1.0f);

        // Combo boxes (e.g. a division while free-running) fade a little less.
        if (dynamic_cast<juce::ComboBox*> (&c) != nullptr)
            c.setAlpha (dimmed ? 0.55f : 1.0f);

        c.repaint();
    }
}
