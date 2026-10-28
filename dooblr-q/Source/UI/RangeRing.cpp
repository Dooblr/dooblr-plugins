#include "RangeRing.h"

#include <cmath>

namespace dooblrq::ui
{
    RangeRing::RangeRing()
    {
        setComponentID ("rangeRing");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (false);
        updateTooltip();
    }

    RangeRing::~RangeRing()
    {
        attach (nullptr);
    }

    void RangeRing::attach (juce::RangedAudioParameter* rangeParameter)
    {
        if (gestureActive && attachment != nullptr)
            attachment->endGesture();

        gestureActive = false;
        dragMode = DragMode::undecided;
        attachment.reset();

        if (rangeParameter != nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*rangeParameter,
                                                                      [this] (float value) { parameterChanged (value); });
            attachment->sendInitialUpdate();
        }
    }

    void RangeRing::parameterChanged (float newValue)
    {
        if (juce::approximatelyEqual (rangeDb, (double) newValue))
            return;

        rangeDb = (double) newValue;
        updateTooltip();
        repaint();
    }

    void RangeRing::setTarget (juce::Slider* knob)
    {
        if (target == knob)
            return;

        if (target != nullptr)
        {
            target->getProperties().remove ("rangeRing");
            target->repaint();
        }

        target = knob;

        if (target != nullptr)
        {
            target->getProperties().set ("rangeRing", true);
            target->repaint();
        }

        placeAround();
        repaint();
    }

    void RangeRing::placeAround()
    {
        if (target == nullptr)
        {
            setBounds ({});
            return;
        }

        setBounds (target->getBounds().expanded (margin));
    }

    void RangeRing::setMapping (std::function<double (double)> toKnobValue)
    {
        mapping = std::move (toKnobValue);

        // Called on every UI tick: only repaint when the drawn arc would move.
        const auto from = mapping ? mapping (0.0) : 0.0, to = mapping ? mapping (rangeDb) : 0.0;

        if (! juce::approximatelyEqual (from, lastMappedFrom) || ! juce::approximatelyEqual (to, lastMappedTo))
        {
            lastMappedFrom = from;
            lastMappedTo = to;
            repaint();
        }
    }

    void RangeRing::setLiveAmount (double dynamicDb)
    {
        if (! std::isfinite (dynamicDb))
            dynamicDb = 0.0;

        if (std::abs (dynamicDb - liveDb) < 0.01)
            return;

        liveDb = dynamicDb;
        repaint();
    }

    void RangeRing::setRingColour (juce::Colour colour)
    {
        if (colour == ringColour)
            return;

        ringColour = colour;
        repaint();
    }

    void RangeRing::setMovesCutoff (bool movesCutoff, bool isHighCut)
    {
        if (cutoff == movesCutoff && highCut == isHighCut)
            return;

        cutoff = movesCutoff;
        highCut = isHighCut;
        updateTooltip();
    }

    void RangeRing::updateTooltip()
    {
        const auto amount = (rangeDb > 0.0 ? "+" : "") + juce::String (rangeDb, 1) + " dB";
        juce::String effect;

        if (cutoff)
        {
            const auto octaves = std::abs (rangeDb) / 6.0;
            const auto narrows = rangeDb >= 0.0;
            const auto up = highCut ? ! narrows : narrows;
            effect = "louder signal moves the cutoff " + juce::String (up ? "up" : "down") + " by up to "
                   + juce::String (octaves, 2) + " oct (" + (narrows ? "closing" : "opening")
                   + " the filter; 1 dB = 1/6 oct)";
        }
        else
        {
            effect = "louder signal " + juce::String (rangeDb >= 0.0 ? "boosts" : "cuts") + " the band by up to "
                   + juce::String (std::abs (rangeDb), 1) + " dB";
        }

        const auto upward = rangeDb * upwardSign();
        const auto kind = juce::approximatelyEqual (upward, 0.0) ? juce::String ("off")
                                                                  : juce::String (upward > 0.0 ? "upward expansion" : "downward compression");
        setTooltip ("Dynamic Range " + amount + " (" + kind + "): " + effect + ". Drag up / clockwise = upward expansion, "
                    "down / counter-clockwise = downward compression; Shift = fine, double-click = 0 dB.");
    }

    //==============================================================================
    RangeRing::DragMode RangeRing::chooseDragMode (juce::Point<float> centre, juce::Point<float> start,
                                                   juce::Point<float> current) noexcept
    {
        const auto d = current - start;
        const auto distance = d.getDistanceFromOrigin();

        if (! (distance >= decisionDistance))
            return DragMode::undecided;

        // Mostly up/down always means vertical (up = more), wherever on the ring it starts, so
        // the familiar knob gesture never turns into a rotation by accident.
        if (std::abs (d.y) >= 2.0f * std::abs (d.x))
            return DragMode::vertical;

        const auto radial = start - centre;
        const auto radialLength = radial.getDistanceFromOrigin();

        if (radialLength < 1.0f)
            return DragMode::vertical;

        const auto u = radial / radialLength;
        const auto tangential = std::abs (d.x * u.y - d.y * u.x); // |d x u|

        return tangential >= 0.6f * distance ? DragMode::rotary : DragMode::vertical;
    }

    double RangeRing::verticalDeltaDb (double dyPixels, bool fine) noexcept
    {
        return -dyPixels * ((maxRangeDb - minRangeDb) / pixelsForFullSpan) * (fine ? fineFactor : 1.0);
    }

    double RangeRing::clockwiseAngle (juce::Point<float> centre, juce::Point<float> a, juce::Point<float> b) noexcept
    {
        // Angle clockwise from 12 o'clock, the convention JUCE uses for rotary sliders.
        const auto angleOf = [centre] (juce::Point<float> p)
        {
            return std::atan2 ((double) (p.x - centre.x), (double) (centre.y - p.y));
        };

        auto delta = angleOf (b) - angleOf (a);
        constexpr auto pi = juce::MathConstants<double>::pi;

        while (delta > pi)   delta -= 2.0 * pi;
        while (delta <= -pi) delta += 2.0 * pi;

        return std::isfinite (delta) ? delta : 0.0;
    }

    double RangeRing::rotaryDeltaDb (double radians, double sweepRadians, bool fine) noexcept
    {
        if (! (sweepRadians > 0.0))
            return 0.0;

        return radians / sweepRadians * (maxRangeDb - minRangeDb) * (fine ? fineFactor : 1.0);
    }

    //==============================================================================
    juce::Point<float> RangeRing::getCentre() const
    {
        if (target == nullptr)
            return getLocalBounds().toFloat().getCentre();

        return getLocalPoint (target, knobGeometry (*target).centre);
    }

    float RangeRing::getRingRadius() const
    {
        return target != nullptr ? knobGeometry (*target).ringRadius : 0.0f;
    }

    float RangeRing::getInnerHitRadius() const
    {
        return target != nullptr ? knobGeometry (*target).knobRadius + 1.0f : 0.0f;
    }

    float RangeRing::getOuterHitRadius() const
    {
        if (target == nullptr)
            return 0.0f;

        const auto g = knobGeometry (*target);
        // Generous: the stroke plus a few pixels outside it, but never past our own bounds.
        return juce::jmin (g.ringRadius + g.ringWidth * 0.5f + 6.0f,
                           (float) juce::jmin (getWidth(), getHeight()) * 0.5f);
    }

    bool RangeRing::hitTest (int x, int y)
    {
        if (target == nullptr || ! target->isVisible())
            return false;

        const auto distance = getCentre().getDistanceFrom ({ (float) x + 0.5f, (float) y + 0.5f });
        return distance >= getInnerHitRadius() && distance <= getOuterHitRadius();
    }

    float RangeRing::angleForKnobValue (double value) const
    {
        const auto rotary = target->getRotaryParameters();
        const auto clamped = juce::jlimit (target->getMinimum(), target->getMaximum(), std::isfinite (value) ? value : target->getValue());
        const auto proportion = (float) target->valueToProportionOfLength (clamped);
        return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
    }

    void RangeRing::paint (juce::Graphics& g)
    {
        if (target == nullptr)
            return;

        const auto geometry = knobGeometry (*target);
        const auto centre = getCentre();
        const auto r = geometry.ringRadius, w = geometry.ringWidth;
        const auto rotary = target->getRotaryParameters();
        const auto enabled = isEnabled();
        const auto map = [this] (double db) { return mapping ? mapping (db) : target->getValue() + db; };

        const auto arc = [&] (float from, float to)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
            return p;
        };
        const auto stroke = juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

        // Track over the knob's sweep; a soft glow in the band colour while hovered.
        const auto track = arc (rotary.startAngleRadians, rotary.endAngleRadians);

        if (hovered && enabled)
        {
            g.setColour (ringColour.withAlpha (0.18f));
            g.strokePath (track, juce::PathStrokeType (w + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (Palette::divider.brighter (hovered && enabled ? 0.35f : 0.1f).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.strokePath (track, stroke);

        // Excursion: from the knob's value to where the full Range takes it.
        const auto fromAngle = angleForKnobValue (map (0.0));
        const auto toAngle = angleForKnobValue (map (rangeDb));
        const auto colour = enabled ? ringColour.withAlpha (hovered || gestureActive ? 1.0f : 0.85f)
                                    : Palette::textMuted.withAlpha (0.35f);

        if (std::abs (toAngle - fromAngle) > 0.002f)
        {
            g.setColour (colour);
            g.strokePath (arc (fromAngle, toAngle), stroke);

            // A tick at the far end shows which way it goes.
            const auto outer = centre.getPointOnCircumference (r + w * 0.5f + 2.0f, toAngle);
            const auto inner = centre.getPointOnCircumference (r - w * 0.5f - 2.0f, toAngle);
            g.drawLine ({ inner, outer }, 2.0f);
        }

        // Zero mark at the knob's own value.
        g.setColour (enabled ? Palette::textPrimary.withAlpha (0.7f) : Palette::textMuted.withAlpha (0.35f));
        g.drawLine ({ centre.getPointOnCircumference (r - w * 0.5f, fromAngle),
                      centre.getPointOnCircumference (r + w * 0.5f, fromAngle) }, 1.5f);

        // Live dynamic amount.
        if (enabled && std::abs (liveDb) > 0.05)
        {
            const auto liveAngle = angleForKnobValue (map (liveDb));
            const auto dot = centre.getPointOnCircumference (r, liveAngle);
            g.setColour (Palette::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (w * 1.3f, w * 1.3f).withCentre (dot));
        }
    }

    //==============================================================================
    void RangeRing::mouseEnter (const juce::MouseEvent&)
    {
        hovered = true;
        repaint();
    }

    void RangeRing::mouseExit (const juce::MouseEvent&)
    {
        hovered = false;
        repaint();
    }

    void RangeRing::mouseDown (const juce::MouseEvent& event)
    {
        downPosition = lastPosition = event.position;
        dragMode = DragMode::undecided;
        dragValue = rangeDb;
    }

    void RangeRing::mouseDrag (const juce::MouseEvent& event)
    {
        if (attachment == nullptr || target == nullptr)
            return;

        if (dragMode == DragMode::undecided)
        {
            dragMode = chooseDragMode (getCentre(), downPosition, event.position);

            if (dragMode == DragMode::undecided)
                return;

            attachment->beginGesture();
            gestureActive = true;
            lastPosition = downPosition; // the first few pixels count too
        }

        const auto fine = event.mods.isShiftDown();
        double delta = 0.0;

        if (dragMode == DragMode::vertical)
        {
            delta = verticalDeltaDb ((double) (event.position.y - lastPosition.y), fine);
        }
        else
        {
            const auto rotary = target->getRotaryParameters();
            delta = rotaryDeltaDb (clockwiseAngle (getCentre(), lastPosition, event.position),
                                   (double) (rotary.endAngleRadians - rotary.startAngleRadians), fine);
        }

        lastPosition = event.position;
        // 0.9.2: the gesture is in "upward amount" (up / clockwise = upward expansion, down /
        // counter-clockwise = downward compression), the same as the graph's hover ring. For
        // cuts positive Range closes the filter (downward), so the direction flips there.
        setRangeFromUi (dragValue + delta * upwardSign());
    }

    void RangeRing::setRangeFromUi (double newRangeDb)
    {
        dragValue = juce::jlimit (minRangeDb, maxRangeDb, std::isfinite (newRangeDb) ? newRangeDb : 0.0);

        if (attachment != nullptr && gestureActive)
            attachment->setValueAsPartOfGesture ((float) dragValue);
    }

    void RangeRing::mouseUp (const juce::MouseEvent&)
    {
        if (gestureActive && attachment != nullptr)
            attachment->endGesture();

        gestureActive = false;
        dragMode = DragMode::undecided;
        repaint();
    }

    void RangeRing::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (attachment != nullptr && ! gestureActive)
            attachment->setValueAsCompleteGesture (0.0f);
    }

    void RangeRing::enablementChanged()
    {
        if (! isEnabled())
            hovered = false;

        repaint();
    }
}
