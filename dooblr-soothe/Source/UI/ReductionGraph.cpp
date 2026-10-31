#include "ReductionGraph.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "Theme.h"

namespace dooblrsoothe::ui
{
    using namespace params;

    namespace
    {
        constexpr float logSpan = 6.907755279f; // ln (20000 / 20)

        juce::String formatFrequency (float f)
        {
            return f >= 1000.0f ? params::formatValue (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : params::formatValue (f, 0) + " Hz";
        }
    }

    ReductionGraph::ReductionGraph (DooblrSootheProcessor& p)
        : processor (p), state (p.getState())
    {
        setOpaque (true);
        setComponentID ("graph");
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { poll(); });
    }

    ReductionGraph::~ReductionGraph() = default;

    void ReductionGraph::setSelectedBand (int band)
    {
        band = juce::jlimit (0, numBands - 1, band);
        if (band == selectedBand)
            return;
        selectedBand = band;
        rebuildCurve();
        repaint();
    }

    bool ReductionGraph::poll()
    {
        bool dirty = false;

        if (const auto* fresh = processor.getEngine().getDisplay().read())
        {
            if (fresh->serial != frameSerial)
            {
                frame = *fresh;
                frameSerial = fresh->serial;
                dirty = true;
            }
        }

        const auto now = processor.readBands();
        if (! bandsValid || now != bands)
        {
            bands = now;
            bandsValid = true;
            rebuildCurve();
            dirty = true;
        }

        if (dirty)
            repaint();
        return dirty;
    }

    //==============================================================================
    float ReductionGraph::xForFrequency (float f) const noexcept
    {
        const auto t = std::log (juce::jlimit (minFreq, maxFreq, f) / minFreq) / logSpan;
        return curveArea.getX() + t * curveArea.getWidth();
    }

