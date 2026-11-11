#include "PatchView.h"

#include <cmath>

namespace dooblr0coast::ui
{
    namespace
    {
        juce::Path cablePath (juce::Point<float> a, juce::Point<float> b)
        {
            juce::Path p;
            p.startNewSubPath (a);
            const auto dx = std::abs (b.x - a.x);
            const auto sag = juce::jlimit (12.0f, 70.0f, dx * 0.35f + 16.0f);
            p.cubicTo ({ a.x, a.y + sag }, { b.x, b.y + sag }, b);
            return p;
        }

        float distanceToPath (const juce::Path& path, juce::Point<float> point)
        {
            juce::PathFlatteningIterator it (path, juce::AffineTransform(), 4.0f);
            float best = 1.0e6f;
            while (it.next())
            {
                const juce::Line<float> line (it.x1, it.y1, it.x2, it.y2);
                juce::Point<float> unused;
                best = juce::jmin (best, line.getDistanceFromPoint (point, unused));
            }
            return best;
        }
    }

    Jack::Jack (bool isOut, int idx, juce::Colour c, juce::String name)
        : output (isOut), index (idx), colour (c), label (std::move (name))
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (label);
        setComponentID (output ? dsp::outJackId ((dsp::OutJack) idx) : dsp::inJackId ((dsp::InJack) idx));
    }

    juce::Point<float> Jack::centreInParent() const
    {
        return getBounds().toFloat().getCentre();
    }

    void Jack::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (Palette::panelRaised);
        g.fillEllipse (r);
        g.setColour (hot ? colour : colour.withMultipliedAlpha (0.85f));
        g.drawEllipse (r, hot ? 2.0f : 1.2f);
        auto hole = r.reduced (r.getWidth() * 0.28f);
        g.setColour (output ? colour.withAlpha (0.85f) : Palette::background);
        g.fillEllipse (hole);
        if (getHeight() > 18)
        {
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (8.5f));
        }
    }

    void Jack::mouseEnter (const juce::MouseEvent&) { hot = true; repaint(); }
    void Jack::mouseExit (const juce::MouseEvent&) { hot = false; repaint(); }

    void Jack::mouseDown (const juce::MouseEvent&)
    {
        if (output && onDragFrom)
            onDragFrom (*this);
    }

    void Jack::mouseDrag (const juce::MouseEvent& e)
    {
        if (output && onDragMove)
            onDragMove (*this, e.getScreenPosition());
    }

    void Jack::mouseUp (const juce::MouseEvent& e)
    {
        if (output && onDragEnd)
            onDragEnd (*this, e.getScreenPosition());
    }

    CableLayer::CableLayer()
    {
        setInterceptsMouseClicks (true, false);
        setOpaque (false);
    }

    void CableLayer::setCables (std::vector<Drawn> next)
    {
        cables = std::move (next);
        repaint();
    }

    void CableLayer::setDrag (juce::Point<float> from, juce::Point<float> to, juce::Colour colour, bool active)
    {
        dragFrom = from;
        dragTo = to;
        dragColour = colour;
        dragging = active;
        repaint();
    }

    bool CableLayer::hitTest (int x, int y)
    {
        if (dragging)
            return false;
        const juce::Point<float> p ((float) x, (float) y);
        for (const auto& c : cables)
            if (distanceToPath (cablePath (c.a, c.b), p) < 7.0f)
                return true;
        return false;
    }

    void CableLayer::paint (juce::Graphics& g)
    {
        for (const auto& c : cables)
        {
            auto path = cablePath (c.a, c.b);
            g.setColour (c.colour.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (dragging)
        {
            auto path = cablePath (dragFrom, dragTo);
            g.setColour (dragColour.withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void CableLayer::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto p = e.position;
        int hit = -1;
        float best = 7.0f;
        for (int i = 0; i < (int) cables.size(); ++i)
        {
            const auto d = distanceToPath (cablePath (cables[(size_t) i].a, cables[(size_t) i].b), p);
            if (d < best)
            {
                best = d;
                hit = i;
            }
        }
        if (hit >= 0 && onRemove)
            onRemove (cables[(size_t) hit].from, cables[(size_t) hit].to);
    }

    void ScopeView::setSamples (const float* data, int count)
    {
        samples.assign (data, data + count);
        repaint();
    }

    void ScopeView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::background);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);

        if (samples.size() < 2)
            return;

        juce::Path path;
        const auto plot = r.reduced (4.0f, 6.0f);
        const auto mid = plot.getCentreY();
        const auto amp = plot.getHeight() * 0.42f;
        for (int i = 0; i < (int) samples.size(); ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (samples.size() - 1);
            const auto y = mid - juce::jlimit (-1.0f, 1.0f, samples[(size_t) i]) * amp;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.strokePath (path, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
