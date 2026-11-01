#include "FeedbackGraph.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "DisplayBallistics.h"
#include "Theme.h"

namespace dooblrfeedback::ui
{
    namespace
    {
        constexpr float logSpan = 6.907755279f; // ln (20000 / 20)

        juce::String timeText (juce::int64 ms)
        {
            return ms > 0 ? juce::Time (ms).formatted ("%H:%M:%S") : juce::String ("-");
        }

        juce::String cutText (float cut)
        {
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + params::formatValue (cut, 1) + " dB";
        }

        bool sameNotches (const dsp::NotchArray& a, const dsp::NotchArray& b)
        {
            for (size_t i = 0; i < a.size(); ++i)
            {
                const auto& x = a[i];
                const auto& y = b[i];
                if (x.active != y.active) return false;
                if (! x.active) continue;
                if (x.id != y.id || x.fixed != y.fixed || x.ringing != y.ringing || x.hits != y.hits
                    || std::abs (x.freq - y.freq) > 0.01f || std::abs (x.cutDb - y.cutDb) > 0.01f || std::abs (x.q - y.q) > 0.01f)
                    return false;
            }
            return true;
        }

        juce::Path padlock (juce::Rectangle<float> box, bool closed)
        {
            juce::Path p;
            const auto body = juce::Rectangle<float> (box.getWidth() * 0.62f, box.getHeight() * 0.42f)
                                  .withCentre ({ box.getCentreX(), box.getY() + box.getHeight() * 0.66f });
            p.addRoundedRectangle (body, 1.2f);
            const auto r = body.getWidth() * 0.32f;
            const auto cx = closed ? body.getCentreX() : body.getCentreX() + r * 0.9f;
            juce::Path shackle;
            shackle.addCentredArc (cx, body.getY(), r, r * 1.15f, 0.0f, -juce::MathConstants<float>::halfPi,
                                   juce::MathConstants<float>::halfPi, true);
            p.addPath (shackle);
            return p;
        }
    }

