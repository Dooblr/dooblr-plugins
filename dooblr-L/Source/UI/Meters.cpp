#include "Meters.h"

#include "../Parameters.h"

#include <cmath>

namespace dooblrl::ui
{
    namespace
    {
        const juce::Colour meterGreen  { 0xff5fbf8a };
        const juce::Colour meterYellow { 0xffe2c25a };
        const juce::Colour meterRed    { 0xffe0574f };

        float toDb (float g) noexcept
        {
            return g > 1.0e-5f ? 20.0f * std::log10 (g) : -100.0f;
        }

        juce::Colour meterColour (float db) noexcept
        {
            if (db >= 0.0f) return meterRed;
            if (db >= -6.0f) return meterYellow;
            return meterGreen;
        }

        float dbToY (float db, juce::Rectangle<float> bar, float minDb, float maxDb) noexcept
        {
            const auto t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return bar.getBottom() - t * bar.getHeight();
        }
    }

    void GrMeterModel::reset() noexcept
    {
        displayedDb = peakHoldDb = 0;
        peakHoldUntilMs = sampleAccum = 0;
        historyWrite = historyFilled = 0;
        history.fill (0.0f);
    }

    void GrMeterModel::tick (float incomingDb, double dtSeconds, double nowMs) noexcept
    {
        if (! std::isfinite (incomingDb)) incomingDb = 0.0f;
        incomingDb = std::clamp (incomingDb, 0.0f, rangeDb);
        dtSeconds = std::max (0.0, dtSeconds);

        if (incomingDb >= displayedDb)
            displayedDb = incomingDb;
        else
            displayedDb = std::max (incomingDb, displayedDb - releaseDbPerSecond * (float) dtSeconds);

        if (displayedDb >= peakHoldDb - 1.0e-6f)
        {
            peakHoldDb = displayedDb;
            peakHoldUntilMs = nowMs + holdMs;
        }
        else if (nowMs >= peakHoldUntilMs)
        {
            peakHoldDb = std::max (displayedDb, peakHoldDb - holdDecayDbPerSecond * (float) dtSeconds);
        }

        sampleAccum += dtSeconds;
        const auto period = 1.0 / historyHz;
        while (sampleAccum >= period)
        {
            pushHistory (displayedDb);
            sampleAccum -= period;
        }
    }

    void GrMeterModel::pushHistory (float db) noexcept
    {
        history[(size_t) historyWrite] = db;
        historyWrite = (historyWrite + 1) % historyLength;
        historyFilled = std::min (historyFilled + 1, historyLength);
    }

    float GrMeterModel::historyOldest (int index) const noexcept
    {
        if (index < 0 || index >= historyFilled)
            return 0.0f;
        const auto idx = (historyWrite - historyFilled + index + historyLength) % historyLength;
        return history[(size_t) idx];
    }

    float GrMeterModel::historyNewest() const noexcept
    {
        return historyFilled > 0 ? historyOldest (historyFilled - 1) : 0.0f;
    }

    LevelMeter::LevelMeter (juce::String captionToUse) : caption (std::move (captionToUse))
    {
        setInterceptsMouseClicks (false, false);
        setComponentID ("meter_" + caption);
    }

