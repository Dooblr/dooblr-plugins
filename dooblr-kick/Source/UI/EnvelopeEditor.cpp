#include "EnvelopeEditor.h"

#include "../DSP/Envelope.h"
#include "../Presets.h"
#include "Theme.h"

#include <cmath>
#include <limits>

namespace dooblrkick::ui
{
    namespace
    {
        constexpr float nodeRadius = 5.0f, hitRadius = 9.0f, lineHitRadius = 7.0f;
        constexpr float zoomPixelsPerOctave = 120.0f; // drag distance that halves / doubles the span
        constexpr float log2Min = 4.321928f;  // log2 (20 Hz)
        constexpr float log2Max = 13.965784f; // log2 (16 kHz)

        float niceCeiling (float ms)
        {
            for (auto step : { 50.0f, 100.0f, 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f, 1500.0f, 2000.0f, 2500.0f,
                               3000.0f, 4000.0f, 5000.0f, 6000.0f })
                if (ms <= step)
                    return step;

            return 6000.0f;
        }

        float gridStep (float spanMs)
        {
            for (auto step : { 1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f })
                if (spanMs / step <= 12.0f)
                    return step;

            return 1000.0f;
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
            r.on = e == params::Envelope::master ? state.getRawParameterValue ("menv_on") : nullptr;
            r.timeScale = e == params::Envelope::master ? state.getRawParameterValue ("menv_tscale") : nullptr;
            r.depth = e == params::Envelope::master ? state.getRawParameterValue ("menv_depth") : nullptr;

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
        userZoom = false;
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
    float EnvelopeEditor::timeToX (float ms) const noexcept { return plot.getX() + plot.getWidth() * (ms - viewStartMs) / viewMs; }
    float EnvelopeEditor::xToTime (float x) const noexcept  { return viewStartMs + (x - plot.getX()) / plot.getWidth() * viewMs; }

    void EnvelopeEditor::setSelectedNode (int node)
    {
        node = juce::jlimit (0, getNumNodes() - 1, node);

        if (node == selectedNode)
            return;

        selectedNode = node;
        repaint();

        if (onSelectedNodeChange)
            onSelectedNodeChange (node);
    }

    bool EnvelopeEditor::showsEffectiveCurve() const noexcept
    {
        const auto& r = raw[(size_t) envelope];
        return r.timeScale != nullptr && (r.timeScale->load() != 1.0f || r.depth->load() < 1.0f);
    }

    dsp::MultiPointEnvelope EnvelopeEditor::getEffectiveEnvelope() const
    {
        const auto& r = raw[(size_t) envelope];
        dsp::MultiPointEnvelope env;
        env.numNodes = getNumNodes();

        for (int n = 0; n < params::maxNodes; ++n)
        {
            const auto v = readNode (n);
            auto& node = env.nodes[(size_t) n];
            node = { v.time, v.value, v.curve };

            if (r.timeScale != nullptr)
                node = params::applyMasterMacros (node, r.timeScale->load(), r.depth->load());
        }

        env.sanitise();
        return env;
    }

    bool EnvelopeEditor::isBypassedLook() const noexcept
    {
        const auto* on = raw[(size_t) envelope].on;
        return on != nullptr && on->load() < 0.5f;
    }

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
        if (userZoom)
            return; // the user's zoom stays until reset

        auto last = readNode (getNumNodes() - 1).time;
        if (showsEffectiveCurve())
            last = juce::jmax (last, getEffectiveEnvelope().getEndTimeMs());
        viewStartMs = 0.0f;
        viewMs = juce::jmin (niceCeiling (juce::jmax (100.0f, last * 1.15f)), getMaxTimeMs());
    }

    void EnvelopeEditor::resetView()
    {
        userZoom = false;
        updateViewRange();
        repaint();
    }

    float EnvelopeEditor::curveYAt (float ms) const
    {
        const auto count = getNumNodes();

        if (ms <= 0.0f)
            return valueToY (readNode (0).value);

        for (int seg = 0; seg < count - 1; ++seg)
        {
            const auto a = readNode (seg), b = readNode (seg + 1);

            if (ms < b.time)
            {
                const auto x = b.time - a.time > 1.0e-6f ? (ms - a.time) / (b.time - a.time) : 1.0f;
                const auto shaped = dsp::applyCurve (juce::jlimit (0.0f, 1.0f, x), a.curve);
                return valueToY (envelope == params::Envelope::pitch
                                     ? std::exp2 (std::log2 (a.value) + (std::log2 (b.value) - std::log2 (a.value)) * shaped)
                                     : a.value + (b.value - a.value) * shaped);
            }
        }

        return valueToY (readNode (count - 1).value);
    }

