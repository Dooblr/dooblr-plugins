#include "Switches.h"

#include "Theme.h"

#include <cmath>

namespace dooblrkick::ui
{
    //==============================================================================
    void ThumbAnimation::animateTo (float target) noexcept
    {
        if (juce::approximatelyEqual (target, to) && (running || juce::approximatelyEqual (position, target)))
            return;

        from = position;
        to = target;
        startMs = -1.0;
        running = true;
    }

    bool ThumbAnimation::advance (double nowMs) noexcept
    {
        if (! running)
            return false;

        if (startMs < 0.0)
            startMs = nowMs;

        const auto t = durationMs > 0.0 ? juce::jlimit (0.0, 1.0, (nowMs - startMs) / durationMs) : 1.0;
        const auto eased = (float) (1.0 - std::pow (1.0 - t, 3.0)); // ease-out cubic
        position = from + (to - from) * eased;

        if (t >= 1.0)
        {
            position = to;
            running = false;
        }

        return true;
    }

    //==============================================================================
    namespace
    {
        class SlideToggleAccessibility final : public juce::AccessibilityHandler
        {
        public:
            explicit SlideToggleAccessibility (SlideToggle& t)
                : juce::AccessibilityHandler (t, juce::AccessibilityRole::toggleButton,
                                              juce::AccessibilityActions()
                                                  .addAction (juce::AccessibilityActionType::toggle, [&t] { t.setRight (! t.isRight(), true); })
                                                  .addAction (juce::AccessibilityActionType::press, [&t] { t.setRight (! t.isRight(), true); })),
                  toggle (t)
            {
            }

            juce::AccessibleState getCurrentState() const override
            {
                auto s = juce::AccessibilityHandler::getCurrentState().withCheckable();
                return toggle.isRight() ? s.withChecked() : s;
            }

        private:
            SlideToggle& toggle;
        };
    }