    void LevelMeter::push (float peakL, float peakR, float truePeakL, float truePeakR, double nowMs)
    {
        const auto dt = lastMs < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, (nowMs - lastMs) * 0.001);
        lastMs = nowMs;
        const float p[2] { peakL, peakR }, t[2] { truePeakL, truePeakR };
        constexpr float rel = 26.0f;
        for (int i = 0; i < 2; ++i)
        {
            const auto pdb = juce::jlimit (minDb, maxDb + 12.0f, toDb (p[i]));
            const auto tdb = juce::jlimit (minDb, maxDb + 12.0f, toDb (t[i]));
            ch[(size_t) i].peak = juce::jmax (minDb, juce::jmax (pdb, ch[(size_t) i].peak - rel * (float) dt));
            ch[(size_t) i].tp   = juce::jmax (minDb, juce::jmax (tdb, ch[(size_t) i].tp   - rel * (float) dt));
        }
        const auto pic = juce::roundToInt (ch[0].peak * 4) ^ juce::roundToInt (ch[1].tp * 8);
        if (pic != lastPicture)
        {
            lastPicture = pic;
            repaint();
        }
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 4.0f);
        auto label = bounds.removeFromTop (16.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (caption, label.toNearestInt(), juce::Justification::centred);

        bounds.reduce (6.0f, 4.0f);
        const auto colW = bounds.getWidth() * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            auto col = bounds.removeFromLeft (colW).reduced (2.0f, 0.0f);
            auto tpBar = col.removeFromRight (col.getWidth() * 0.38f);
            col.removeFromRight (2.0f);
            auto peakBar = col;

            g.setColour (Palette::grid);
            g.fillRoundedRectangle (peakBar, 2.0f);
            g.fillRoundedRectangle (tpBar, 2.0f);

            const auto pTop = dbToY (ch[(size_t) i].peak, peakBar, minDb, maxDb);
            auto pFill = peakBar.withY (pTop).withBottom (peakBar.getBottom());
            g.setColour (meterColour (ch[(size_t) i].peak));
            g.fillRect (pFill);

            const auto tTop = dbToY (ch[(size_t) i].tp, tpBar, minDb, maxDb);
            g.setColour (Palette::frozen.withAlpha (0.9f));
            g.fillRect (tpBar.withY (tTop).withBottom (tpBar.getBottom()));
        }
    }

    GrHistory::GrHistory (juce::AudioProcessorValueTreeState& state)
    {
        setComponentID ("grHistory");
        setOpaque (true);

        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (params::id::threshold)))
        {
            thresholdParam = ranged;
            shownDb = ranged->convertFrom0to1 (ranged->getValue());
            thresholdAttachment = std::make_unique<juce::ParameterAttachment> (
                *ranged,
                [this] (float v)
                {
                    shownDb = v;
                    repaint();
                },
                nullptr);
        }
    }

    juce::Rectangle<float> GrHistory::plotBounds() const noexcept
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 8.0f);
        r.removeFromRight (scaleWidth);
        return r;
    }

    float GrHistory::levelDbToY (float db) const noexcept
    {
        return dbToY (db, plotBounds(), thresholdMinDb, thresholdMaxDb);
    }

    float GrHistory::yToThresholdDb (float y) const noexcept
    {
        const auto plot = plotBounds();
        const auto t = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - y) / juce::jmax (1.0f, plot.getHeight()));
        return thresholdMinDb + t * (thresholdMaxDb - thresholdMinDb);
    }

    float GrHistory::toDb (float linear) noexcept
    {
        return linear > 1.0e-6f ? 20.0f * std::log10 (linear) : thresholdMinDb;
    }

    GrHistory::Column GrHistory::getColumnFromOldest (int index) const noexcept
    {
        if (index < 0 || index >= filled)
            return {};
        const auto idx = (write - filled + index + maxColumns) % maxColumns;
        return ring[(size_t) idx];
    }

    GrHistory::Column GrHistory::getColumnFromNewest (int index) const noexcept
    {
        if (index < 0 || index >= filled)
            return {};
        const auto idx = (write - 1 - index + maxColumns) % maxColumns;
        return ring[(size_t) idx];
    }

    void GrHistory::clear() noexcept
    {
        write = filled = 0;
        ring.fill ({});
        model.reset();
        waveform = {};
        rebuildWaveform();
        repaint();
    }

    bool GrHistory::overThresholdLine (juce::Point<float> p) const noexcept
    {
        return plotBounds().expanded (0.0f, hitSlopPx).contains (p)
            && std::abs (p.y - getThresholdLineY()) < hitSlopPx;
    }

    void GrHistory::setHover (bool next)
    {
        if (next == hover)
            return;
        hover = next;
        setMouseCursor (hover || dragging ? juce::MouseCursor::UpDownResizeCursor
                                          : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void GrHistory::applyDragY (float y)
    {
        if (thresholdAttachment == nullptr || thresholdParam == nullptr)
            return;
        const auto snapped = thresholdParam->getNormalisableRange().snapToLegalValue (yToThresholdDb (y));
        thresholdAttachment->setValueAsPartOfGesture (snapped);
    }

    void GrHistory::resized()
    {
        grid = {};
        waveform = {};
        rebuildGrid();
        rebuildWaveform();
    }

    void GrHistory::rebuildGrid()
    {
        if (getWidth() <= 2 || getHeight() <= 2)
            return;
        const auto scale = (int) juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor());
        grid = juce::Image (juce::Image::ARGB, getWidth() * scale, getHeight() * scale, true);
        juce::Graphics g (grid);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::background);
        g.fillRect (bounds);

        const auto plot = plotBounds();
        g.setColour (Palette::grid);
        const int ticks[] { 0, -6, -12, -18, -24, -30, -36, -40 };
        g.setFont (juce::FontOptions (10.0f));
        for (int db : ticks)
        {
            const auto y = levelDbToY ((float) db);
            g.setColour (Palette::grid);
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (Palette::textMuted);
            g.drawText (juce::String (db),
                        juce::Rectangle<float> (plot.getRight() + 2.0f, y - 7.0f, scaleWidth - 4.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    void GrHistory::drawColumn (juce::Graphics& g, juce::Rectangle<float> plot, float x, float width, const Column& c) const
    {
        const auto bottom = plot.getBottom();
        const auto yIn = juce::jlimit (plot.getY(), bottom, dbToY (c.inPeakDb, plot, thresholdMinDb, thresholdMaxDb));
        const auto yOut = juce::jlimit (plot.getY(), bottom, dbToY (c.outPeakDb, plot, thresholdMinDb, thresholdMaxDb));
        const auto range = thresholdMaxDb - thresholdMinDb;
        const auto grH = juce::jlimit (0.0f, plot.getHeight(), c.grDb / range * plot.getHeight());

        if (bottom > yIn)
        {
            g.setColour (Palette::spectrum.withAlpha (0.38f));
            g.fillRect (x, yIn, width, bottom - yIn);
        }

        if (yOut > yIn + 0.4f)
        {
            g.setColour (Palette::dynamic.withAlpha (0.55f));
            g.fillRect (x, yIn, width, yOut - yIn);
        }

        if (bottom > yOut)
        {
            g.setColour (Palette::accent.withAlpha (0.78f));
            g.fillRect (x, yOut, width, bottom - yOut);
        }

        g.setColour (Palette::accent);
        g.fillRect (x, yOut - 0.8f, width, 1.6f);

        if (grH > 0.35f)
        {
            g.setColour (Palette::dynamic.withAlpha (0.42f));
            g.fillRect (x, plot.getY(), width, grH);
        }
    }

    void GrHistory::rebuildWaveform()
    {
        const auto plot = plotBounds().toNearestInt();
        if (plot.getWidth() <= 2 || plot.getHeight() <= 2)
        {
            waveform = {};
            return;
        }

        const auto scale = (int) juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor());
        waveform = juce::Image (juce::Image::ARGB, plot.getWidth() * scale, plot.getHeight() * scale, true);
        juce::Graphics g (waveform);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        g.setColour (juce::Colours::transparentBlack);
        g.fillAll();

        const auto local = juce::Rectangle<float> (0.0f, 0.0f, (float) plot.getWidth(), (float) plot.getHeight());
        const auto visible = juce::jmin (filled, plot.getWidth());
        const auto x0 = plot.getWidth() - visible;
        for (int i = 0; i < visible; ++i)
        {
            const auto col = getColumnFromOldest (filled - visible + i);
            drawColumn (g, local, (float) (x0 + i), 1.0f, col);
        }
    }

    void GrHistory::pushColumn (const Column& c)
    {
        ring[(size_t) write] = c;
        write = (write + 1) % maxColumns;
        filled = juce::jmin (filled + 1, maxColumns);

        const auto plot = plotBounds().toNearestInt();
        if (! waveform.isValid() || plot.getWidth() <= 2)
        {
            rebuildWaveform();
            repaint();
            return;
        }

        const auto scale = (int) juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor());
        if (waveform.getWidth() != plot.getWidth() * scale || waveform.getHeight() != plot.getHeight() * scale)
        {
            rebuildWaveform();
            repaint();
            return;
        }

        if (waveform.getWidth() > scale)
            waveform.moveImageSection (0, 0, scale, 0, waveform.getWidth() - scale, waveform.getHeight());

        waveform.clear (juce::Rectangle<int> ((plot.getWidth() - 1) * scale, 0, scale, plot.getHeight() * scale));
        juce::Graphics g (waveform);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        const auto local = juce::Rectangle<float> (0.0f, 0.0f, (float) plot.getWidth(), (float) plot.getHeight());
        drawColumn (g, local, (float) (plot.getWidth() - 1), 1.0f, c);
        repaint();
    }

    void GrHistory::ingest (const dsp::HistoryFrame* frames, int numFrames)
    {
        if (frames == nullptr || numFrames <= 0)
            return;
        for (int i = 0; i < numFrames; ++i)
        {
            Column c;
            c.inPeakDb = juce::jlimit (thresholdMinDb, 6.0f, toDb (frames[i].inPeak));
            c.outPeakDb = juce::jlimit (thresholdMinDb, 6.0f, toDb (frames[i].outPeak));
            c.grDb = juce::jmax (0.0f, frames[i].grDb);
            ring[(size_t) write] = c;
            write = (write + 1) % maxColumns;
            filled = juce::jmin (filled + 1, maxColumns);
        }
        rebuildWaveform();
        repaint();
    }

    void GrHistory::paint (juce::Graphics& g)
    {
        const auto inv = 1.0f / juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor());
        if (grid.isValid())
            g.drawImageTransformed (grid, juce::AffineTransform::scale (inv));
        else
            g.fillAll (Palette::background);

        const auto plot = plotBounds();
        if (waveform.isValid() && plot.getWidth() >= 4.0f)
            g.drawImage (waveform, plot);

        if (plot.getWidth() >= 4.0f)
        {
            const auto active = hover || dragging;
            const auto y = getThresholdLineY();
            g.setColour (Palette::frozen.withAlpha (active ? 1.0f : 0.85f));
            g.fillRect (plot.getX(), y - (active ? 1.0f : 0.75f), plot.getWidth(), active ? 2.0f : 1.5f);
        }
    }

    void GrHistory::mouseMove (const juce::MouseEvent& e)
    {
        if (! dragging)
            setHover (overThresholdLine (e.position));
    }

    void GrHistory::mouseDown (const juce::MouseEvent& e)
    {
        mouseMove (e);

        if (e.getNumberOfClicks() >= 2 || ! hover || thresholdAttachment == nullptr)
            return;

        dragging = true;
        downMouseY = e.position.y;
        downLineY = getThresholdLineY();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        thresholdAttachment->beginGesture();
        repaint();
    }

    void GrHistory::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        const auto fine = e.mods.isShiftDown() || e.mods.isCommandDown();
        const auto y = downLineY + (e.position.y - downMouseY) * (fine ? fineDragScale : 1.0f);
        applyDragY (y);
    }

    void GrHistory::mouseUp (const juce::MouseEvent& e)
    {
        if (dragging && thresholdAttachment != nullptr)
        {
            dragging = false;
            thresholdAttachment->endGesture();
        }
        dragging = false;
        mouseMove (e);
        if (! hover)
            setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void GrHistory::mouseExit (const juce::MouseEvent&)
    {
        if (! dragging)
            setHover (false);
    }

    void GrHistory::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (thresholdAttachment == nullptr || thresholdParam == nullptr)
            return;
        if (! overThresholdLine (e.position) && ! hover)
            return;

        const auto def = thresholdParam->convertFrom0to1 (thresholdParam->getDefaultValue());
        thresholdAttachment->setValueAsCompleteGesture (def);
        shownDb = def;
        repaint();
    }

    LoudnessPanel::LoudnessPanel()
    {
        setComponentID ("loudness");
        reset.setComponentID ("meterReset");
        reset.setTooltip ("Reset integrated loudness, LRA and peak holds");
        reset.onClick = [this] { if (onReset) onReset(); };
        addAndMakeVisible (reset);
    }

    void LoudnessPanel::setReadings (float m, float s, float i, float lraToUse, bool has, float targetLufs)
    {
        mLufs = m; sLufs = s; iLufs = i; lra = lraToUse; hasI = has; target = targetLufs;
        const auto pic = juce::roundToInt (m * 10) ^ juce::roundToInt (s * 6) ^ juce::roundToInt (i * 4)
                         ^ juce::roundToInt (lra * 8);
        if (pic != lastPicture)
        {
            lastPicture = pic;
            repaint();
        }
    }

    void LoudnessPanel::resized()
    {
        auto r = getLocalBounds().reduced (8, 6);
        reset.setBounds (r.removeFromBottom (22).removeFromLeft (juce::jmin (72, r.getWidth())));
    }

    void LoudnessPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto inner = getLocalBounds().reduced (8, 6);
        inner.removeFromBottom (26);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (Palette::textMuted);
        g.drawText ("LOUDNESS", inner.removeFromTop (16), juce::Justification::centredLeft);

        auto row = [&] (const juce::String& name, float value, bool valid)
        {
            auto r = inner.removeFromTop (juce::jmax (16, inner.getHeight() / 4));
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (name, r.removeFromLeft (28), juce::Justification::centredLeft);
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            const auto text = valid ? (params::formatValue ((double) value, 1) + " LUFS") : "--";
            g.drawText (text, r, juce::Justification::centredRight);
        };

        row ("M", mLufs, mLufs > -69.0f);
        row ("S", sLufs, sLufs > -69.0f);
        row ("I", iLufs, hasI);
        {
            auto r = inner.removeFromTop (juce::jmax (16, inner.getHeight()));
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("LRA", r.removeFromLeft (28), juce::Justification::centredLeft);
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (params::formatValue ((double) lra, 1) + " LU", r, juce::Justification::centredRight);
        }

        if (std::isfinite (target) && target < 0.0f)
        {
            g.setColour (Palette::accent.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (params::formatValue ((double) target, 0) + " tgt",
                        getLocalBounds().reduced (8, 6).removeFromBottom (44).removeFromRight (64),
                        juce::Justification::centredRight);
        }
    }
}