    float EnvelopeEditor::distanceToCurve (juce::Point<float> p) const
    {
        // Nearest point of the drawn curve, sampled per pixel around p (steep segments included).
        auto best = std::numeric_limits<float>::max();

        for (float dx = -lineHitRadius; dx <= lineHitRadius; dx += 1.0f)
        {
            const auto y = curveYAt (xToTime (p.x + dx));
            best = juce::jmin (best, std::hypot (dx, y - p.y));
        }

        // Vertical jumps (nodes at the same time) are vertical lines between two y values.
        for (int n = 1; n < getNumNodes(); ++n)
        {
            const auto a = getNodePosition (n - 1), b = getNodePosition (n);

            if (std::abs (a.x - b.x) < 0.5f && std::abs (p.x - a.x) <= lineHitRadius
                && p.y >= juce::jmin (a.y, b.y) && p.y <= juce::jmax (a.y, b.y))
                best = juce::jmin (best, std::abs (p.x - a.x));
        }

        return best;
    }

    bool EnvelopeEditor::isZoomAreaAt (juce::Point<float> position) const
    {
        return hitTestPoint (position).kind == HitKind::zoom;
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

        if (! plot.contains (p))
            return {};

        if (distanceToCurve (p) <= lineHitRadius)
            return { HitKind::line, -1 };

        return { HitKind::zoom, -1 };
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
            setMouseCursor (hit.kind == HitKind::zoom ? juce::MouseCursor::UpDownResizeCursor
                          : hit.kind == HitKind::node || hit.kind == HitKind::handle ? juce::MouseCursor::DraggingHandCursor
                                                                                     : juce::MouseCursor::NormalCursor);
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
            setSelectedNode (drag.index);
        else if (drag.kind == HitKind::handle)
            setSelectedNode (drag.index + 1);

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
        else if (drag.kind == HitKind::zoom)
        {
            if (e.mods.isPopupMenu())
            {
                drag = {};
                return;
            }

            zoomStartSpan = viewMs;
            zoomStartStart = viewStartMs;
            zoomAnchorFraction = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
            zoomAnchorMs = xToTime (e.position.x);
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
                const auto next = n + 1 < getNumNodes() ? readNode (n + 1).time : getMaxTimeMs();
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
        else if (drag.kind == HitKind::zoom)
        {
            // Up stretches the view out (zoom in), down contracts it; the time under the mouse stays put.
            const auto maxSpan = getMaxTimeMs();
            const auto span = juce::jlimit (minViewMs, maxSpan, zoomStartSpan * std::exp2 ((e.position.y - dragStart.y) / zoomPixelsPerOctave));
            const auto start = juce::jlimit (0.0f, juce::jmax (0.0f, maxSpan - span), zoomAnchorMs - zoomAnchorFraction * span);

            if (! juce::approximatelyEqual (span, viewMs) || ! juce::approximatelyEqual (start, viewStartMs))
            {
                viewMs = span;
                viewStartMs = start;
                userZoom = true;
            }
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
        else if ((hit.kind == HitKind::zoom || hit.kind == HitKind::line) && plot.contains (e.position))
            addNodeAt (e.position);
        else if (hit.kind == HitKind::none && getRulerArea().contains (e.position))
            resetView(); // double-click the time ruler: fit the envelope again
    }

    bool EnvelopeEditor::addNodeAt (juce::Point<float> position)
    {
        const auto count = getNumNodes();

        if (count >= params::maxNodes)
            return false;

        const auto time = juce::jlimit (0.0f, getMaxTimeMs(), xToTime (position.x));
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
        setSelectedNode (insertAt);
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
        hash = hash * 1000003u ^ (isBypassedLook() ? 7u : 3u);

        if (const auto& r = raw[(size_t) envelope]; r.timeScale != nullptr)
        {
            hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (r.timeScale->load() * 10000.0f);
            hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (r.depth->load() * 10000.0f);
        }

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

            if (selectedNode > getNumNodes() - 1)
                setSelectedNode (getNumNodes() - 1);

            if (drag.kind == HitKind::none)
                updateViewRange();

            repaint();
        }
    }

    void EnvelopeEditor::rebuildCache()
    {
        // Grid (depends on the view range and the plot size).
        // Grid lines follow the zoom: about 6..12 per view, at 1-2-5 steps.
        const auto timeStep = gridStep (viewMs);
        timeLines.clear();
        timeLabels.clear();
        const auto viewEnd = viewStartMs + viewMs;
        const auto decimals = timeStep < 1.0f ? 1 : 0;

        for (auto i = (int) std::ceil (viewStartMs / timeStep - 1.0e-3f); (float) i * timeStep <= viewEnd + 1.0e-3f; ++i)
        {
            const auto t = (float) i * timeStep;
            timeLines.push_back (timeToX (t));
            timeLabels.push_back ({ timeToX (t), juce::String (t, decimals) + (timeLabels.empty() ? " ms" : "") });
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
        curvePath.preallocateSpace ((count - 1) * 200 * 3 + 16);
        curvePath.startNewSubPath (getNodePosition (0));
        const auto viewStart = viewStartMs;

        for (int seg = 0; seg < count - 1; ++seg)
        {
            const auto a = readNode (seg), b = readNode (seg + 1);
            // Only the visible part of a segment is sampled (about one point per 3 px), so a
            // zoomed-in view stays smooth; parts outside the plot are straight (clipped) lines.
            const auto t0 = juce::jmax (a.time, viewStart), t1 = juce::jmin (b.time, viewEnd);

            if (t1 > t0 && b.time > a.time)
            {
                const auto steps = juce::jlimit (8, 200, (int) ((timeToX (t1) - timeToX (t0)) / 3.0f));

                for (int k = 0; k <= steps; ++k)
                {
                    const auto t = t0 + (t1 - t0) * (float) k / (float) steps;
                    curvePath.lineTo (timeToX (t), curveYAt (juce::jmin (t, b.time - 1.0e-4f)));
                }
            }

            curvePath.lineTo (getNodePosition (seg + 1));
        }

        const auto left = juce::jmin (plot.getX(), getNodePosition (0).x);
        const auto right = juce::jmax (plot.getRight(), getNodePosition (count - 1).x);
        curvePath.lineTo (right, getNodePosition (count - 1).y);

        fillPath = curvePath;
        fillPath.lineTo (right, plot.getBottom());
        fillPath.lineTo (left, plot.getBottom());
        fillPath.closeSubPath();

        // Master with Time Scale / Depth away from 100 %: the envelope as it plays.
        effectivePath.clear();
        if (showsEffectiveCurve())
        {
            const auto effective = getEffectiveEnvelope();
            const auto steps = juce::jlimit (16, 600, juce::roundToInt (plot.getWidth() / 2.0f));
            effectivePath.preallocateSpace (steps * 3 + 8);

            for (int i = 0; i <= steps; ++i)
            {
                const auto t = viewStartMs + viewMs * (float) i / (float) steps;
                const juce::Point<float> p (timeToX (t), valueToY (effective.evaluate (t)));

                if (i == 0)
                    effectivePath.startNewSubPath (p);
                else
                    effectivePath.lineTo (p);
            }
        }
    }

    void EnvelopeEditor::resized()
    {
        plot = getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedBottom (18.0f).withTrimmedTop (10.0f).withTrimmedRight (12.0f);
    }

    void EnvelopeEditor::paint (juce::Graphics& g)
    {
        const auto bypassed = isBypassedLook();
        const auto colour = (envelope == params::Envelope::pitch ? Palette::dynamic
                           : envelope == params::Envelope::master ? Palette::master : Palette::accent)
                                .withMultipliedAlpha (bypassed ? 0.4f : 1.0f);

        // Rebuild the cached geometry only when something it depends on changed.
        auto hash = computeHash();
        hash = hash * 1000003u ^ (juce::uint64) juce::roundToInt (viewMs * 10.0f);
        hash = hash * 1000003u ^ (juce::uint64) juce::roundToInt (viewStartMs * 10.0f);
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
            g.strokePath (curvePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (! effectivePath.isEmpty())
            {
                // A thin light line (a dashed stroke would allocate in paint).
                g.setColour (Palette::textPrimary.withAlpha (0.55f));
                g.strokePath (effectivePath, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
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

            if (n == getSelectedNode() && onSelectedNodeChange)
            {
                g.setColour (Palette::textPrimary.withAlpha (0.8f));
                g.drawEllipse (juce::Rectangle<float> (nodeRadius * 2.0f + 6.0f, nodeRadius * 2.0f + 6.0f).withCentre (p), 1.2f);
            }

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
        g.drawText ("double-click: add / remove point   drag diamond: curve   drag empty: zoom   double-click ruler: fit",
                    plot.reduced (6.0f, 4.0f), juce::Justification::topRight);

        if (bypassed)
        {
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (Palette::textMuted);
            g.drawText ("ENVELOPE OFF (bypassed)", plot.reduced (6.0f, 4.0f), juce::Justification::topLeft);
        }
    }
}
