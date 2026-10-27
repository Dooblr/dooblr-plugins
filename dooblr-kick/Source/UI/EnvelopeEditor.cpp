#include "EnvelopeEditor.h"

#include "../DSP/Envelope.h"
#include "../Presets.h"
#include "Theme.h"

#include <cmath>

namespace dooblrkick::ui
{
    namespace
    {
        constexpr float nodeRadius = 5.0f, hitRadius = 9.0f;
        constexpr float log2Min = 4.321928f;  // log2 (20 Hz)
        constexpr float log2Max = 13.965784f; // log2 (16 kHz)

        float niceCeiling (float ms)
        {
            for (auto step : { 50.0f, 100.0f, 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f, 1500.0f, 2000.0f, 2500.0f })
                if (ms <= step)
                    return step;

            return 2500.0f;
        }
    }

    EnvelopeEditor::EnvelopeEditor (juce::AudioProcessorValueTreeState& stateToUse, params::Envelope envelopeToUse)
        : state (stateToUse), envelope (envelopeToUse)
    {
        setComponentID ("envelopeEditor");
        setOpaque (true);

        for (const auto e : { params::Envelope::pitch, params::Envelope::amp, params::Envelope::master })
        {
            auto& r = raw[(size_t) e];
            r.count = state.getRawParameterValue (params::envelopeCountId (e));

            for (int n = 0; n < params::maxNodes; ++n)
            {
                r.time[(size_t) n] = n > 0 ? state.getRawParameterValue (params::nodeTimeId (e, n)) : nullptr;
                r.value[(size_t) n] = state.getRawParameterValue (params::nodeValueId (e, n));
                r.curve[(size_t) n] = n < params::maxNodes - 1 ? state.getRawParameterValue (params::nodeCurveId (e, n)) : nullptr;
            }
        }

        diamond.addQuadrilateral (0.0f, -4.0f, 4.0f, 0.0f, 0.0f, 4.0f, -4.0f, 0.0f);
        updateViewRange();
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { checkForChanges(); });
    }

    void EnvelopeEditor::setEnvelope (params::Envelope newEnvelope)
    {
        envelope = newEnvelope;
        hover = drag = {};
        updateViewRange();
        repaint();
    }

    juce::RangedAudioParameter* EnvelopeEditor::param (const juce::String& id) const
    {
        return state.getParameter (id);
    }

    int EnvelopeEditor::getNumNodes() const
    {
        return juce::jlimit (2, params::maxNodes, juce::roundToInt (raw[(size_t) envelope].count->load()));
    }

    EnvelopeEditor::NodeValues EnvelopeEditor::readNode (int node) const
    {
        const auto& r = raw[(size_t) envelope];
        const auto n = (size_t) juce::jlimit (0, params::maxNodes - 1, node);
        return { r.time[n] != nullptr ? r.time[n]->load() : 0.0f,
                 r.value[n]->load(),
                 r.curve[n] != nullptr ? r.curve[n]->load() : 0.0f };
    }

    //==============================================================================
    float EnvelopeEditor::timeToX (float ms) const noexcept { return plot.getX() + plot.getWidth() * ms / viewMs; }
    float EnvelopeEditor::xToTime (float x) const noexcept  { return (x - plot.getX()) / plot.getWidth() * viewMs; }

    float EnvelopeEditor::valueToY (float value) const noexcept
    {
        const auto norm = envelope == params::Envelope::pitch
                              ? (std::log2 (juce::jmax (1.0f, value)) - log2Min) / (log2Max - log2Min)
                              : value;
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, norm);
    }

    float EnvelopeEditor::yToValue (float y) const noexcept
    {
        const auto norm = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - y) / plot.getHeight());
        return envelope == params::Envelope::pitch ? std::exp2 (log2Min + norm * (log2Max - log2Min)) : norm;
    }

    void EnvelopeEditor::updateViewRange()
    {
        const auto last = readNode (getNumNodes() - 1).time;
        viewMs = niceCeiling (juce::jmax (100.0f, last * 1.15f));
    }

    juce::Point<float> EnvelopeEditor::getNodePosition (int node) const
    {
        const auto v = readNode (node);
        return { timeToX (v.time), valueToY (v.value) };
    }

    juce::Point<float> EnvelopeEditor::getCurveHandlePosition (int segment) const
    {
        const auto a = readNode (segment), b = readNode (segment + 1);
        const auto mid = a.value + (b.value - a.value) * dsp::applyCurve (0.5f, a.curve);
        const auto y = envelope == params::Envelope::pitch
                           ? valueToY (std::exp2 (std::log2 (a.value) + (std::log2 (b.value) - std::log2 (a.value)) * dsp::applyCurve (0.5f, a.curve)))
                           : valueToY (mid);
        return { timeToX (0.5f * (a.time + b.time)), y };
    }

    EnvelopeEditor::Hit EnvelopeEditor::hitTestPoint (juce::Point<float> p) const
    {
        const auto count = getNumNodes();

        for (int n = count; --n >= 0;)
            if (getNodePosition (n).getDistanceFrom (p) <= hitRadius)
                return { HitKind::node, n };

        for (int s = 0; s < count - 1; ++s)
            if (getCurveHandlePosition (s).getDistanceFrom (p) <= hitRadius)
                return { HitKind::handle, s };

        return {};
    }

    //==============================================================================
    void EnvelopeEditor::beginGesture (std::initializer_list<juce::String> ids)
    {
        endGesture();

        for (const auto& id : ids)
            if (auto* p = param (id))
            {
                p->beginChangeGesture();
                gestureParameters.push_back (p);
            }
    }

    void EnvelopeEditor::endGesture()
    {
        for (auto* p : gestureParameters)
            p->endChangeGesture();

        gestureParameters.clear();
    }

    void EnvelopeEditor::setParam (const juce::String& id, float plainValue)
    {
        if (auto* p = param (id))
            p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plainValue)));
    }

    void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
    {
        const auto hit = hitTestPoint (e.position);

        if (hit.kind != hover.kind || hit.index != hover.index)
        {
            hover = hit;
            setMouseCursor (hit.kind == HitKind::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
    }

    void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
    {
        hover = {};
        repaint();
    }

    void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
    {
        drag = hitTestPoint (e.position);
        dragStart = e.position;

        if (drag.kind == HitKind::node && e.mods.isPopupMenu())
        {
            removeNode (drag.index);
            drag = {};
            return;
        }

        if (drag.kind == HitKind::node)
        {
            if (drag.index > 0)
                beginGesture ({ params::nodeTimeId (envelope, drag.index), params::nodeValueId (envelope, drag.index) });
            else
                beginGesture ({ params::nodeValueId (envelope, drag.index) });
        }
        else if (drag.kind == HitKind::handle)
        {
            dragStartCurve = readNode (drag.index).curve;
            beginGesture ({ params::nodeCurveId (envelope, drag.index) });
        }
    }

    void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag.kind == HitKind::node)
        {
            const auto n = drag.index;
            setParam (params::nodeValueId (envelope, n), yToValue (e.position.y));

            if (n > 0)
            {
                const auto previous = readNode (n - 1).time;
                const auto next = n + 1 < getNumNodes() ? readNode (n + 1).time : params::maxNodeTimeMs;
                setParam (params::nodeTimeId (envelope, n), juce::jlimit (previous, next, xToTime (e.position.x)));
            }
        }
        else if (drag.kind == HitKind::handle)
        {
            const auto a = readNode (drag.index), b = readNode (drag.index + 1);
            // Dragging the handle towards the next node's value makes the segment move there faster.
            const auto towardsEnd = b.value < a.value ? 1.0f : -1.0f;
            const auto delta = (e.position.y - dragStart.y) / 120.0f * towardsEnd;
            setParam (params::nodeCurveId (envelope, drag.index), juce::jlimit (-1.0f, 1.0f, dragStartCurve + delta));
        }

        repaint();
    }

    void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
    {
        endGesture();
        drag = {};
        updateViewRange();
        repaint();
    }

    void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto hit = hitTestPoint (e.position);

        if (hit.kind == HitKind::node)
            removeNode (hit.index);
        else if (hit.kind == HitKind::none && plot.contains (e.position))
            addNodeAt (e.position);
    }

    bool EnvelopeEditor::addNodeAt (juce::Point<float> position)
    {
        const auto count = getNumNodes();

        if (count >= params::maxNodes)
            return false;

        const auto time = juce::jlimit (0.0f, params::maxNodeTimeMs, xToTime (position.x));
        std::vector<NodeValues> nodes;

        for (int n = 0; n < count; ++n)
            nodes.push_back (readNode (n));

        auto insertAt = count;

        for (int n = 1; n < count; ++n)
            if (time < nodes[(size_t) n].time)
            {
                insertAt = n;
                break;
            }

        if (insertAt == 0 || (time <= 0.0f))
            return false;

        nodes.insert (nodes.begin() + insertAt, NodeValues { time, yToValue (position.y), nodes[(size_t) insertAt - 1].curve });

        presets::ValueList values;
        values.emplace_back (params::envelopeCountId (envelope), (float) nodes.size());

        for (int n = 0; n < (int) nodes.size(); ++n)
        {
            if (n > 0)
                values.emplace_back (params::nodeTimeId (envelope, n), nodes[(size_t) n].time);

            values.emplace_back (params::nodeValueId (envelope, n), nodes[(size_t) n].value);

            if (n < params::maxNodes - 1)
                values.emplace_back (params::nodeCurveId (envelope, n), nodes[(size_t) n].curve);
        }

        presets::apply (state, values, true);
        repaint();
        return true;
    }

    bool EnvelopeEditor::removeNode (int node)
    {
        const auto count = getNumNodes();

        if (count <= 2 || node <= 0 || node >= count)
            return false;

        std::vector<NodeValues> nodes;

        for (int n = 0; n < count; ++n)
            if (n != node)
                nodes.push_back (readNode (n));

        presets::ValueList values;
        values.emplace_back (params::envelopeCountId (envelope), (float) nodes.size());

        for (int n = 1; n < (int) nodes.size(); ++n)
        {
            values.emplace_back (params::nodeTimeId (envelope, n), nodes[(size_t) n].time);
            values.emplace_back (params::nodeValueId (envelope, n), nodes[(size_t) n].value);

            if (n < params::maxNodes - 1)
                values.emplace_back (params::nodeCurveId (envelope, n), nodes[(size_t) n].curve);
        }

        presets::apply (state, values, true);
        hover = {};
        repaint();
        return true;
    }

    //==============================================================================
    juce::uint64 EnvelopeEditor::computeHash() const noexcept
    {
        juce::uint64 hash = (juce::uint64) getNumNodes() * 31u + (juce::uint64) envelope;

        for (int n = 0; n < params::maxNodes; ++n)
        {
            const auto v = readNode (n);
            hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (v.time * 100.0f);
            hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (v.value * 1000.0f);
            hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (v.curve * 1000.0f);
        }

        return hash;
    }

    void EnvelopeEditor::checkForChanges()
    {
        // Repaint when any node parameter changed (automation, undo, presets).
        const auto hash = computeHash();

        if (hash != lastHash)
        {
            lastHash = hash;

            if (drag.kind == HitKind::none)
                updateViewRange();

            repaint();
        }
    }

    void EnvelopeEditor::rebuildCache()
    {
        // Grid (depends on the view range and the plot size).
        const auto timeStep = viewMs <= 200.0f ? 25.0f : viewMs <= 600.0f ? 100.0f : viewMs <= 1500.0f ? 250.0f : 500.0f;
        timeLines.clear();
        timeLabels.clear();

        for (float t = 0.0f; t <= viewMs + 0.5f; t += timeStep)
        {
            timeLines.push_back (timeToX (t));
            timeLabels.push_back ({ timeToX (t), juce::String (juce::roundToInt (t)) + (t > 0.0f ? "" : " ms") });
        }

        valueLines.clear();
        valueLabels.clear();

        if (envelope == params::Envelope::pitch)
        {
            for (auto hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                valueLines.push_back (valueToY (hz));
                valueLabels.push_back ({ valueToY (hz), params::formatHz (hz).replace (" ", "") });
            }
        }
        else
        {
            for (auto level : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                valueLines.push_back (valueToY (level));
                valueLabels.push_back ({ valueToY (level), juce::String (juce::roundToInt (level * 100.0f)) + "%" });
            }
        }

        // The curve.
        const auto count = getNumNodes();
        curvePath.clear();
        curvePath.preallocateSpace ((count - 1) * 48 * 3 + 8);
        curvePath.startNewSubPath (getNodePosition (0));

        for (int seg = 0; seg < count - 1; ++seg)
        {
            const auto a = readNode (seg), b = readNode (seg + 1);

            for (int k = 1; k <= 48; ++k)
            {
                const auto x = (float) k / 48.0f;
                const auto shaped = dsp::applyCurve (x, a.curve);
                const auto value = envelope == params::Envelope::pitch
                                       ? std::exp2 (std::log2 (a.value) + (std::log2 (b.value) - std::log2 (a.value)) * shaped)
                                       : a.value + (b.value - a.value) * shaped;
                curvePath.lineTo (timeToX (a.time + (b.time - a.time) * x), valueToY (value));
            }
        }

        curvePath.lineTo (plot.getRight(), getNodePosition (count - 1).y);

        fillPath = curvePath;
        fillPath.lineTo (plot.getRight(), plot.getBottom());
        fillPath.lineTo (plot.getX(), plot.getBottom());
        fillPath.closeSubPath();
    }

    void EnvelopeEditor::resized()
    {
        plot = getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedBottom (18.0f).withTrimmedTop (10.0f).withTrimmedRight (12.0f);
    }

    void EnvelopeEditor::paint (juce::Graphics& g)
    {
        const auto colour = envelope == params::Envelope::pitch ? Palette::dynamic
                          : envelope == params::Envelope::master ? Palette::master : Palette::accent;

        // Rebuild the cached geometry only when something it depends on changed.
        auto hash = computeHash();
        hash = hash * 1000003u ^ (juce::uint64) juce::roundToInt (viewMs * 10.0f);
        hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (plot.getWidth() * 7.0f + plot.getHeight() * 131.0f + plot.getX());

        if (hash != cacheHash || curvePath.isEmpty())
        {
            cacheHash = hash;
            rebuildCache();
        }

        g.fillAll (Palette::background); // opaque; the editor behind is the same colour
        g.setColour (Palette::background);
        g.fillRoundedRectangle (plot, 3.0f);

        // Grid.
        g.setColour (Palette::grid);

        for (auto x : timeLines)
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        for (auto y : valueLines)
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textMuted);

        for (const auto& label : timeLabels)
            g.drawText (label.text, juce::Rectangle<float> (label.position - 30.0f, plot.getBottom() + 2.0f, 60.0f, 14.0f),
                        juce::Justification::centred);

        for (const auto& label : valueLabels)
            g.drawText (label.text, juce::Rectangle<float> (0.0f, label.position - 7.0f, plot.getX() - 6.0f, 14.0f),
                        juce::Justification::centredRight);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (colour.withAlpha (0.10f));
            g.fillPath (fillPath);
            g.setColour (colour);
            g.strokePath (curvePath, juce::PathStrokeType (2.0f));
        }

        const auto count = getNumNodes();

        // Curve handles and nodes.
        for (int s = 0; s < count - 1; ++s)
        {
            const auto p = getCurveHandlePosition (s);
            const auto active = (hover.kind == HitKind::handle && hover.index == s) || (drag.kind == HitKind::handle && drag.index == s);
            g.setColour (active ? Palette::textPrimary : Palette::textMuted.withAlpha (0.7f));
            g.fillPath (diamond, juce::AffineTransform::translation (p.x, p.y));
        }

        for (int n = 0; n < count; ++n)
        {
            const auto p = getNodePosition (n);
            const auto active = (hover.kind == HitKind::node && hover.index == n) || (drag.kind == HitKind::node && drag.index == n);
            g.setColour (Palette::background);
            g.fillEllipse (juce::Rectangle<float> (nodeRadius * 2.0f + 2.0f, nodeRadius * 2.0f + 2.0f).withCentre (p));
            g.setColour (active ? Palette::textPrimary : colour);
            g.fillEllipse (juce::Rectangle<float> (nodeRadius * 2.0f, nodeRadius * 2.0f).withCentre (p));

            if (active)
            {
                const auto v = readNode (n);
                const auto text = juce::String (v.time, 1) + " ms  "
                                  + (envelope == params::Envelope::pitch ? params::formatHz (v.value)
                                                                         : juce::String (juce::roundToInt (v.value * 100.0f)) + " %");
                g.setFont (juce::FontOptions (11.0f));
                g.setColour (Palette::textPrimary);
                const auto box = juce::Rectangle<float> (130.0f, 16.0f).withPosition (juce::jmin (p.x + 8.0f, plot.getRight() - 130.0f), juce::jmax (plot.getY(), p.y - 22.0f));
                g.drawText (text, box, juce::Justification::centredLeft);
            }
        }

        g.setFont (juce::FontOptions (10.5f));
        g.setColour (Palette::textMuted);
        g.drawText ("double-click: add / remove point   drag diamond: curve", plot.reduced (6.0f, 4.0f), juce::Justification::topRight);
    }
}