    FeedbackGraph::FeedbackGraph (DooblrFeedbackProcessor& p) : processor (p)
    {
        setOpaque (true);
        setComponentID ("graph");
        markers.reserve (dsp::maxNotches);
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { poll(); });
        refreshNotches();
    }

    FeedbackGraph::~FeedbackGraph() = default;

    //==============================================================================
    bool FeedbackGraph::poll()
    {
        auto dirty = false;
        auto fresh = false;
        if (processor.getEngine().getDisplay().read (target))
        {
            hasFrame = true;
            fresh = true;
        }

        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const auto dt = lastTickMs > 0.0 ? std::clamp ((nowMs - lastTickMs) * 0.001, 0.0, 0.25) : 0.0;
        lastTickMs = nowMs;
        if (hasFrame && (fresh || settling || ! shownValid))
            dirty = advanceDisplay (dt) || fresh;

        const auto now = processor.getNotches();
        if (! sameNotches (now, notches))
        {
            notches = now;
            layoutMarkers();
            rebuildCurve();
            dirty = true;
        }

        if (dirty) repaint();
        return dirty;
    }

    bool FeedbackGraph::advanceDisplay (double dtSeconds)
    {
        if (! hasFrame) return false;
        if (! shownValid)
        {
            snapDisplay();
            return true;
        }
        const auto times = analyzerTimes (analyzerSpeedFromIndex (processor.getAnalyzerSpeed()));
        constexpr auto n = (std::size_t) dsp::DisplayFrame::numPoints;
        auto biggest = applyBallistics (target.input.data(), frame.input.data(), n, dtSeconds, times);
        biggest = std::max (biggest, applyBallistics (target.output.data(), frame.output.data(), n, dtSeconds, times));
        frame.binHz = target.binHz;
        frame.serial = target.serial;
        settling = biggest > 0.01f;
        if (biggest <= 0.0f) return false;
        rebuildSpectrumPaths();
        return true;
    }

    void FeedbackGraph::snapDisplay()
    {
        if (! hasFrame) return;
        frame = target;
        shownValid = true;
        settling = false;
        rebuildSpectrumPaths();
        repaint();
    }

    void FeedbackGraph::refreshNotches()
    {
        notches = processor.getNotches();
        layoutMarkers();
        rebuildCurve();
        repaint();
    }

    void FeedbackGraph::setSelectedNotch (int notchId)
    {
        if (notchId == selectedNotch) return;
        selectedNotch = notchId;
        repaint();
    }

    const FeedbackGraph::Marker* FeedbackGraph::markerFor (int notchId) const noexcept
    {
        for (const auto& m : markers)
            if (m.id == notchId) return &m;
        return nullptr;
    }

    const dsp::Notch* FeedbackGraph::notchById (int notchId) const noexcept
    {
        for (const auto& n : notches)
            if (n.active && n.id == notchId) return &n;
        return nullptr;
    }

    //==============================================================================
    float FeedbackGraph::xForFrequency (float f) const noexcept
    {
        const auto t = std::log (juce::jlimit (minHz, maxHz, f) / minHz) / logSpan;
        return plot.getX() + t * plot.getWidth();
    }

    float FeedbackGraph::frequencyForX (float x) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / std::max (1.0f, plot.getWidth()));
        return minHz * std::exp (t * logSpan);
    }

    float FeedbackGraph::yForLevel (float dbfs) const noexcept
    {
        return juce::jmap (juce::jlimit (spectrumFloorDb, 0.0f, dbfs), spectrumFloorDb, 0.0f, plot.getBottom(), plot.getY());
    }

    float FeedbackGraph::yForCut (float cutDb) const noexcept
    {
        return juce::jmap (juce::jlimit (0.0f, cutRangeDb, cutDb), 0.0f, cutRangeDb, baselineY, plot.getBottom() - 6.0f);
    }

    void FeedbackGraph::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (4.0f);
        area.removeFromTop (18.0f);
        area.removeFromRight (30.0f);
        area.removeFromLeft (2.0f);
        area.removeFromBottom (2.0f);
        plot = area;
        baselineY = plot.getY() + juce::jlimit (70.0f, 210.0f, plot.getHeight() * 0.45f);
        background = {};
        layoutMarkers();
        rebuildCurve();
        rebuildSpectrumPaths();
    }

    void FeedbackGraph::layoutMarkers()
    {
        markers.clear();
        if (plot.isEmpty()) return;

        std::array<int, dsp::maxNotches> order {};
        int count = 0;
        for (int i = 0; i < dsp::maxNotches; ++i)
            if (notches[(size_t) i].active) order[(size_t) count++] = i;
        std::sort (order.begin(), order.begin() + count, [this] (int a, int b) { return notches[(size_t) a].freq < notches[(size_t) b].freq; });

        const auto laneTop = plot.getY() + 4.0f;
        const auto laneBottom = baselineY - 2.0f * pinRadius - 8.0f;
        const auto rows = juce::jlimit (0, 4, (int) std::floor ((laneBottom - laneTop + 4.0f) / (chipHeight + 4.0f)));
        std::array<float, 4> rowRight;
        rowRight.fill (-1.0e9f);

        for (int k = 0; k < count; ++k)
        {
            const auto& n = notches[(size_t) order[(size_t) k]];
            Marker m;
            m.id = n.id;
            m.number = k + 1;
            const auto cx = xForFrequency (n.freq);
            m.handle = { cx, yForCut (n.cutDb) };

            const auto left = plot.getX() + 2.0f, right = plot.getRight() - 2.0f;
            const auto wanted = juce::jlimit (left, std::max (left, right - chipWidth), cx - 0.5f * chipWidth);
            auto placed = false;
            for (int r = 0; r < rows && ! placed; ++r)
            {
                auto x = std::max (wanted, rowRight[(size_t) r] + 4.0f);
                // Allow a chip to slide right of its marker, but not so far it points elsewhere.
                if (x + chipWidth <= right && x - wanted <= 0.6f * chipWidth)
                {
                    m.chip = { x, laneTop + (float) r * (chipHeight + 4.0f), chipWidth, chipHeight };
                    rowRight[(size_t) r] = x + chipWidth;
                    placed = true;
                }
            }

            if (placed)
            {
                auto buttons = m.chip.withTrimmedLeft (chipWidth - 22.0f).reduced (2.0f, 3.0f);
                m.lockButton = buttons.removeFromTop (buttons.getHeight() * 0.5f).reduced (0.0f, 1.0f);
                m.clearButton = buttons.reduced (0.0f, 1.0f);
            }
            else
            {
                m.compact = true;
                m.chip = juce::Rectangle<float> (2.0f * pinRadius, 2.0f * pinRadius).withCentre ({ cx, baselineY - pinRadius - 4.0f });
            }
            markers.push_back (m);
        }
    }

    //==============================================================================
    void FeedbackGraph::smoothDisplay (const float* in, float* out, float binHz) noexcept
    {
        constexpr int n = dsp::DisplayFrame::numPoints, maxRadius = 12;
        const auto spacingOct = std::log2 (dsp::DisplayFrame::maxHz / dsp::DisplayFrame::minHz) / (float) (n - 1);
        for (int p = 0; p < n; ++p)
        {
            const auto f = dsp::DisplayFrame::pointFrequency (p);
            const auto sigmaOct = binHz > 0.0f ? std::min (1.0f / 6.0f, 0.5f * std::log2 (1.0f + binHz / f)) : 0.0f;
            const auto sigma = sigmaOct / spacingOct;
            if (sigma < 0.33f)
            {
                out[p] = in[p];
                continue;
            }
            const auto radius = std::min (maxRadius, (int) std::ceil (3.0f * sigma));
            float sum = 0.0f, weights = 0.0f;
            for (int j = -radius; j <= radius; ++j)
            {
                const auto w = std::exp (-0.5f * (float) (j * j) / (sigma * sigma));
                sum += w * in[juce::jlimit (0, n - 1, p + j)];
                weights += w;
            }
            out[p] = sum / weights;
        }
    }

    void FeedbackGraph::addSmoothCurve (juce::Path& path, const float* xs, const float* ys, int n, bool startNewSubPath)
    {
        if (n <= 0) return;
        if (startNewSubPath) path.startNewSubPath (xs[0], ys[0]);
        else                 path.lineTo (xs[0], ys[0]);
        const auto secant = [&] (int i) { const auto h = xs[i + 1] - xs[i]; return h > 1.0e-6f ? (ys[i + 1] - ys[i]) / h : 0.0f; };
        const auto tangent = [&] (int i)
        {
            if (n < 2) return 0.0f;
            if (i == 0) return secant (0);
            if (i == n - 1) return secant (n - 2);
            const auto a = secant (i - 1), b = secant (i);
            return a * b <= 0.0f ? 0.0f : 2.0f * a * b / (a + b);
        };
        auto m0 = tangent (0);
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto m1 = tangent (i + 1);
            const auto h = (xs[i + 1] - xs[i]) / 3.0f;
            path.cubicTo (xs[i] + h, ys[i] + m0 * h, xs[i + 1] - h, ys[i + 1] - m1 * h, xs[i + 1], ys[i + 1]);
            m0 = m1;
        }
    }

    void FeedbackGraph::rebuildSpectrumPaths()
    {
        inputPath.clear();
        outputPath.clear();
        if (! hasFrame || plot.isEmpty()) return;

        constexpr int n = dsp::DisplayFrame::numPoints;
        smoothDisplay (frame.input.data(), drawnInput.data(), frame.binHz);
        smoothDisplay (frame.output.data(), drawnOutput.data(), frame.binHz);
        for (int i = 0; i < n; ++i)
        {
            pointX[(size_t) i] = xForFrequency (dsp::DisplayFrame::pointFrequency (i));
            inputY[(size_t) i] = yForLevel (drawnInput[(size_t) i]);
            outputY[(size_t) i] = yForLevel (drawnOutput[(size_t) i]);
        }
        inputPath.startNewSubPath (plot.getX(), plot.getBottom());
        addSmoothCurve (inputPath, pointX.data(), inputY.data(), n, false);
        inputPath.lineTo (plot.getRight(), plot.getBottom());
        inputPath.closeSubPath();
        addSmoothCurve (outputPath, pointX.data(), outputY.data(), n, true);
    }

    void FeedbackGraph::rebuildCurve()
    {
        curveFill.clear();
        curveEdge.clear();
        if (plot.isEmpty()) return;
        const auto steps = std::max (2, (int) plot.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const auto y = yForCut (-dsp::FeedbackEngine::bankResponseDb (notches, frequencyForX (x)));
            if (i == 0) curveEdge.startNewSubPath (x, y); else curveEdge.lineTo (x, y);
        }
        curveFill = curveEdge;
        curveFill.lineTo (plot.getRight(), baselineY);
        curveFill.lineTo (plot.getX(), baselineY);
        curveFill.closeSubPath();
    }

    void FeedbackGraph::drawBackground()
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt ((float) getWidth() * scale)),
                                  std::max (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        g.fillAll (Palette::panel);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (plot, 4.0f);
        g.setFont (juce::FontOptions (11.0f));

        for (float f : { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f })
        {
            const auto x = xForFrequency (f);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (Palette::textMuted);
            const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getY() - 17.0f, 40.0f, 14.0f).toNearestInt(), juce::Justification::centred);
        }

        // Cut scale (right edge) below the 0 dB baseline.
        for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f })
        {
            const auto y = yForCut (db);
            g.setColour (db == 0.0f ? Palette::divider.brighter (0.25f) : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (Palette::textMuted);
            const auto text = db == 0.0f ? juce::String ("0") : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String ((int) db);
            g.drawText (text, juce::Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 28.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    //==============================================================================
    void FeedbackGraph::paint (juce::Graphics& g)
    {
        const auto scale = (float) juce::Component::getApproximateScaleFactorForComponent (this);
        if (! background.isValid() || std::abs (scale - backgroundScale) > 1.0e-4f)
            drawBackground();
        g.drawImage (background, getLocalBounds().toFloat());

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());
            if (hasFrame)
            {
                g.setColour (Palette::spectrum.withAlpha (0.10f));
                g.fillPath (inputPath);
                g.setColour (Palette::spectrum.withAlpha (0.28f));
                g.strokePath (inputPath, juce::PathStrokeType (1.0f));
                g.setColour (Palette::textPrimary.withAlpha (0.45f));
                g.strokePath (outputPath, juce::PathStrokeType (1.1f));
            }
            g.setColour (Palette::dynamic.withAlpha (0.24f));
            g.fillPath (curveFill);
            g.setColour (Palette::dynamic.withAlpha (0.9f));
            g.strokePath (curveEdge, juce::PathStrokeType (1.4f));
        }

        const auto dash = std::array<float, 2> { 3.0f, 3.0f };
        for (const auto& m : markers)
        {
            const auto* n = notchById (m.id);
            if (n == nullptr) continue;
            const auto colour = n->fixed ? Palette::accent : Palette::dynamic;
            const auto selected = m.id == selectedNotch, hovered = m.id == hoverNotch;

            // Line from the chip / pin down to the handle at the notch's depth.
            juce::Path line;
            line.startNewSubPath (m.handle.x, m.chip.getBottom());
            line.lineTo (m.handle);
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dash.data(), (int) dash.size());
            g.setColour (colour.withAlpha (selected || hovered ? 0.9f : 0.5f));
            g.fillPath (dashed);

            juce::Path diamond;
            const auto r = selected || hovered ? 5.5f : 4.5f;
            diamond.addQuadrilateral (m.handle.x, m.handle.y - r, m.handle.x + r, m.handle.y, m.handle.x, m.handle.y + r, m.handle.x - r, m.handle.y);
            g.setColour (colour);
            g.fillPath (diamond);

            if (m.compact)
            {
                g.setColour (colour.withAlpha (selected ? 0.95f : 0.7f));
                g.fillEllipse (m.chip);
                g.setColour (Palette::background);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (juce::String (m.number), m.chip.toNearestInt(), juce::Justification::centred);
                continue;
            }

            g.setColour (Palette::panelRaised.withAlpha (0.94f));
            g.fillRoundedRectangle (m.chip, 4.0f);
            g.setColour (colour.withAlpha (selected ? 1.0f : (n->ringing ? 0.9f : 0.55f)));
            g.drawRoundedRectangle (m.chip.reduced (0.5f), 4.0f, selected ? 1.8f : 1.0f);
            g.fillRect (m.chip.getX() + 1.0f, m.chip.getY() + 5.0f, 2.5f, m.chip.getHeight() - 10.0f);

            auto text = m.chip.withTrimmedRight (22.0f).reduced (7.0f, 3.0f).withTrimmedLeft (1.0f);
            const auto lineHeight = text.getHeight() / 3.0f;
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (Palette::textPrimary);
            g.drawText (params::formatFrequency (n->freq) + "  " + params::noteName (n->freq),
                        text.removeFromTop (lineHeight).toNearestInt(), juce::Justification::centredLeft, false);
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (colour.brighter (0.2f));
            g.drawText (cutText (n->cutDb) + "  Q " + params::formatValue (n->q, 0),
                        text.removeFromTop (lineHeight).toNearestInt(), juce::Justification::centredLeft, false);
            g.setColour (Palette::textMuted);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (n->hits) + "  " + timeText (n->detectedMs)
                            + (n->fixed ? "  FIX" : "  FLT"),
                        text.toNearestInt(), juce::Justification::centredLeft, false);

            // Lock (fixed / floating) and clear buttons.
            const auto overLock = hovering && m.lockButton.contains (hoverPoint);
            const auto overClear = hovering && m.clearButton.contains (hoverPoint);
            if (overLock || overClear)
            {
                g.setColour (Palette::divider.brighter (0.2f));
                g.fillRoundedRectangle (overLock ? m.lockButton : m.clearButton, 3.0f);
            }
            g.setColour (n->fixed ? Palette::accent : Palette::textMuted);
            const auto lockBox = m.lockButton.withSizeKeepingCentre (11.0f, 12.0f);
            if (n->fixed) g.fillPath (padlock (lockBox, true));
            else          g.strokePath (padlock (lockBox, false), juce::PathStrokeType (1.2f));
            g.setColour (overClear ? Palette::textPrimary : Palette::textMuted);
            const auto x = m.clearButton.withSizeKeepingCentre (8.0f, 8.0f);
            g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
            g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
        }

        if (hovering)
        {
            const auto text = readoutText();
            if (text.isNotEmpty())
            {
                const auto font = juce::Font (juce::FontOptions (11.5f));
                const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
                const auto chip = juce::Rectangle<float> (plot.getRight() - w - 6.0f, plot.getBottom() - 24.0f, w, 18.0f);
                g.setColour (Palette::panelRaised.withAlpha (0.92f));
                g.fillRoundedRectangle (chip, 4.0f);
                g.setFont (font);
                g.setColour (Palette::textPrimary);
                g.drawText (text, chip.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    juce::String FeedbackGraph::readoutText() const
    {
        if (const auto* n = notchById (hoverNotch))
        {
            const auto* m = markerFor (hoverNotch);
            return "#" + juce::String (m != nullptr ? m->number : 0) + "  " + params::formatValue (n->freq, 1) + " Hz  "
                 + params::noteWithCents (n->freq) + "  cut " + params::formatValue (n->cutDb, 1) + " dB (most "
                 + params::formatValue (n->neededDb, 1) + ")  Q " + params::formatValue (n->q, 1) + "  "
                 + juce::String (n->hits) + (n->hits == 1 ? " hit  " : " hits  ") + timeText (n->detectedMs)
                 + (n->fixed ? "  fixed" : "  floating") + (n->ringing ? "  ringing" : "");
        }
        if (! plot.contains (hoverPoint)) return {};
        const auto f = frequencyForX (hoverPoint.x);
        auto text = params::formatFrequency (f) + "  " + params::noteWithCents (f);
        if (hasFrame)
        {
            const auto t = std::log (f / dsp::DisplayFrame::minHz) / logSpan * (float) (dsp::DisplayFrame::numPoints - 1);
            const auto i = juce::jlimit (0, dsp::DisplayFrame::numPoints - 1, juce::roundToInt (t));
            text << "  " << params::formatValue (drawnOutput[(size_t) i], 0) << " dBFS";
        }
        return text;
    }

    //==============================================================================
    int FeedbackGraph::notchAt (juce::Point<float> position) const
    {
        for (auto it = markers.rbegin(); it != markers.rend(); ++it)
            if (it->chip.expanded (it->compact ? 2.0f : 0.0f).contains (position))
                return it->id;
        for (const auto& m : markers)
            if (m.handle.getDistanceFrom (position) <= 7.0f)
                return m.id;
        return 0;
    }

    void FeedbackGraph::mouseMove (const juce::MouseEvent& e)
    {
        hovering = true;
        hoverPoint = e.position;
        hoverNotch = notchAt (e.position);
        auto overButton = false;
        for (const auto& m : markers)
            if (! m.compact && (m.lockButton.contains (e.position) || m.clearButton.contains (e.position))) overButton = true;
        setMouseCursor (overButton ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void FeedbackGraph::mouseExit (const juce::MouseEvent&)
    {
        hovering = false;
        hoverNotch = 0;
        repaint();
    }

    void FeedbackGraph::mouseDown (const juce::MouseEvent& e)
    {
        const auto id = notchAt (e.position);
        if (id == 0) return;
        const auto* m = markerFor (id);
        const auto* n = notchById (id);
        if (m == nullptr || n == nullptr) return;

        if (e.mods.isPopupMenu() || (! m->compact && m->clearButton.contains (e.position)))
        {
            processor.clearNotch (id);
            if (selectedNotch == id) selectedNotch = 0;
            hoverNotch = 0;
        }
        else if (! m->compact && m->lockButton.contains (e.position))
            processor.setNotchFixed (id, ! n->fixed);
        else
        {
            selectedNotch = id;
            if (onNotchSelected) onNotchSelected (id);
        }
        refreshNotches();
    }
}
