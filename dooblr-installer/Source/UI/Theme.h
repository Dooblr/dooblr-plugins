#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblr::installer::ui
{
    /** The suite palette (design system §2), shared with the plug-ins. */
    namespace Palette
    {
        inline const juce::Colour background  { 0xff16181c };
        inline const juce::Colour panel       { 0xff1d2026 };
        inline const juce::Colour panelRaised { 0xff252931 };
        inline const juce::Colour divider     { 0xff2c3038 };
        inline const juce::Colour textPrimary { 0xffe6e8eb };
        inline const juce::Colour textMuted   { 0xff8a919c };
        inline const juce::Colour accent      { 0xff5fb3a1 };
        inline const juce::Colour dynamic     { 0xffd9a35b };
        inline const juce::Colour danger      { 0xffe0574f };
    }

    /** Flat buttons, square check boxes and a thin progress bar in the suite style. */
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String& text) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    };
}
