#include "CompressionDisplay.h"

namespace dooblrdistressor::ui
{
    namespace
    {
        using Map = CompressionMap;
        using Handle = CompressionDisplay::Handle;

        void idsFor (Handle handle, juce::String& a, juce::String& b)
        {
            namespace id = params::id;
            a = b = {};
            switch (handle)
            {
                case Handle::knee:    a = id::input;  b = id::output; break;
                case Handle::slope:   a = id::ratio;                  break;
                case Handle::attack:  a = id::attack;                 break;
                case Handle::release: a = id::release;                break;
                case Handle::none:    break;
            }
        }
    }

    CompressionDisplay::CompressionDisplay (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void CompressionDisplay::pushMeters (float grDb, float inPeakL, float inPeakR, float, float, double nowMs)
    {
        const auto dt = lastNowMs > 0.0 ? juce::jlimit (0.0, 0.1, (nowMs - lastNowMs) * 0.001) : 1.0 / GrMeterModel::historyHz;
        lastNowMs = nowMs;
        meter.tick (grDb, dt, nowMs);
        inPeak = juce::jmax (inPeakL, inPeakR);
        repaint();
    }

    CompressionMap::Params CompressionDisplay::readParams() const
    {
        Map::Params p;
        auto read = [this] (const char* pid) -> float
        {
            if (auto* q = state.getParameter (pid))
                return q->convertFrom0to1 (q->getValue());
            return 0.0f;
        };
        p.input = read (params::id::input);
        p.output = read (params::id::output);
        p.attack = read (params::id::attack);
        p.release = read (params::id::release);
        p.ratio = Map::clampRatio ((int) std::lround ((double) read (params::id::ratio)));
        return p;
    }

    void CompressionDisplay::writeParam (const juce::String& pid, float value)
    {
        if (auto* q = state.getParameter (pid))
            q->setValueNotifyingHost (q->convertTo0to1 (value));
    }

    void CompressionDisplay::beginGesture (const juce::String& pid)
    {
        if (auto* q = state.getParameter (pid))
            q->beginChangeGesture();
    }

    void CompressionDisplay::endGesture (const juce::String& pid)
    {
        if (auto* q = state.getParameter (pid))
            q->endChangeGesture();
    }

    void CompressionDisplay::endOpenGestures()
    {
        if (! gestureOpen)
            return;
        if (gestureA.isNotEmpty()) endGesture (gestureA);
        if (gestureB.isNotEmpty()) endGesture (gestureB);
        gestureOpen = false;
        gestureA = gestureB = {};
    }

    juce::Point<float> CompressionDisplay::toPlot (juce::Rectangle<float> area, float nx, float ny) const noexcept
    {
        return { area.getX() + Map::clamp01 (nx) * area.getWidth(),
                 area.getBottom() - Map::clamp01 (ny) * area.getHeight() };
    }

    float CompressionDisplay::xToNorm (juce::Rectangle<float> area, float x) const noexcept
    {
        return area.getWidth() > 1.0f ? (x - area.getX()) / area.getWidth() : 0.0f;
    }

    float CompressionDisplay::yToNorm (juce::Rectangle<float> area, float y) const noexcept
    {
        return area.getHeight() > 1.0f ? (area.getBottom() - y) / area.getHeight() : 0.0f;
    }

    float CompressionDisplay::dbToX (float db) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (db - plotMinDb) / (plotMaxDb - plotMinDb));
        return curveArea.getX() + t * curveArea.getWidth();
    }

    float CompressionDisplay::dbToYCurve (float db) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (db - plotMinDb) / (plotMaxDb - plotMinDb));
        return curveArea.getBottom() - t * curveArea.getHeight();
    }

    float CompressionDisplay::dbToYGr (juce::Rectangle<float> area, float db) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, db / GrMeterModel::rangeDb);
        return area.getY() + t * area.getHeight();
    }

    juce::Point<float> CompressionDisplay::handleCentre (Handle handle) const
    {
        const auto pts = Map::fromParams (readParams());
        switch (handle)
        {
            case Handle::knee:    return toPlot (curveArea, pts.kneeX, pts.kneeY);
            case Handle::slope:   return toPlot (curveArea, 0.92f, 1.0f - pts.slopeT);
            case Handle::attack:  return toPlot (historyArea, 0.06f + 0.38f * pts.attackT, 0.78f);
            case Handle::release: return toPlot (historyArea, 0.56f + 0.38f * pts.releaseT, 0.78f);
            case Handle::none:    break;
        }
        return {};
    }

    CompressionDisplay::Handle CompressionDisplay::handleAt (juce::Point<float> position) const
    {
        Handle best = Handle::none;
        auto bestD = handleHit * handleHit;
        for (auto h : { Handle::knee, Handle::slope, Handle::attack, Handle::release })
        {
            const auto d = position.getDistanceSquaredFrom (handleCentre (h));
            if (d <= bestD)
            {
                bestD = d;
                best = h;
            }
        }
        return best;
    }

    void CompressionDisplay::resized()
    {
        auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        const auto meterW = juce::jlimit (36.0f, 56.0f, bounds.getWidth() * 0.07f);
        const auto curveW = juce::jlimit (180.0f, bounds.getWidth() * 0.42f, bounds.getWidth() * 0.44f);
        curveArea = bounds.removeFromLeft (curveW);
        bounds.removeFromLeft (8.0f);
        meterArea = bounds.removeFromLeft (meterW);
        bounds.removeFromLeft (8.0f);
        historyArea = bounds;
    }

    void CompressionDisplay::drawHandle (juce::Graphics& g, juce::Point<float> centre, juce::Colour colour, bool hot) const
    {
        const auto r = handleRadius + (hot ? 1.5f : 0.0f);
        g.setColour (colour.withAlpha (hot ? 1.0f : 0.85f));
        g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        g.setColour (Palette::textPrimary.withAlpha (hot ? 1.0f : 0.55f));
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, hot ? 2.0f : 1.2f);
    }

    void CompressionDisplay::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto p = readParams();
        const auto curve = dsp::DistressorEngine::curveFor (p.ratio, p.attack, p.release);
        const auto inGainDb = dsp::DistressorEngine::inputGainDb (p.input);
        const auto outGainDb = dsp::DistressorEngine::outputGainDb (p.output) + curve.makeupDb;
        const auto threshShift = (p.input - 5.0f) * 1.2f;
        auto* hrParam = state.getParameter (params::id::headroom);
        const auto headroomDb = (float) params::headroomDbForIndex (
            hrParam != nullptr ? (int) std::lround ((double) hrParam->convertFrom0to1 (hrParam->getValue())) : params::defaultHeadroomIndex);
        const auto drive = dsp::DistressorEngine::headroomDriveDb (headroomDb);

        // Transfer curve (unity dashed, then the compressor I/O).
        {
            g.setColour (Palette::background);
            g.fillRoundedRectangle (curveArea, 4.0f);
            g.setColour (Palette::grid);
            g.drawLine (dbToX (0.0f), curveArea.getY(), dbToX (0.0f), curveArea.getBottom(), 1.0f);
            g.drawLine (curveArea.getX(), dbToYCurve (0.0f), curveArea.getRight(), dbToYCurve (0.0f), 1.0f);
            g.setColour (Palette::divider);
            const float dashes[] { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (dbToX (plotMinDb), dbToYCurve (plotMinDb),
                                                dbToX (plotMaxDb), dbToYCurve (plotMaxDb)),
                              dashes, 2, 1.0f);

            juce::Path io;
            for (int i = 0; i < curvePoints; ++i)
            {
                const auto inDb = plotMinDb + (plotMaxDb - plotMinDb) * (float) i / (float) (curvePoints - 1);
                const auto detector = inDb + inGainDb + drive + threshShift;
                const auto gr = dsp::DistressorEngine::staticReductionDb (detector, p.ratio, p.attack, p.release);
                const auto outDb = inDb + inGainDb + drive + gr + outGainDb - drive;
                const auto x = dbToX (inDb);
                const auto y = dbToYCurve (outDb);
                if (i == 0) io.startNewSubPath (x, y); else io.lineTo (x, y);
            }
            g.setColour (Palette::accent.withAlpha (0.18f));
            auto fill = io;
            fill.lineTo (curveArea.getRight(), curveArea.getBottom());
            fill.lineTo (curveArea.getX(), curveArea.getBottom());
            fill.closeSubPath();
            g.fillPath (fill);
            g.setColour (Palette::accent);
            g.strokePath (io, juce::PathStrokeType (2.0f));

            if (inPeak > 1.0e-5f)
            {
                const auto inDb = dsp::DistressorEngine::gainToDb (inPeak);
                const auto detector = inDb + inGainDb + drive + threshShift;
                const auto gr = dsp::DistressorEngine::staticReductionDb (detector, p.ratio, p.attack, p.release);
                const auto outDb = inDb + inGainDb + gr + outGainDb;
                g.setColour (Palette::textPrimary);
                g.fillEllipse (dbToX (inDb) - 3.5f, dbToYCurve (outDb) - 3.5f, 7.0f, 7.0f);
            }
        }

        // Vertical GR meter with peak-hold tick.
        {
            g.setColour (Palette::background);
            g.fillRoundedRectangle (meterArea, 4.0f);
            const auto shown = meter.getDisplayedDb();
            const auto hold = meter.getPeakHoldDb();
            auto fill = meterArea;
            fill.setHeight (meterArea.getHeight() * juce::jlimit (0.0f, 1.0f, shown / GrMeterModel::rangeDb));
            g.setColour (Palette::dynamic.withAlpha (0.85f));
            g.fillRoundedRectangle (fill, 3.0f);
            const auto holdY = dbToYGr (meterArea, hold);
            g.setColour (Palette::frozen);
            g.fillRect (meterArea.getX() + 2.0f, holdY - 1.0f, meterArea.getWidth() - 4.0f, 2.0f);
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (params::formatValue ((double) shown, 1), meterArea.toNearestInt(), juce::Justification::centred);
        }

        // Scrolling GR history, schematic envelope for the timing handles, no captions.
        {
            g.setColour (Palette::background);
            g.fillRoundedRectangle (historyArea, 4.0f);

            const auto n = meter.getHistoryFilled();
            if (n >= 2)
            {
                juce::Path hist;
                const auto denom = (float) (GrMeterModel::historyLength - 1);
                for (int i = 0; i < n; ++i)
                {
                    const auto slot = GrMeterModel::historyLength - n + i;
                    const auto x = historyArea.getX() + historyArea.getWidth() * (float) slot / denom;
                    const auto y = dbToYGr (historyArea, meter.historyOldest (i));
                    if (i == 0) hist.startNewSubPath (x, y); else hist.lineTo (x, y);
                }
                auto fill = hist;
                const auto right = historyArea.getX() + historyArea.getWidth() * (float) (GrMeterModel::historyLength - 1) / denom;
                fill.lineTo (right, historyArea.getY());
                fill.lineTo (historyArea.getX() + historyArea.getWidth() * (float) (GrMeterModel::historyLength - n) / denom,
                             historyArea.getY());
                fill.closeSubPath();
                g.setColour (Palette::dynamic.withAlpha (0.26f));
                g.fillPath (fill);
                g.setColour (Palette::dynamic.withAlpha (0.9f));
                g.strokePath (hist, juce::PathStrokeType (1.4f));
            }

            juce::Path envelope;
            const auto riseEnd = handleCentre (Handle::attack);
            const auto fallEnd = handleCentre (Handle::release);
            const auto topY = historyArea.getY() + historyArea.getHeight() * 0.22f;
            envelope.startNewSubPath (historyArea.getX() + 4.0f, historyArea.getY() + 4.0f);
            envelope.lineTo (riseEnd.x, topY);
            envelope.lineTo (historyArea.getX() + historyArea.getWidth() * 0.54f, topY);
            envelope.lineTo (fallEnd.x, fallEnd.y);
            g.setColour (Palette::spectrum.withAlpha (0.45f));
            g.strokePath (envelope, juce::PathStrokeType (1.2f));
        }

        drawHandle (g, handleCentre (Handle::knee), Palette::accent, hover == Handle::knee || drag == Handle::knee);
        drawHandle (g, handleCentre (Handle::slope), Palette::dynamic, hover == Handle::slope || drag == Handle::slope);
        drawHandle (g, handleCentre (Handle::attack), Palette::dynamic, hover == Handle::attack || drag == Handle::attack);
        drawHandle (g, handleCentre (Handle::release), Palette::dynamic, hover == Handle::release || drag == Handle::release);
    }

    void CompressionDisplay::mouseMove (const juce::MouseEvent& e)
    {
        hover = handleAt (e.position);
        setMouseCursor (hover != Handle::none ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void CompressionDisplay::mouseExit (const juce::MouseEvent&)
    {
        hover = Handle::none;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void CompressionDisplay::mouseDown (const juce::MouseEvent& e)
    {
        drag = handleAt (e.position);
        hover = drag;
        if (drag == Handle::none)
            return;
        juce::String a, b;
        idsFor (drag, a, b);
        beginGesture (a);
        if (b.isNotEmpty()) beginGesture (b);
        gestureOpen = true;
        gestureA = a;
        gestureB = b;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void CompressionDisplay::applyDrag (juce::Point<float> position)
    {
        auto p = readParams();
        switch (drag)
        {
            case Handle::knee:
                p = Map::applyKnee (p, xToNorm (curveArea, position.x), yToNorm (curveArea, position.y));
                writeParam (params::id::input, p.input);
                writeParam (params::id::output, p.output);
                break;
            case Handle::slope:
                p = Map::applySlope (p, 1.0f - yToNorm (curveArea, position.y));
                writeParam (params::id::ratio, (float) p.ratio);
                break;
            case Handle::attack:
                p = Map::applyAttack (p, (xToNorm (historyArea, position.x) - 0.06f) / 0.38f);
                writeParam (params::id::attack, p.attack);
                break;
            case Handle::release:
                p = Map::applyRelease (p, (xToNorm (historyArea, position.x) - 0.56f) / 0.38f);
                writeParam (params::id::release, p.release);
                break;
            case Handle::none:
                break;
        }
    }

    void CompressionDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Handle::none)
            return;
        const auto fine = e.mods.isShiftDown();
        auto pos = e.position;
        if (fine)
        {
            const auto centre = handleCentre (drag);
            pos = centre + (e.position - centre) * 0.2f;
        }
        applyDrag (pos);
        repaint();
    }

    void CompressionDisplay::mouseUp (const juce::MouseEvent&)
    {
        endOpenGestures();
        drag = Handle::none;
    }

    void CompressionDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto handle = handleAt (e.position);
        if (handle == Handle::none)
            return;

        juce::String a, b;
        idsFor (handle, a, b);
        beginGesture (a);
        if (b.isNotEmpty()) beginGesture (b);

        switch (handle)
        {
            case Handle::knee:
                writeParam (params::id::input, Map::knobDefault);
                writeParam (params::id::output, Map::knobDefault);
                break;
            case Handle::slope:
                writeParam (params::id::ratio, (float) Map::defaultRatio);
                break;
            case Handle::attack:
                writeParam (params::id::attack, Map::knobDefault);
                break;
            case Handle::release:
                writeParam (params::id::release, Map::knobDefault);
                break;
            case Handle::none:
                break;
        }

        if (b.isNotEmpty()) endGesture (b);
        endGesture (a);
        hover = handle;
        repaint();
    }
}
