#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace dooblrkick::ui
{
    /** v0.3.5: an eased 0..1 position for a switch thumb (VBlank driven, time based). */
    struct ThumbAnimation
    {
        float position = 0.0f, from = 0.0f, to = 0.0f;
        double startMs = -1.0, durationMs = 180.0;
        bool running = false;

        void jumpTo (float target) noexcept { position = from = to = target; running = false; }
        void animateTo (float target) noexcept;
        /** Moves towards the target; returns true while it is still moving. */
        bool advance (double nowMs) noexcept;
    };

    /**
        v0.3.5: the Body envelope view switch: a pill with both labels ("PITCH" | "AMP") and a
        thumb that slides (ease-out, about 180 ms) to the active side, tinted in that view's curve
        colour. Click anywhere to switch, or drag the thumb across; Space / Return toggle, Left /
        Right pick a side. A view setting (not a parameter): no undo, not automatable.
    */
    class SlideToggle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SlideToggle (juce::String leftText, juce::String rightText, juce::Colour leftColour, juce::Colour rightColour);
        ~SlideToggle() override;

        bool isRight() const noexcept { return right; }
        /** Selects a side; notify calls onChange (only when it changed). */
        void setRight (bool shouldBeRight, bool notify, bool animate = true);
        std::function<void()> onChange;
        /** v0.3.6: the side colours (the layer pages tint AMP in the layer's curve colour). */
        void setColours (juce::Colour newLeft, juce::Colour newRight);
        juce::Colour getRightColour() const noexcept { return rightColour; }

        float getThumbPosition() const noexcept { return thumb.position; } // 0 left .. 1 right
        bool isAnimating() const noexcept { return thumb.running; }
        void advanceAnimation (double nowMs);

        juce::Rectangle<float> getTrackBounds() const;
        juce::Rectangle<float> getThumbBounds() const;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        static constexpr int preferredWidth = 132, preferredHeight = 22;

    private:
        juce::String leftText, rightText;
        juce::Colour leftColour, rightColour;
        bool right = false, dragging = false;
        float dragStartThumb = 0.0f;
        ThumbAnimation thumb;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };

    /**
        v0.3.5: a layer's Key Track switch: a medium vertical slide switch under the layer's
        fader, captioned "KEY", thumb up = on (the MIDI note transposes the layer relative to
        the Key). A ToggleButton, so an APVTS ButtonAttachment binds it (host automation, one undo
        step per click, keyboard and accessibility as a toggle).
    */
    class KeyTrackSwitch final : public juce::ToggleButton
    {
    public:
        explicit KeyTrackSwitch (juce::Colour colour);
        ~KeyTrackSwitch() override;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void buttonStateChanged() override { repaint(); }

        float getThumbPosition() const noexcept { return thumb.position; } // 0 down (off) .. 1 up (on)
        void advanceAnimation (double nowMs);
        juce::Rectangle<float> getTrackBounds() const;

        static constexpr int width = 56, height = 62;

    private:
        juce::Colour colour;
        ThumbAnimation thumb;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };

    /** v0.3.5: the Play button: a right-pointing triangle (no text; tooltip "Play"). */
    class PlayIconButton final : public juce::Button
    {
    public:
        PlayIconButton();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        /** The triangle, centred in the given box. */
        static juce::Path makeIcon (juce::Rectangle<float> box);
    };

    /**
        v0.3.5: the draggable divider between the envelope section and the waveform: a vertical
        bar with a dotted grip that brightens on hover / drag (left-right resize cursor). It
        reports drags (pixels from the drag start) and a double-click (reset); the editor owns
        the ratio.
    */
    class SplitterBar final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SplitterBar();

        std::function<void()> onDragStart, onReset;
        std::function<void (int deltaX)> onDrag;

        bool isHot() const noexcept { return hot || dragging; }

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }

        static constexpr int width = 12;

    private:
        bool hot = false, dragging = false;
    };
}