    SlideToggle::SlideToggle (juce::String left, juce::String rightLabel, juce::Colour lc, juce::Colour rc)
        : leftText (std::move (left)), rightText (std::move (rightLabel)), leftColour (lc), rightColour (rc)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (leftText + " / " + rightText);
        setDescription ("Shows the " + leftText.toLowerCase() + " or the " + rightText.toLowerCase() + " envelope");
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { advanceAnimation (juce::Time::getMillisecondCounterHiRes()); });
    }

    SlideToggle::~SlideToggle() = default;

    void SlideToggle::setColours (juce::Colour newLeft, juce::Colour newRight)
    {
        if (newLeft == leftColour && newRight == rightColour)
            return;

        leftColour = newLeft;
        rightColour = newRight;
        repaint();
    }

    void SlideToggle::setRight (bool shouldBeRight, bool notify, bool animate)
    {
        const auto changed = shouldBeRight != right;
        right = shouldBeRight;

        if (animate)
            thumb.animateTo (right ? 1.0f : 0.0f);
        else
            thumb.jumpTo (right ? 1.0f : 0.0f);

        repaint();

        if (changed)
        {
            if (auto* h = getAccessibilityHandler())
                h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);

            if (notify && onChange)
                onChange();
        }
    }

    void SlideToggle::advanceAnimation (double nowMs)
    {
        if (thumb.advance (nowMs))
            repaint();
    }

    juce::Rectangle<float> SlideToggle::getTrackBounds() const
    {
        return getLocalBounds().toFloat().reduced (1.0f);
    }

    juce::Rectangle<float> SlideToggle::getThumbBounds() const
    {
        const auto track = getTrackBounds();
        const auto half = track.getWidth() * 0.5f;
        return juce::Rectangle<float> (track.getX() + thumb.position * half, track.getY(), half, track.getHeight()).reduced (2.0f);
    }

    void SlideToggle::paint (juce::Graphics& g)
    {
        const auto track = getTrackBounds();
        const auto radius = track.getHeight() * 0.5f;
        const auto mix = juce::jlimit (0.0f, 1.0f, thumb.position);
        const auto thumbColour = leftColour.interpolatedWith (rightColour, mix);

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (track, radius);
        g.setColour (hasKeyboardFocus (false) ? thumbColour.withAlpha (0.9f) : Palette::grid.brighter (0.25f));
        g.drawRoundedRectangle (track, radius, 1.0f);

        const auto thumbBox = getThumbBounds();
        g.setColour (thumbColour.withAlpha (isEnabled() ? 0.9f : 0.35f));
        g.fillRoundedRectangle (thumbBox, thumbBox.getHeight() * 0.5f);

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const auto half = track.getWidth() * 0.5f;
        const auto leftBox = track.withWidth (half), rightBox = track.withTrimmedLeft (half);
        // Over the thumb: dark text on the colour; the other side muted.
        g.setColour (Palette::background.interpolatedWith (Palette::textMuted, mix));
        g.drawText (leftText, leftBox, juce::Justification::centred);
        g.setColour (Palette::textMuted.interpolatedWith (Palette::background, mix));
        g.drawText (rightText, rightBox, juce::Justification::centred);
    }

    void SlideToggle::mouseDown (const juce::MouseEvent&)
    {
        dragging = false;
        dragStartThumb = thumb.position;
    }

    void SlideToggle::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging && std::abs (e.getDistanceFromDragStartX()) < 4)
            return;

        dragging = true;
        const auto travel = juce::jmax (1.0f, getTrackBounds().getWidth() * 0.5f);
        thumb.running = false;
        thumb.position = juce::jlimit (0.0f, 1.0f, dragStartThumb + (float) e.getDistanceFromDragStartX() / travel);
        thumb.to = thumb.position;
        repaint();
    }

    void SlideToggle::mouseUp (const juce::MouseEvent& e)
    {
        if (dragging)
        {
            dragging = false;
            const auto target = thumb.position > 0.5f;
            thumb.to = -1.0f; // force the settle animation from where the thumb was dropped
            setRight (target, true);
            return;
        }

        if (getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu())
            setRight (! right, true); // a click anywhere switches
    }

    bool SlideToggle::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
            setRight (! right, true);
        else if (key == juce::KeyPress::leftKey)
            setRight (false, true);
        else if (key == juce::KeyPress::rightKey)
            setRight (true, true);
        else
            return false;

        return true;
    }

    std::unique_ptr<juce::AccessibilityHandler> SlideToggle::createAccessibilityHandler()
    {
        return std::make_unique<SlideToggleAccessibility> (*this);
    }

    //==============================================================================
    KeyTrackSwitch::KeyTrackSwitch (juce::Colour c) : colour (c)
    {
        setButtonText ("Key");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        thumb.durationMs = 140.0;
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { advanceAnimation (juce::Time::getMillisecondCounterHiRes()); });
    }

    KeyTrackSwitch::~KeyTrackSwitch() = default;

    void KeyTrackSwitch::advanceAnimation (double nowMs)
    {
        const auto target = getToggleState() ? 1.0f : 0.0f;

        if (! juce::approximatelyEqual (thumb.to, target))
        {
            if (isShowing())
                thumb.animateTo (target);
            else
                thumb.jumpTo (target);
        }

        if (thumb.advance (nowMs))
            repaint();
    }

    juce::Rectangle<float> KeyTrackSwitch::getTrackBounds() const
    {
        // A medium vertical pill (18 x 36) under the caption.
        auto area = getLocalBounds().toFloat();
        area.removeFromTop (16.0f);
        return juce::Rectangle<float> (18.0f, juce::jmin (36.0f, area.getHeight() - 4.0f)).withCentre (area.getCentre());
    }

    void KeyTrackSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        // The drawn thumb follows the state even before the first VBlank (e.g. snapshots).
        const auto target = getToggleState() ? 1.0f : 0.0f;
        const auto pos = thumb.running || juce::approximatelyEqual (thumb.to, target) ? thumb.position : target;
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.35f;

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour ((getToggleState() && enabled ? colour : Palette::textMuted).withMultipliedAlpha (alpha));
        g.drawText ("KEY", getLocalBounds().removeFromTop (14), juce::Justification::centred);

        const auto track = getTrackBounds();
        const auto radius = track.getWidth() * 0.5f;
        g.setColour (Palette::grid.interpolatedWith (colour.withAlpha (0.55f), pos).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, radius);
        g.setColour ((highlighted && enabled ? Palette::textMuted : Palette::grid.brighter (0.3f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (track, radius, 1.0f);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (track.expanded (2.0f), radius + 2.0f, 1.0f);
        }

        // Thumb: up = on.
        const auto knob = track.getWidth() - 4.0f;
        const auto travel = track.getHeight() - 4.0f - knob;
        const auto y = track.getBottom() - 2.0f - knob - travel * pos;
        auto knobBox = juce::Rectangle<float> (track.getX() + 2.0f, y, knob, knob);
        if (down && enabled)
            knobBox = knobBox.reduced (0.75f);
        g.setColour ((pos > 0.5f ? Palette::textPrimary : Palette::textMuted).withMultipliedAlpha (alpha));
        g.fillEllipse (knobBox);

        if (! enabled)
        {
            // Disabled look: a slash across the track.
            g.setColour (Palette::textMuted.withAlpha (0.6f));
            g.drawLine (track.getX() - 3.0f, track.getBottom() - 4.0f, track.getRight() + 3.0f, track.getY() + 4.0f, 1.2f);
        }
    }

    //==============================================================================
    PlayIconButton::PlayIconButton() : juce::Button ("Play")
    {
        setButtonText ({}); // an icon, no text
        setTitle ("Play");
        setTooltip ("Play");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    juce::Path PlayIconButton::makeIcon (juce::Rectangle<float> box)
    {
        // An equilateral-ish right triangle, nudged right so it looks optically centred.
        const auto h = juce::jmin (box.getHeight() * 0.5f, box.getWidth() * 0.5f);
        const auto w = h * 0.9f;
        const auto c = box.getCentre().translated (w * 0.08f, 0.0f);
        juce::Path p;
        p.addTriangle (c.x - w * 0.5f, c.y - h * 0.5f, c.x - w * 0.5f, c.y + h * 0.5f, c.x + w * 0.5f, c.y);
        return p;
    }

    void PlayIconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto& lf = getLookAndFeel();
        lf.drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

        const auto box = getLocalBounds().toFloat();
        const auto icon = makeIcon (box);
        g.setColour (! isEnabled() ? Palette::textMuted.withAlpha (0.4f)
                     : down        ? Palette::accent.darker (0.2f)
                     : highlighted ? Palette::accent
                                   : Palette::textPrimary);
        g.fillPath (icon);
        g.strokePath (icon, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); // soft corners
    }

    //==============================================================================
    SplitterBar::SplitterBar()
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTitle ("Resize the envelope and waveform sections");
        setTooltip ("Drag to resize; double-click to reset");
    }

    void SplitterBar::mouseDown (const juce::MouseEvent&)
    {
        dragging = true;
        if (onDragStart) onDragStart();
        repaint();
    }

    void SplitterBar::mouseDrag (const juce::MouseEvent& e)
    {
        if (onDrag) onDrag (e.getDistanceFromDragStartX());
    }

    void SplitterBar::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        const auto active = isHot();
        const auto cx = area.getCentreX();

        // A thin full-height rule, brighter while hot.
        g.setColour (active ? Palette::accent.withAlpha (0.35f) : Palette::grid);
        g.fillRoundedRectangle (juce::Rectangle<float> (active ? 3.0f : 1.0f, area.getHeight() - 8.0f).withCentre ({ cx, area.getCentreY() }), 1.5f);

        // The grip: a column of dots on a small pill in the middle.
        const auto grip = juce::Rectangle<float> (8.0f, 34.0f).withCentre (area.getCentre());
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (grip, 4.0f);
        g.setColour (active ? Palette::accent : Palette::textMuted.withAlpha (0.8f));
        for (int i = 0; i < 5; ++i)
            g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ cx, grip.getY() + 5.0f + (float) i * 6.0f }));
    }
}
