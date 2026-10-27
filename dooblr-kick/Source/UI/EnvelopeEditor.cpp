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
        for (int l = 0; l < 5; ++l)
        {
            layerStart[(size_t) l] = state.getRawParameterValue (params::layerId (l, "start"));
            auto& lr = layerRaw[(size_t) l];
            auto maxMs = 0.0f;

            if (l == 0)
            {
                lr.length = state.getRawParameterValue ("click_length");
                lr.type = state.getRawParameterValue ("click_type");
                lr.source = state.getRawParameterValue (params::layerId (0, "source"));
                if (auto* p = state.getParameter ("click_length"))
                    maxMs = p->getNormalisableRange().end;
            }
            else if (l >= 2)
            {
                const char* names[] { "attack", "hold", "decay", "curve" };
                for (int k = 0; k < 4; ++k)
                {
                    lr.ahd[(size_t) k] = state.getRawParameterValue (params::layerId (l, names[k]));
                    if (k < 3)
                        if (auto* p = state.getParameter (params::layerId (l, names[k])))
                            maxMs += p->getNormalisableRange().end;
                }
            }

            lr.maxMs = juce::jmax (50.0f, maxMs);
        }

        layer = envelope == params::Envelope::master ? -1 : 1;
        updateViewRange();
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { tick(); });
    }

    void EnvelopeEditor::setEnvelope (params::Envelope newEnvelope)
    {
        envelope = newEnvelope;
        shape = Shape::points;
        layer = envelope == params::Envelope::master ? -1 : 1;
        rebind();
    }

    void EnvelopeEditor::setLayerEnvelope (int newLayer)
    {
        if (newLayer == 1)
        {
            setEnvelope (envelope == params::Envelope::master ? params::Envelope::amp : envelope);
            return;
        }

        envelope = params::Envelope::amp; // a 0..1 level graph
        layer = juce::jlimit (0, 4, newLayer);
        shape = layer == 0 ? Shape::click : Shape::ahd;
        rebind();
    }

    void EnvelopeEditor::rebind()
    {
        // A different envelope: drop everything cached for the old one (geometry, hover /
        // drag state, the change hash, the playhead's offset) and fit the new one.
        endGesture();
        cacheHash = 0;
        lastHash = computeHash();
        curvePath.clear();
        selectedNode = juce::jmin (selectedNode, getNumNodes() - 1);
        playheadSeen = false;
        playheadActive = false;
        playheadAlpha = 0.0f;
        hover = drag = {};
        userZoom = false;
        animating = false;
        updateViewRange();
        repaint();
    }

    juce::RangedAudioParameter* EnvelopeEditor::param (const juce::String& id) const
    {
        return state.getParameter (id);
    }

    int EnvelopeEditor::getNumNodes() const
    {
        if (shape == Shape::ahd)
            return 4;
        if (shape == Shape::click)
            return 2;

        return juce::jlimit (2, params::maxNodes, juce::roundToInt (raw[(size_t) envelope].count->load()));
    }

    EnvelopeEditor::NodeValues EnvelopeEditor::readNode (int node) const
    {
        if (shape == Shape::ahd)
        {
            // Attack (linear) / hold / curved decay as 4 points; the decay segment uses the same
            // curve law as AhdEnvelope (1 - applyCurve), which is the multi-point segment rule.
            const auto a = ahdParam (0), h = ahdParam (1), d = ahdParam (2), c = ahdParam (3);
            switch (juce::jlimit (0, 3, node))
            {
                case 0:  return { 0.0f, 0.0f, 0.0f };
                case 1:  return { a, 1.0f, 0.0f };
                case 2:  return { a + h, 1.0f, c };
                default: return { a + h + d, 0.0f, 0.0f };
            }
        }

        if (shape == Shape::click)
        {
            const auto* length = layerRaw[0].length;
            return node <= 0 ? NodeValues { 0.0f, 1.0f, 0.0f }
                             : NodeValues { juce::jmax (0.5f, length != nullptr ? length->load() : 12.0f), 0.0f, 0.0f };
        }

        return readNodeOf (envelope, node);
    }

    float EnvelopeEditor::ahdParam (int which) const
    {
        const auto* p = layer >= 0 ? layerRaw[(size_t) layer].ahd[(size_t) juce::jlimit (0, 3, which)] : nullptr;
        return p != nullptr ? p->load() : 0.0f;
    }

    float EnvelopeEditor::clickLevelAt (float ms) const
    {
        // The Click's amplitude shape over its Length, as KickVoice::renderClick makes it
        // (normalised; the oscillator / noise itself is not drawn).
        const auto& lr = layerRaw[0];
        const auto lengthMs = juce::jmax (0.5f, lr.length != nullptr ? lr.length->load() : 12.0f);

        if (ms < 0.0f || ms >= lengthMs)
            return 0.0f;

        const auto x = ms / lengthMs;

        if (lr.source != nullptr && lr.source->load() > 0.5f)
            return x < 0.6f ? 1.0f : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (x - 0.6f) / 0.4f);

        const auto taper = 1.0f - x;
        switch (lr.type != nullptr ? juce::roundToInt (lr.type->load()) : 0)
        {
            case 1:  return std::exp (-5.0f * x) * taper;                                // noise burst
            case 2:  return std::exp (-7.0f * x) * taper;                                // snap
            case 3:  return juce::jmin (1.0f, ms / 0.1f) * std::exp (-8.0f * x) * taper; // tick
            case 4:  return juce::jmin (1.0f, ms / 0.5f) * std::exp (-3.0f * x) * taper; // knock
            default: return std::exp (-4.0f * x) * taper;                                // chirp
        }
    }

    juce::Colour EnvelopeEditor::getCurveColour() const
    {
        if (shape != Shape::points && layer >= 0)
            return bandColour (layer);

        return envelope == params::Envelope::pitch ? Palette::dynamic
             : envelope == params::Envelope::master ? Palette::master : Palette::accent;
    }

    float EnvelopeEditor::getMaxTimeMs() const
    {
        if (shape != Shape::points && layer >= 0)
            return layerRaw[(size_t) layer].maxMs;

        return params::maxNodeTimeFor (envelope);
    }

    bool EnvelopeEditor::isNodeEditable (int node) const
    {
        if (shape == Shape::ahd)
            return node >= 1 && node <= 3;
        if (shape == Shape::click)
            return node == 1;
        return node >= 0 && node < getNumNodes();
    }

    bool EnvelopeEditor::isHandleEditable (int segment) const
    {
        if (shape == Shape::ahd)
            return segment == 2;
        if (shape == Shape::click)
            return false;
        return segment >= 0 && segment < getNumNodes() - 1;
    }

    juce::StringArray EnvelopeEditor::getBoundParameterIds() const
    {
        juce::StringArray ids;

        if (shape == Shape::ahd)
        {
            for (const auto* name : { "attack", "hold", "decay", "curve" })
                ids.add (params::layerId (layer, name));
        }
        else if (shape == Shape::click)
        {
            ids.addArray ({ "click_length", "click_type", params::layerId (0, "source") });
        }
        else
        {
            ids.add (params::envelopeCountId (envelope));
            for (int n = 0; n < getNumNodes(); ++n)
            {
                if (n > 0) ids.add (params::nodeTimeId (envelope, n));
                ids.add (params::nodeValueId (envelope, n));
                if (n < getNumNodes() - 1) ids.add (params::nodeCurveId (envelope, n));
            }
        }

        return ids;
    }

    EnvelopeEditor::NodeValues EnvelopeEditor::readNodeOf (params::Envelope e, int node) const
    {
        const auto& r = raw[(size_t) e];
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
        refit (false);
    }

    float EnvelopeEditor::getExtentMs() const
    {
        auto last = readNode (getNumNodes() - 1).time;
        if (showsEffectiveCurve())
            last = juce::jmax (last, getEffectiveEnvelope().getEndTimeMs());
        return last;
    }

    float EnvelopeEditor::fitSpanFor (float lastMs, float maxMs) noexcept
    {
        // 0 .. last point plus a small margin (8 % + 4 ms), at least 50 ms.
        return juce::jlimit (juce::jmin (50.0f, maxMs), maxMs, juce::jmax (50.0f, lastMs * 1.08f + 4.0f));
    }

    float EnvelopeEditor::getFitSpanMs() const
    {
        return fitSpanFor (getExtentMs(), getMaxTimeMs());
    }

    float EnvelopeEditor::clampViewStart (float startMs, float spanMs, float maxMs) noexcept
    {
        return juce::jlimit (0.0f, juce::jmax (0.0f, maxMs - spanMs), startMs);
    }

    EnvelopeEditor::DragAxis EnvelopeEditor::decideDragAxis (juce::Point<float> offset) noexcept
    {
        if (offset.getDistanceFromOrigin() < axisLockPixels)
            return DragAxis::undecided;

        return std::abs (offset.x) > std::abs (offset.y) ? DragAxis::pan : DragAxis::zoom;
    }

    void EnvelopeEditor::refit (bool animate)
    {
        if (userZoom)
            return; // the user's zoom / pan stays until reset

        const auto span = getFitSpanMs();

        if (animate && fitAnimationMs > 0.0 && (isShowing() || animateOffscreen))
            animateTo (0.0f, span);
        else
        {
            animating = false;
            viewStartMs = 0.0f;
            viewMs = span;
        }
    }

    void EnvelopeEditor::animateTo (float start, float span)
    {
        const auto target = getViewTarget();

        if (juce::approximatelyEqual (target.x, start) && juce::approximatelyEqual (target.y, span))
            return;

        fromStart = viewStartMs;
        fromSpan = viewMs;
        toStart = start;
        toSpan = span;
        animStartMs = -1.0;
        animating = true;
    }

    void EnvelopeEditor::advanceViewAnimation (double nowMs)
    {
        if (! animating)
            return;

        if (animStartMs < 0.0)
            animStartMs = nowMs;

        const auto t = fitAnimationMs > 0.0 ? juce::jlimit (0.0, 1.0, (nowMs - animStartMs) / fitAnimationMs) : 1.0;
        const auto eased = (float) (1.0 - std::pow (1.0 - t, 3.0)); // ease-out cubic

        // Interpolate both edges, so the view never leaves 0 .. max on the way.
        const auto fromEnd = fromStart + fromSpan, toEnd = toStart + toSpan;
        const auto start = fromStart + (toStart - fromStart) * eased;
        const auto end = fromEnd + (toEnd - fromEnd) * eased;
        viewStartMs = start;
        viewMs = juce::jmax (minViewMs, end - start);

        if (t >= 1.0)
        {
            viewStartMs = toStart;
            viewMs = toSpan;
            animating = false;
        }

        repaint();
    }

    void EnvelopeEditor::setManualView (float start, float span)
    {
        const auto maxSpan = getMaxTimeMs();
        span = juce::jlimit (juce::jmin (minViewMs, maxSpan), maxSpan, span);
        start = clampViewStart (start, span, maxSpan);
        animating = false;

        if (! juce::approximatelyEqual (span, viewMs) || ! juce::approximatelyEqual (start, viewStartMs))
        {
            viewMs = span;
            viewStartMs = start;
            userZoom = true;
            repaint();
        }
    }

    void EnvelopeEditor::panBy (float deltaMs)
    {
        setManualView (viewStartMs + deltaMs, viewMs);
    }

    void EnvelopeEditor::zoomAround (float x, float spanFactor)
    {
        const auto fraction = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        const auto anchor = xToTime (plot.getX() + fraction * plot.getWidth());
        const auto span = juce::jlimit (juce::jmin (minViewMs, getMaxTimeMs()), getMaxTimeMs(), viewMs * spanFactor);
        setManualView (anchor - fraction * span, span);
    }

    void EnvelopeEditor::keepTimeVisible (float ms)
    {
        // While a point is dragged: no refit, but don't let it leave the view.
        const auto end = viewStartMs + viewMs;

        if (ms <= end && ms >= viewStartMs)
            return;

        animating = false;
        const auto maxSpan = getMaxTimeMs();

        if (! userZoom && ms > end)
            viewMs = juce::jmin (maxSpan, fitSpanFor (ms, maxSpan));
        else if (ms > end)
            viewStartMs = clampViewStart (ms - 0.92f * viewMs, viewMs, maxSpan);
        else
            viewStartMs = clampViewStart (ms - 0.08f * viewMs, viewMs, maxSpan);
    }

    void EnvelopeEditor::resetView()
    {
        userZoom = false;
        refit (true);
        repaint();
    }

    float EnvelopeEditor::curveYAt (float ms) const
    {
        const auto count = getNumNodes();

        if (shape == Shape::click && ms > 0.0f)
            return valueToY (clickLevelAt (ms));

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
            if (isNodeEditable (n) && getNodePosition (n).getDistanceFrom (p) <= hitRadius)
                return { HitKind::node, n };

        for (int s = 0; s < count - 1; ++s)
            if (isHandleEditable (s) && getCurveHandlePosition (s).getDistanceFrom (p) <= hitRadius)
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
            setMouseCursor (hit.kind == HitKind::zoom ? juce::MouseCursor::UpDownLeftRightResizeCursor
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

        if (drag.kind == HitKind::node && shape == Shape::ahd)
        {
            const char* names[] { "attack", "hold", "decay" };
            beginGesture ({ params::layerId (layer, names[juce::jlimit (1, 3, drag.index) - 1]) });
        }
        else if (drag.kind == HitKind::node && shape == Shape::click)
        {
            beginGesture ({ "click_length" });
        }
        else if (drag.kind == HitKind::node)
        {
            if (drag.index > 0)
                beginGesture ({ params::nodeTimeId (envelope, drag.index), params::nodeValueId (envelope, drag.index) });
            else
                beginGesture ({ params::nodeValueId (envelope, drag.index) });
        }
        else if (drag.kind == HitKind::handle)
        {
            dragStartCurve = readNode (drag.index).curve;
            beginGesture ({ shape == Shape::ahd ? params::layerId (layer, "curve") : params::nodeCurveId (envelope, drag.index) });
        }
        else if (drag.kind == HitKind::zoom)
        {
            if (e.mods.isPopupMenu())
            {
                drag = {};
                return;
            }

            animating = false; // a manual view change takes over from a running fit
            dragAxis = DragAxis::undecided;
            zoomStartSpan = viewMs;
            zoomStartStart = viewStartMs;
            zoomAnchorFraction = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
            zoomAnchorMs = xToTime (e.position.x);
        }
    }

    void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag.kind == HitKind::node && shape != Shape::points)
        {
            // AHD / Click: points move in time only; each sets its own segment's length.
            const auto n = drag.index;
            const auto previous = readNode (n - 1).time;
            const auto t = juce::jlimit (previous, getMaxTimeMs(), xToTime (e.position.x));

            if (shape == Shape::click)
                setParam ("click_length", t);
            else
            {
                const char* names[] { "attack", "hold", "decay" };
                setParam (params::layerId (layer, names[juce::jlimit (1, 3, n) - 1]), t - previous);
            }

            keepTimeVisible (readNode (n).time);
        }
        else if (drag.kind == HitKind::node)
        {
            const auto n = drag.index;
            setParam (params::nodeValueId (envelope, n), yToValue (e.position.y));

            if (n > 0)
            {
                const auto previous = readNode (n - 1).time;
                const auto next = n + 1 < getNumNodes() ? readNode (n + 1).time : getMaxTimeMs();
                setParam (params::nodeTimeId (envelope, n), juce::jlimit (previous, next, xToTime (e.position.x)));
                keepTimeVisible (readNode (n).time);
            }
        }
        else if (drag.kind == HitKind::handle)
        {
            const auto a = readNode (drag.index), b = readNode (drag.index + 1);
            // Dragging the handle towards the next node's value makes the segment move there faster.
            const auto towardsEnd = b.value < a.value ? 1.0f : -1.0f;
            const auto delta = (e.position.y - dragStart.y) / 120.0f * towardsEnd;
            setParam (shape == Shape::ahd ? params::layerId (layer, "curve") : params::nodeCurveId (envelope, drag.index),
                      juce::jlimit (-1.0f, 1.0f, dragStartCurve + delta));
        }
        else if (drag.kind == HitKind::zoom)
        {
            // v0.3.4: the axis locks once the drag has moved a few pixels.
            if (dragAxis == DragAxis::undecided)
            {
                dragAxis = decideDragAxis (e.position - dragStart);

                if (dragAxis == DragAxis::pan)
                    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                else if (dragAxis == DragAxis::zoom)
                    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            }

            const auto maxSpan = getMaxTimeMs();

            if (dragAxis == DragAxis::zoom)
            {
                // Up stretches the view out (zoom in), down contracts it; the time under the mouse stays put.
                const auto span = juce::jlimit (minViewMs, maxSpan, zoomStartSpan * std::exp2 ((e.position.y - dragStart.y) / zoomPixelsPerOctave));
                setManualView (zoomAnchorMs - zoomAnchorFraction * span, span);
            }
            else if (dragAxis == DragAxis::pan)
            {
                // The time under the mouse follows it (grab and slide).
                const auto msPerPixel = zoomStartSpan / juce::jmax (1.0f, plot.getWidth());
                setManualView (zoomStartStart - (e.position.x - dragStart.x) * msPerPixel, zoomStartSpan);
            }
        }

        repaint();
    }

    void EnvelopeEditor::mouseUp (const juce::MouseEvent& e)
    {
        endGesture();
        const auto wasView = drag.kind == HitKind::zoom;
        drag = {};
        dragAxis = DragAxis::undecided;
        refit (true);

        if (wasView)
        {
            hover = { HitKind::none, -2 }; // forces the hover cursor to be set again
            mouseMove (e);
        }

        repaint();
    }

    void EnvelopeEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! plot.withBottom (getRulerArea().getBottom()).contains (e.position))
        {
            Component::mouseWheelMove (e, wheel);
            return;
        }

        const auto sign = wheel.isReversed ? -1.0f : 1.0f;

        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            // Wheel up / away zooms in.
            if (! juce::approximatelyEqual (wheel.deltaY, 0.0f))
                zoomAround (e.position.x, std::exp2 (-sign * wheel.deltaY * 2.0f));
            return;
        }

        auto dx = wheel.deltaX;
        if (juce::approximatelyEqual (dx, 0.0f) && e.mods.isShiftDown())
            dx = wheel.deltaY;

        if (juce::approximatelyEqual (dx, 0.0f))
        {
            Component::mouseWheelMove (e, wheel); // plain vertical scrolling is not ours
            return;
        }

        // Like a Viewport: a positive delta moves towards the start.
        panBy (-sign * dx * viewMs);
    }

    void EnvelopeEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        if (scaleFactor > 0.0f && plot.contains (e.position))
            zoomAround (e.position.x, 1.0f / scaleFactor);
    }

    //==============================================================================
    float EnvelopeEditor::playheadTimeFor (std::int64_t samples, double sampleRate, float offsetMs) noexcept
    {
        return (float) ((double) samples * 1000.0 / (sampleRate > 0.0 ? sampleRate : 48000.0)) - offsetMs;
    }

    void EnvelopeEditor::setPlayheadTap (const dsp::PlayheadTap* tap)
    {
        playheadTap = tap;
        playheadSeen = false;
        playheadActive = false;
        playheadAlpha = 0.0f;
    }

    void EnvelopeEditor::updatePlayhead (double nowMs)
    {
        if (playheadTap == nullptr)
            return;

        const auto snap = playheadTap->read();
        const auto oldAlpha = playheadAlpha, oldMs = playheadMs;

        if (! playheadSeen)
        {
            // A hit that was already over when the graph appeared is not shown.
            playheadSeen = true;
            playheadHit = snap.hit;
            playheadActive = snap.active;
            playheadEndedAt = nowMs - playheadFadeMs;
        }

        const auto newHit = snap.hit != playheadHit;
        playheadHit = snap.hit;

        if (snap.active)
        {
            const auto* start = layer >= 0 ? layerStart[(size_t) layer] : nullptr;
            const auto offset = start == nullptr ? 0.0f : start->load(); // the layer's own Start offset
            playheadMs = playheadTimeFor (snap.samples, playheadTap->getSampleRate(), offset);
            playheadActive = true;
            playheadAlpha = 1.0f;
        }
        else
        {
            if (playheadActive || newHit)
                playheadEndedAt = nowMs; // the voice just ended: fade from here (position frozen)

            playheadActive = false;
            playheadAlpha = (float) juce::jlimit (0.0, 1.0, 1.0 - (nowMs - playheadEndedAt) / playheadFadeMs);
        }

        if (! juce::approximatelyEqual (oldAlpha, playheadAlpha) || (playheadAlpha > 0.0f && ! juce::approximatelyEqual (oldMs, playheadMs)))
            repaint(); // only while it is visible or fading
    }

    float EnvelopeEditor::playheadAlphaForGain (float gain) noexcept
    {
        if (! (gain > 0.0f)) // also NaN
            return playheadAlphaFloor;

        const auto db = 20.0f * std::log10 (gain);
        const auto norm = juce::jlimit (0.0f, 1.0f, (db - playheadFloorDb) / -playheadFloorDb);
        return playheadAlphaFloor + (1.0f - playheadAlphaFloor) * norm;
    }

    dsp::MultiPointEnvelope EnvelopeEditor::getPlayheadLevelEnvelope() const
    {
        if (envelope == params::Envelope::master)
            return getEffectiveEnvelope();

        // Body graphs: the Body amp envelope (as drawn; it has no macros).
        constexpr auto amp = params::Envelope::amp;
        dsp::MultiPointEnvelope env;
        env.numNodes = juce::jlimit (2, params::maxNodes, juce::roundToInt (raw[(size_t) amp].count->load()));

        for (int n = 0; n < params::maxNodes; ++n)
        {
            const auto v = readNodeOf (amp, n);
            env.nodes[(size_t) n] = { v.time, v.value, v.curve };
        }

        env.sanitise();
        return env;
    }

    float EnvelopeEditor::getPlayheadLevelGainAt (float ms) const
    {
        if (envelope == params::Envelope::master && isBypassedLook())
            return 1.0f; // envelope off: unity gain, so the playhead stays at full brightness

        if (shape == Shape::ahd)
            return dsp::AhdEnvelope { ahdParam (0), ahdParam (1), ahdParam (2), ahdParam (3) }.evaluate (juce::jmax (0.0f, ms));

        if (shape == Shape::click)
            return clickLevelAt (juce::jmax (0.0f, ms));

        return getPlayheadLevelEnvelope().evaluate (juce::jmax (0.0f, ms));
    }

    std::optional<float> EnvelopeEditor::getPlayheadX() const noexcept
    {
        if (playheadAlpha <= 0.0f || playheadMs < viewStartMs || playheadMs > viewStartMs + viewMs)
            return std::nullopt;

        return timeToX (playheadMs);
    }

    void EnvelopeEditor::tick()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        checkForChanges();
        advanceViewAnimation (now);
        advanceCrossfade (now);
        updatePlayhead (now);
    }

    void EnvelopeEditor::startCrossfade()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;

        crossfadeAmount = 0.0f; // capture without an older fade on top
        crossfadeImage = createComponentSnapshot (getLocalBounds(), true, juce::Component::getApproximateScaleFactorForComponent (this));
        crossfadeAmount = 1.0f;
        crossfadeStartMs = -1.0;
        repaint();
    }

    void EnvelopeEditor::advanceCrossfade (double nowMs)
    {
        if (crossfadeAmount <= 0.0f)
            return;

        if (crossfadeStartMs < 0.0)
            crossfadeStartMs = nowMs;

        const auto t = juce::jlimit (0.0, 1.0, (nowMs - crossfadeStartMs) / crossfadeMs);
        crossfadeAmount = (float) (1.0 - t * t * (3.0 - 2.0 * t)); // smoothstep out

        if (t >= 1.0)
        {
            crossfadeAmount = 0.0f;
            crossfadeImage = {};
        }

        repaint();
    }

    void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto hit = hitTestPoint (e.position);

        if (hit.kind == HitKind::node)
            removeNode (hit.index);
        else if ((hit.kind == HitKind::zoom || hit.kind == HitKind::line) && plot.contains (e.position) && shape == Shape::points)
            addNodeAt (e.position);
        else if (hit.kind == HitKind::none && getRulerArea().contains (e.position))
            resetView(); // double-click the time ruler: fit the envelope again
    }

    bool EnvelopeEditor::addNodeAt (juce::Point<float> position)
    {
        const auto count = getNumNodes();

        if (count >= params::maxNodes || shape != Shape::points)
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

        if (count <= 2 || node <= 0 || node >= count || shape != Shape::points)
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
        hash = hash * 1000003u ^ (juce::uint64) ((int) shape * 16 + layer + 1);

        if (shape == Shape::click)
            for (const auto* p : { layerRaw[0].type, layerRaw[0].source })
                hash = hash * 1000003u ^ (juce::uint64) (juce::int64) juce::roundToInt (p != nullptr ? p->load() * 10.0f : 0.0f);
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
                refit (true); // a new extent (points, Time Scale, preset, undo, Random, on / off)

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
        const auto colour = getCurveColour().withMultipliedAlpha (bypassed ? 0.4f : 1.0f);

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

        paintPlayhead (g);

        const auto count = getNumNodes();

        // Curve handles and nodes.
        for (int s = 0; s < count - 1; ++s)
        {
            if (! isHandleEditable (s))
                continue;

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

        if (crossfadeAmount > 0.0f && crossfadeImage.isValid())
        {
            // The previous view fades out, sliding a little to the side it came from.
            const auto slide = (1.0f - crossfadeAmount) * 10.0f;
            g.setOpacity (crossfadeAmount);
            g.drawImage (crossfadeImage, getLocalBounds().toFloat().translated (-slide, 0.0f));
            g.setOpacity (1.0f);
        }
    }

    void EnvelopeEditor::paintPlayhead (juce::Graphics& g) const
    {
        // v0.3.4: the live hit. Clipped to the plot; nothing when it is off view or faded out.
        const auto x = getPlayheadX();

        if (! x.has_value())
            return;

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        // v0.3.5: brightness follows the level being played (computed here from the envelope
        // shape; the envelopes are fixed-size values, so nothing allocates).
        const auto alphaAt = [this] (float ms) { return playheadAlphaForGain (getPlayheadLevelGainAt (ms)); };
        const auto fadeAlpha = playheadAlpha;
        const auto alpha = fadeAlpha * alphaAt (playheadMs);
        const auto head = Palette::textPrimary;

        // Faint played region.
        const auto left = juce::jmax (plot.getX(), timeToX (0.0f));
        g.setColour (head.withAlpha (0.035f * alpha));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, plot.getY(), *x, plot.getBottom()));

        // Soft trail: alpha strips behind the line (a ColourGradient would allocate). Each strip
        // takes the level at its own time, so the trail fades along the curve.
        constexpr int strips = 12;
        constexpr float stripWidth = 2.0f;

        for (int i = 0; i < strips; ++i)
        {
            const auto stripX = *x - stripWidth * (float) (i + 1);
            const auto fade = 1.0f - (float) i / (float) strips;
            const auto stripAlpha = fadeAlpha * alphaAt (xToTime (stripX + stripWidth * 0.5f));
            g.setColour (head.withAlpha (0.09f * fade * fade * stripAlpha));
            g.fillRect (juce::Rectangle<float> (stripX, plot.getY(), stripWidth, plot.getHeight()));
        }

        g.setColour (head.withAlpha (0.85f * alpha));
        g.fillRect (juce::Rectangle<float> (*x - 0.75f, plot.getY(), 1.5f, plot.getHeight()));

        // A dot riding the curve at the current gain / pitch (unity while the master is bypassed).
        float y;
        if (envelope == params::Envelope::master)
            y = isBypassedLook() ? valueToY (1.0f)
              : showsEffectiveCurve() ? valueToY (getEffectiveEnvelope().evaluate (playheadMs))
                                      : curveYAt (playheadMs);
        else
            y = curveYAt (playheadMs);

        const auto colour = getCurveColour();
        g.setColour (colour.withAlpha (0.25f * alpha));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ *x, y }));
        g.setColour (head.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ *x, y }));
    }
}
