#include "ProgressRing.h"
#include "Theme.h"

namespace dooblr::installer::ui
{
    ProgressRing::ProgressRing()
    {
        setInterceptsMouseClicks (false, false);
        setTitle ("Install progress");
        setAccessible (true);
    }

    ProgressRing::~ProgressRing() { stopTimer(); }

    void ProgressRing::setProgress (double p)
    {
        p = juce::jlimit (0.0, 1.0, p);
        if (std::abs (p - progress) > 1.0e-4)
        {
            progress = p;
            repaint();
        }
    }

    void ProgressRing::setState (State s)
    {
        state = s;
        if (state == State::busy) startTimerHz (30);
        else                      stopTimer();
        if (state == State::done || state == State::problems)
            progress = 1.0;
        repaint();
    }

    void ProgressRing::timerCallback()
    {
        spin = std::fmod (spin + 0.12f, juce::MathConstants<float>::twoPi);
        repaint();
    }

    void ProgressRing::paint (juce::Graphics& g)
    {
        const auto size = (float) juce::jmin (getWidth(), getHeight());
        const auto thickness = juce::jmax (6.0f, size * 0.07f);
        const auto area = getLocalBounds().toFloat().withSizeKeepingCentre (size, size).reduced (thickness * 0.5f + 1.0f);
        const auto c = area.getCentre();
        const auto radius = area.getWidth() * 0.5f;
        const auto stroke = juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Track
        g.setColour (Palette::panelRaised);
        g.drawEllipse (area, thickness);

        const auto colour = state == State::failed   ? Palette::danger
                          : (state == State::problems || state == State::cancelled) ? Palette::dynamic
                                                     : Palette::accent;

        // Filled part: the overall progress, clockwise from the top.
        if (progress > 0.0)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * (float) progress, true);
            g.setColour (colour);
            g.strokePath (arc, stroke);
        }

        // Activity: a short turning arc on the track while busy.
        if (state == State::busy)
        {
            juce::Path a;
            a.addCentredArc (c.x, c.y, radius, radius, 0.0f, spin, spin + 0.9f, true);
            g.setColour (Palette::textPrimary.withAlpha (0.35f));
            g.strokePath (a, juce::PathStrokeType (thickness * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Centre
        const auto inner = area.reduced (thickness * 1.5f);
        if (state == State::problems)
        {
            // Some installed, some failed: "!" rather than a tick.
            const auto t = inner.withSizeKeepingCentre (thickness, inner.getHeight() * 0.46f);
            juce::Path bar;
            bar.startNewSubPath (t.getCentreX(), t.getY());
            bar.lineTo (t.getCentreX(), t.getBottom() - thickness * 1.9f);
            g.setColour (colour);
            g.strokePath (bar, juce::PathStrokeType (thickness * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (juce::Rectangle<float> (thickness * 0.95f, thickness * 0.95f).withCentre ({ t.getCentreX(), t.getBottom() - thickness * 0.45f }));
        }
        else if (state == State::done)
        {
            juce::Path tick;
            const auto t = inner.withSizeKeepingCentre (inner.getWidth() * 0.42f, inner.getHeight() * 0.42f);
            tick.startNewSubPath (t.getX(), t.getCentreY() + t.getHeight() * 0.02f);
            tick.lineTo (t.getX() + t.getWidth() * 0.38f, t.getBottom() - t.getHeight() * 0.12f);
            tick.lineTo (t.getRight(), t.getY() + t.getHeight() * 0.1f);
            g.setColour (colour);
            g.strokePath (tick, juce::PathStrokeType (thickness * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (state == State::failed)
        {
            juce::Path x;
            const auto t = inner.withSizeKeepingCentre (inner.getWidth() * 0.34f, inner.getHeight() * 0.34f);
            x.startNewSubPath (t.getTopLeft());     x.lineTo (t.getBottomRight());
            x.startNewSubPath (t.getTopRight());    x.lineTo (t.getBottomLeft());
            g.setColour (colour);
            g.strokePath (x, juce::PathStrokeType (thickness * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (Palette::textPrimary);
            const auto stopped = state == State::cancelled;
            g.setFont (juce::FontOptions (juce::jmax (14.0f, size * (stopped ? 0.13f : 0.2f)), juce::Font::bold));
            g.drawText (stopped ? juce::String ("Stopped") : juce::String (juce::roundToInt (progress * 100.0)) + "%",
                        inner, juce::Justification::centred);
        }
    }
}
