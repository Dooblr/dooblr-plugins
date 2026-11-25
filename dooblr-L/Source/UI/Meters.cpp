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

    GrHistory::GrHistory()
    {
        setComponentID ("grHistory");
        setOpaque (true);
    }

    void GrHistory::resized()
    {
        grid = {};
        rebuildGrid();
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
        g.setColour (Palette::grid);
        for (int db : { 0, 6, 12, 18, 24 })
        {
            const auto y = dbToY ((float) db, bounds.reduced (0, 14.0f), 0.0f, GrMeterModel::rangeDb);
            g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
        }
    }

    void GrHistory::push (float grDb, double nowMs)
    {
        const auto dt = lastMs < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, (nowMs - lastMs) * 0.001);
        lastMs = nowMs;
        model.tick (grDb, dt, nowMs);
        const auto pic = juce::roundToInt (model.getDisplayedDb() * 10)
                         ^ (model.getHistoryFilled() << 4)
                         ^ juce::roundToInt (model.historyNewest() * 8);
        if (pic != lastPicture)
        {
            lastPicture = pic;
            repaint();
        }
    }

    void GrHistory::paint (juce::Graphics& g)
    {
        if (grid.isValid())
            g.drawImageTransformed (grid, juce::AffineTransform::scale (
                1.0f / juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor())));
        else
        {
            g.fillAll (Palette::background);
        }

        auto bounds = getLocalBounds().toFloat().reduced (0.0f, 14.0f);
        if (bounds.getWidth() < 4.0f || model.getHistoryFilled() < 2)
            return;

        juce::Path path;
        const auto n = model.getHistoryFilled();
        for (int i = 0; i < n; ++i)
        {
            const auto x = bounds.getX() + bounds.getWidth() * (float) i / (float) (GrMeterModel::historyLength - 1);
            const auto y = dbToY (model.historyOldest (i), bounds, 0.0f, GrMeterModel::rangeDb);
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        g.setColour (Palette::dynamic);
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto peakY = dbToY (model.getPeakHoldDb(), bounds, 0.0f, GrMeterModel::rangeDb);
        g.setColour (Palette::frozen);
        g.fillRect (juce::Rectangle<float> (6.0f, 2.0f).withCentre ({ bounds.getRight() - 8.0f, peakY }));
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
