#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblrq::ui
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
    }

    /** Colour used to identify a band in the display and band selector. */
    juce::Colour bandColour (int bandIndex);

    /** Where a rotary knob is drawn inside its slider (slider coordinates). A knob carrying
        the "rangeRing" property (0.9.0) is drawn smaller so the outer Range ring fits around it. */
    struct KnobGeometry
    {
        juce::Point<float> centre;
        float outerRadius = 0.0f;  // the space the knob had before 0.9.0
        float knobRadius  = 0.0f;  // the knob as drawn
        float ringRadius  = 0.0f;  // centre line of the Range ring (when present)
        float ringWidth   = 0.0f;  // stroke width of the Range ring
    };

    KnobGeometry knobGeometry (juce::Slider& slider);

    /** Flat, understated look for knobs, buttons and combo boxes. */
    class DooblrLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        DooblrLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };
}
