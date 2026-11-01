#include "Theme.h"

namespace dooblr::installer::ui
{
    LookAndFeel::LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
        setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, Palette::accent);
        setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
        setColour (juce::TextButton::textColourOnId, Palette::background);
        setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
        setColour (juce::ComboBox::outlineColourId, Palette::divider);
        setColour (juce::ComboBox::textColourId, Palette::textPrimary);
        setColour (juce::ComboBox::arrowColourId, Palette::textMuted);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panelRaised);
        setColour (juce::PopupMenu::textColourId, Palette::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::textPrimary);
        setColour (juce::TextEditor::backgroundColourId, Palette::panel);
        setColour (juce::TextEditor::textColourId, Palette::textPrimary);
        setColour (juce::TextEditor::outlineColourId, Palette::divider);
        setColour (juce::TextEditor::focusedOutlineColourId, Palette::divider);
        setColour (juce::ScrollBar::thumbColourId, Palette::divider);
        setColour (juce::Label::textColourId, Palette::textPrimary);
        setColour (juce::ToggleButton::textColourId, Palette::textPrimary);
        setColour (juce::ProgressBar::foregroundColourId, Palette::accent);
        setColour (juce::ProgressBar::backgroundColourId, Palette::panelRaised);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : base;
        if (! b.isEnabled())
            c = c.withMultipliedAlpha (0.45f);
        else if (down)
            c = c.brighter (0.15f);
        else if (highlighted)
            c = c.brighter (0.07f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::divider.withAlpha (b.getToggleState() ? 0.0f : 1.0f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) h * 0.5f), juce::Font::bold));
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        const auto box = b.getLocalBounds().removeFromLeft (b.getHeight()).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
        const auto on = b.getToggleState();
        const auto alpha = b.isEnabled() ? 1.0f : 0.4f;

        g.setColour ((on ? Palette::accent : Palette::panelRaised).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour ((on ? Palette::accent : (highlighted ? Palette::textMuted : Palette::divider.brighter (0.3f))).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);

        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY() + 0.5f);
            tick.lineTo (box.getX() + 7.8f, box.getBottom() - 5.0f);
            tick.lineTo (box.getRight() - 4.0f, box.getY() + 5.0f);
            g.setColour (Palette::background.withMultipliedAlpha (alpha));
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (b.getButtonText().isNotEmpty())
        {
            g.setColour (Palette::textPrimary.withMultipliedAlpha (alpha));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (b.getHeight() + 4), juce::Justification::centredLeft);
        }
    }

    void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double p, const juce::String& text)
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        const auto bar = r.removeFromTop (8.0f);
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, p)), 4.0f);
        if (text.isNotEmpty())
        {
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (text, r.withTrimmedTop (12.0f), juce::Justification::centredLeft);
        }
    }
}