    float ReductionGraph::frequencyForX (float x) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - curveArea.getX()) / std::max (1.0f, curveArea.getWidth()));
        return minFreq * std::exp (t * logSpan);
    }

    float ReductionGraph::yForCurveDb (float db) const noexcept
    {
        return juce::jmap (juce::jlimit (-curveRangeDb, curveRangeDb, db), curveRangeDb, -curveRangeDb,
                           curveArea.getY() + nodeRadius, curveArea.getBottom() - nodeRadius);
    }

    float ReductionGraph::curveDbForY (float y) const noexcept
    {
        const auto db = juce::jmap (y, curveArea.getY() + nodeRadius, curveArea.getBottom() - nodeRadius, curveRangeDb, -curveRangeDb);
        return juce::jlimit (-curveRangeDb, curveRangeDb, db);
    }

    juce::Point<float> ReductionGraph::getNodePosition (int band) const
    {
        const auto& b = bands[(size_t) juce::jlimit (0, numBands - 1, band)];
        const auto y = shapeUsesGain (b.shape) ? yForCurveDb (b.gain) : yForCurveDb (0.0f);
        return { xForFrequency (b.freq), y };
    }

    int ReductionGraph::bandAt (juce::Point<float> position) const
    {
        int best = -1;
        float bestDistance = nodeRadius + 4.0f;
        for (int b = 0; b < numBands; ++b)
        {
            if (! bands[(size_t) b].active)
                continue;
            const auto d = getNodePosition (b).getDistanceFrom (position);
            if (d <= bestDistance || (d == bestDistance && b == selectedBand))
            {
                best = b;
                bestDistance = d;
            }
        }
        return best;
    }

    //==============================================================================
    void ReductionGraph::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        area.removeFromLeft (34.0f);
        area.removeFromRight (30.0f);
        area.removeFromBottom (16.0f);
        area.removeFromTop (18.0f);
        const auto gap = 22.0f;
        spectrumArea = area.removeFromTop ((area.getHeight() - gap) * 0.58f);
        area.removeFromTop (gap);
        curveArea = area;
        background = {};
        rebuildCurve();
    }

    void ReductionGraph::drawBackground()
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (getWidth() * scale)),
                                  std::max (1, juce::roundToInt (getHeight() * scale)), true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        g.fillAll (Palette::panel);
        g.setFont (juce::FontOptions (10.5f));

        const auto caption = [&g] (juce::Rectangle<float> pane, const juce::String& text)
        {
            g.setColour (Palette::textMuted);
            g.drawText (text, pane.withY (pane.getY() - 17.0f).withHeight (15.0f).toNearestInt(), juce::Justification::centredLeft);
        };
        caption (spectrumArea, "SPECTRUM / REDUCTION");
        caption (curveArea, "DEPTH CURVE");

        for (auto pane : { spectrumArea, curveArea })
        {
            g.setColour (Palette::background);
            g.fillRoundedRectangle (pane, 4.0f);
        }

        // Frequency grid, shared by both panes.
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto x = xForFrequency (f);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), spectrumArea.getY(), spectrumArea.getBottom());
            g.drawVerticalLine (juce::roundToInt (x), curveArea.getY(), curveArea.getBottom());
            g.setColour (Palette::textMuted);
            const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, curveArea.getBottom() + 2.0f, 40.0f, 14.0f).toNearestInt(),
                        juce::Justification::centred);
        }

        // Spectrum levels (left) and reduction (right).
        for (float db : { -72.0f, -54.0f, -36.0f, -18.0f })
        {
            const auto y = juce::jmap (db, spectrumFloorDb, 0.0f, spectrumArea.getBottom(), spectrumArea.getY());
            g.setColour (Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), spectrumArea.getX(), spectrumArea.getRight());
            g.setColour (Palette::textMuted);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (spectrumArea.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
        for (float cut : { 10.0f, 20.0f })
        {
            const auto y = spectrumArea.getY() + cut / reductionRangeDb * spectrumArea.getHeight();
            g.setColour (Palette::dynamic.withAlpha (0.7f));
            g.drawText ("-" + juce::String ((int) cut), juce::Rectangle<float> (spectrumArea.getRight() + 4.0f, y - 7.0f, 28.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }

        // Depth curve scale.
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const auto y = yForCurveDb (db);
            g.setColour (db == 0.0f ? Palette::divider : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), curveArea.getX(), curveArea.getRight());
            g.setColour (Palette::textMuted);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (curveArea.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
    }

    void ReductionGraph::rebuildCurve()
    {
        curvePath.clear();
        selectedBandPath.clear();
        if (curveArea.isEmpty())
            return;

        const auto steps = std::max (2, (int) curveArea.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = curveArea.getX() + curveArea.getWidth() * (float) i / (float) steps;
            const auto f = frequencyForX (x);
            const auto y = yForCurveDb (dsp::curveDb (bands, f, -curveRangeDb - 6.0f));
            if (i == 0) curvePath.startNewSubPath (x, y); else curvePath.lineTo (x, y);

            const auto& sel = bands[(size_t) selectedBand];
            if (sel.active)
            {
                const auto sy = yForCurveDb (dsp::bandDb (sel, f, -curveRangeDb - 6.0f));
                if (i == 0) selectedBandPath.startNewSubPath (x, sy); else selectedBandPath.lineTo (x, sy);
            }
        }
    }

    void ReductionGraph::paint (juce::Graphics& g)
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        if (! background.isValid() || scale != backgroundScale)
            drawBackground();
        g.drawImage (background, getLocalBounds().toFloat());

        // Spectrum and reduction.
        if (frameSerial >= 0)
        {
            const auto n = dsp::DisplayFrame::numPoints;
            const auto yLevel = [this] (float db)
            {
                return juce::jmap (juce::jlimit (spectrumFloorDb, 0.0f, db), spectrumFloorDb, 0.0f, spectrumArea.getBottom(), spectrumArea.getY());
            };
            inputPath.clear();
            outputPath.clear();
            reductionPath.clear();
            inputPath.startNewSubPath (spectrumArea.getX(), spectrumArea.getBottom());
            reductionPath.startNewSubPath (spectrumArea.getX(), spectrumArea.getY());
            for (int i = 0; i < n; ++i)
            {
                const auto x = xForFrequency (dsp::DisplayFrame::pointFrequency (i));
                inputPath.lineTo (x, yLevel (frame.input[(size_t) i]));
                if (i == 0) outputPath.startNewSubPath (x, yLevel (frame.output[(size_t) i]));
                else        outputPath.lineTo (x, yLevel (frame.output[(size_t) i]));
                const auto cut = juce::jlimit (0.0f, reductionRangeDb, frame.reduction[(size_t) i]);
                reductionPath.lineTo (x, spectrumArea.getY() + cut / reductionRangeDb * spectrumArea.getHeight());
            }
            inputPath.lineTo (spectrumArea.getRight(), spectrumArea.getBottom());
            inputPath.closeSubPath();
            reductionPath.lineTo (spectrumArea.getRight(), spectrumArea.getY());
            reductionPath.closeSubPath();

            g.saveState();
            g.reduceClipRegion (spectrumArea.toNearestInt());
            g.setColour (Palette::spectrum.withAlpha (0.11f));
            g.fillPath (inputPath);
            g.setColour (Palette::spectrum.withAlpha (0.32f));
            g.strokePath (inputPath, juce::PathStrokeType (1.0f));
            g.setColour (Palette::textPrimary.withAlpha (0.75f));
            g.strokePath (outputPath, juce::PathStrokeType (1.3f));
            g.setColour (Palette::dynamic.withAlpha (0.45f));
            g.fillPath (reductionPath);
            g.setColour (Palette::dynamic);
            g.strokePath (reductionPath, juce::PathStrokeType (1.2f));
            g.restoreState();
        }

        // Depth curve and nodes.
        g.saveState();
        g.reduceClipRegion (curveArea.toNearestInt());
        if (! selectedBandPath.isEmpty())
        {
            g.setColour (bandColour (selectedBand).withAlpha (0.45f));
            g.strokePath (selectedBandPath, juce::PathStrokeType (1.0f));
        }
        g.setColour (Palette::accent);
        g.strokePath (curvePath, juce::PathStrokeType (2.0f));
        g.restoreState();

        const auto listen = (int) state.getRawParameterValue (id::listenBand)->load() - 1;
        for (int b = 0; b < numBands; ++b)
        {
            if (! bands[(size_t) b].active)
                continue;
            const auto c = getNodePosition (b);
            const auto colour = bandColour (b);
            const auto r = nodeRadius + (b == hoverBand || b == dragBand ? 1.5f : 0.0f);
            g.setColour (b == selectedBand ? colour : colour.withAlpha (0.35f));
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
            g.setColour (b == listen ? Palette::textPrimary : colour);
            g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, b == listen ? 2.0f : 1.2f);
            g.setColour (b == selectedBand ? Palette::background : Palette::textPrimary);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (c.x - r, c.y - r, r * 2.0f, r * 2.0f).toNearestInt(), juce::Justification::centred);
        }

        // Hover readout.
        if (hovering || dragBand >= 0)
        {
            const auto text = readoutText();
            if (text.isNotEmpty())
            {
                g.setFont (juce::FontOptions (11.5f));
                g.setColour (Palette::textPrimary);
                g.drawText (text, juce::Rectangle<float> (spectrumArea.getX(), 6.0f, spectrumArea.getWidth(), 14.0f).toNearestInt(),
                            juce::Justification::centredRight);
            }
        }
    }

    juce::String ReductionGraph::readoutText() const
    {
        const auto band = dragBand >= 0 ? dragBand : hoverBand;
        if (band >= 0)
        {
            const auto& b = bands[(size_t) band];
            auto text = "Band " + juce::String (band + 1) + "  " + shapeNames()[(int) b.shape] + "  " + formatFrequency (b.freq);
            if (shapeUsesGain (b.shape))
                text << "  " << (std::abs (b.gain) >= 0.05f && b.gain > 0 ? "+" : "") << params::formatValue (b.gain, 1) << " dB";
            text << "  Q " << params::formatValue (b.q, 2);
            return text;
        }

        if (hoverPosition.x < curveArea.getX() || hoverPosition.x > curveArea.getRight())
            return {};
        const auto f = frequencyForX (hoverPosition.x);
        auto text = formatFrequency (f);
        if (curveArea.contains (hoverPosition))
            text << "  curve " << params::formatValue (dsp::curveDb (bands, f, -60.0f), 1) << " dB";
        else if (frameSerial >= 0)
        {
            const auto t = std::log (f / dsp::DisplayFrame::minFreq) / logSpan;
            const auto i = juce::jlimit (0, dsp::DisplayFrame::numPoints - 1, juce::roundToInt (t * (dsp::DisplayFrame::numPoints - 1)));
            text << "  in " << params::formatValue (frame.input[(size_t) i], 1) << " dB  cut " << params::formatValue (frame.reduction[(size_t) i], 1) << " dB";
        }
        return text;
    }

    //==============================================================================
    void ReductionGraph::setParam (const juce::String& pid, float value)
    {
        if (auto* p = state.getParameter (pid))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void ReductionGraph::beginGesture (const juce::String& pid)
    {
        if (auto* p = state.getParameter (pid))
            p->beginChangeGesture();
    }

    void ReductionGraph::endGesture (const juce::String& pid)
    {
        if (auto* p = state.getParameter (pid))
            p->endChangeGesture();
    }

    void ReductionGraph::mouseMove (const juce::MouseEvent& e)
    {
        hovering = true;
        hoverPosition = e.position;
        const auto band = bandAt (e.position);
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        hoverBand = band;
        repaint();
    }

    void ReductionGraph::mouseExit (const juce::MouseEvent&)
    {
        hovering = false;
        hoverBand = -1;
        repaint();
    }

    void ReductionGraph::mouseDown (const juce::MouseEvent& e)
    {
        poll();
        const auto band = bandAt (e.position);
        if (band < 0)
            return;

        setSelectedBand (band);
        if (onBandSelected)
            onBandSelected (band);

        if (e.mods.isPopupMenu())
        {
            showBandMenu (band);
            return;
        }

        dragBand = band;
        dragMoved = false;
        dragPosition = getNodePosition (band);
        lastDragMouse = e.position;
    }

    void ReductionGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragBand < 0)
            return;

        if (! dragMoved)
        {
            if (e.getDistanceFromDragStart() < 2)
                return;
            dragMoved = true;
            beginGesture (bandFreq (dragBand));
            beginGesture (bandGain (dragBand));
        }

        const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        dragPosition += (e.position - lastDragMouse) * fine;
        lastDragMouse = e.position;

        setParam (bandFreq (dragBand), frequencyForX (dragPosition.x));
        if (shapeUsesGain (bands[(size_t) dragBand].shape))
            setParam (bandGain (dragBand), curveDbForY (dragPosition.y));
        hoverPosition = e.position;
        poll();
    }

    void ReductionGraph::mouseUp (const juce::MouseEvent&)
    {
        if (dragBand >= 0 && dragMoved)
        {
            endGesture (bandFreq (dragBand));
            endGesture (bandGain (dragBand));
        }
        dragBand = -1;
        dragMoved = false;
        repaint();
    }

    void ReductionGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        const auto band = bandAt (e.position);
        if (band >= 0)
            removeBand (band);
        else if (spectrumArea.contains (e.position) || curveArea.contains (e.position))
            addBandAt (e.position);
    }

    void ReductionGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        auto band = bandAt (e.position);
        if (band < 0)
            return;

        const auto& b = bands[(size_t) band];
        const auto delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
        if (delta == 0.0f)
            return;
        const auto q = juce::jlimit (minQ, maxQ, b.q * std::pow (2.0f, delta * (e.mods.isShiftDown() ? 0.5f : 2.0f)));
        beginGesture (bandQ (band));
        setParam (bandQ (band), q);
        endGesture (bandQ (band));
        poll();
    }

    void ReductionGraph::addBandAt (juce::Point<float> position)
    {
        poll();
        int band = -1;
        for (int b = 0; b < numBands && band < 0; ++b)
            if (! bands[(size_t) b].active)
                band = b;
        if (band < 0)
            return;

        const auto f = frequencyForX (position.x);
        const auto t = (position.x - curveArea.getX()) / std::max (1.0f, curveArea.getWidth());
        const auto shape = t < 0.08f ? Shape::lowCut : (t > 0.92f ? Shape::highCut : Shape::bell);
        const auto gain = curveArea.contains (position) && shapeUsesGain (shape) ? curveDbForY (position.y) : 0.0f;

        const juce::String ids[] { bandActive (band), bandShape (band), bandFreq (band), bandGain (band), bandQ (band) };
        for (auto& pid : ids) beginGesture (pid);
        setParam (bandActive (band), 1.0f);
        setParam (bandShape (band), (float) (int) shape);
        setParam (bandFreq (band), f);
        setParam (bandGain (band), gain);
        setParam (bandQ (band), 1.0f);
        for (auto& pid : ids) endGesture (pid);

        poll();
        setSelectedBand (band);
        if (onBandSelected)
            onBandSelected (band);
    }

    void ReductionGraph::removeBand (int band)
    {
        const auto listening = (int) state.getRawParameterValue (id::listenBand)->load() == band + 1;
        beginGesture (bandActive (band));
        setParam (bandActive (band), 0.0f);
        endGesture (bandActive (band));
        if (listening)
        {
            beginGesture (id::listenBand);
            setParam (id::listenBand, 0.0f);
            endGesture (id::listenBand);
        }
        hoverBand = -1;
        poll();
    }

    void ReductionGraph::showBandMenu (int band)
    {
        juce::PopupMenu menu;
        const auto names = shapeNames();
        const auto current = (int) bands[(size_t) band].shape;
        for (int s = 0; s < names.size(); ++s)
            menu.addItem (1 + s, names[s], true, s == current);
        menu.addSeparator();
        const auto listening = (int) state.getRawParameterValue (id::listenBand)->load() == band + 1;
        menu.addItem (100, "Listen", true, listening);
        menu.addItem (101, "Remove band");

        const auto node = getNodePosition (band);
        juce::Component::SafePointer<ReductionGraph> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (juce::roundToInt (node.x), juce::roundToInt (node.y), 1, 1))),
                            [safe, band, listening] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                auto& self = *safe;
                                if (result >= 1 && result <= numShapes)
                                {
                                    self.beginGesture (bandShape (band));
                                    self.setParam (bandShape (band), (float) (result - 1));
                                    self.endGesture (bandShape (band));
                                }
                                else if (result == 100)
                                {
                                    self.beginGesture (id::listenBand);
                                    self.setParam (id::listenBand, listening ? 0.0f : (float) (band + 1));
                                    self.endGesture (id::listenBand);
                                }
                                else if (result == 101)
                                    self.removeBand (band);
                                self.poll();
                            });
    }
}
