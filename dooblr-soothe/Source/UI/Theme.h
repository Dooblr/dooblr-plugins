#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblrsoothe::ui
{
    namespace Palette
    {
        inline const juce::Colour background  { 0xff16181c };
        inline const juce::Colour panel       { 0xff1d2026 };
        inline const juce::Colour panelRaised { 0xff252931 };
        inline const juce::Colour divider     { 0xff2c3038 };
        inline const juce::Colour grid        { 0xff262a31 };
        inline const juce::Colour textPrimary { 0xffe6e8eb };
        inline const juce::Colour textMuted   { 0xff8a919c };
        inline const juce::Colour accent      { 0xff5fb3a1 };
        inline const juce::Colour dynamic     { 0xffd9a35b };
        inline const juce::Colour spectrum    { 0xff9fb4c8 };
        inline const juce::Colour frozen      { 0xffe58fc0 };
        inline const juce::Colour master      { 0xff8fa8e8 }; // the Envelope page (master envelope, dry / wet)
    }

    /** Dimmed: shown greyed out because the current mode ignores it, but still live, so it can be
        clicked, dragged and automated (0.1.4: dimming no longer disables the control). */
    inline bool isDimmed (const juce::Component& c)
    {
        return ! c.isEnabled() || (bool) c.getProperties().getWithDefault ("dimmed", false);
    }

    /** Sets the dimmed look (see isDimmed); the control stays enabled and clickable. */
    void setDimmed (juce::Component& c, bool dimmed);

    /** Colour used to identify a band in the display and band selector. */
    juce::Colour bandColour (int bandIndex);

    /** Flat, understated look for knobs, buttons and combo boxes. */
    class DooblrLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        DooblrLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        /** Value boxes squeeze long readouts (e.g. "12.0 kHz") rather than truncating them. */
        juce::Label* createSliderTextBox (juce::Slider&) override;

        /** Vertical sliders (the layer Level faders, v0.3.3): a slim groove filled from the bottom
            and a flat cap. Other linear styles use the default look. */
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    };
}
