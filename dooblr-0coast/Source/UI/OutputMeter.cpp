#include "OutputMeter.h"

#include "Theme.h"

#include <cmath>

namespace dooblr0coast::ui
{
    namespace
    {
        const juce::Colour meterGreen  { 0xff5fbf8a };
        const juce::Colour meterYellow { 0xffe2c25a };
        const juce::Colour meterRed    { 0xffe0574f };

        float toDb (float gain) noexcept
        {
            return gain > 1.0e-5f ? 20.0f * std::log10 (gain) : -100.0f;
        }
    }

    OutputMeter::Zone OutputMeter::zoneFor (float db) noexcept
    {
        return db >= redDb ? Zone::red : db >= yellowDb ? Zone::yellow : Zone::green;
    }

    juce::Colour OutputMeter::zoneColour (Zone zone) noexcept
    {
        return zone == Zone::red ? meterRed : zone == Zone::yellow ? meterYellow : meterGreen;
    }

    juce::Colour OutputMeter::colourFor (float db) noexcept
    {
        // Pure zone colours at and above each threshold; a short blend into it from below.
        if (db >= redDb)
            return meterRed;
        if (db > redDb - blendDb)
            return meterYellow.interpolatedWith (meterRed, (db - (redDb - blendDb)) / blendDb);
        if (db >= yellowDb)
            return meterYellow;
        if (db > yellowDb - blendDb)
            return meterGreen.interpolatedWith (meterYellow, (db - (yellowDb - blendDb)) / blendDb);
        return meterGreen;
    }

    OutputMeter::OutputMeter (dsp::OutputPeakTap* tapToUse) : tap (tapToUse)
    {
        setComponentID ("outputMeter");
        setTooltip ("Output peak, L / R (after the Output gain). Red at 0 dBFS; click to clear the clip lights");
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { tick(); });
    }

    void OutputMeter::tick()
    {
        if (tap == nullptr || ! isShowing())
            return;

        const auto [left, right] = tap->take();
        pushPeaks (left, right, juce::Time::getMillisecondCounterHiRes());
    }

    void OutputMeter::pushPeaks (float left, float right, double nowMs)
    {
        const auto dt = lastPushMs < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, (nowMs - lastPushMs) * 0.001);
        lastPushMs = nowMs;
        const float peaks[2] { left, right };

        for (size_t c = 0; c < 2; ++c)
        {
            auto& ch = channels[c];
            const auto peak = std::abs (peaks[c]);
            const auto db = juce::jlimit (-100.0f, maxDb + 12.0f, toDb (peak));

            // Instant attack, linear-in-dB release.
            ch.level = juce::jmax (minDb, juce::jmax (db, ch.level - (float) (releaseDbPerSecond * dt)));

            if (db >= ch.hold)
            {
                ch.hold = db;
                ch.holdSince = nowMs;
            }
            else if (nowMs - ch.holdSince > holdMs)
                ch.hold = juce::jmax (ch.level, ch.hold - (float) (holdDecayDbPerSecond * dt));

            if (peak >= 1.0f)
                ch.clipped = true; // latches until clicked
        }

        auto hash = 17;
        for (const auto& ch : channels)
            hash = hash * 31 + juce::roundToInt (ch.level * 4.0f) * 7 + juce::roundToInt (ch.hold * 4.0f) * 13 + (ch.clipped ? 1 : 0);

        if (hash != lastPictureHash)
        {
            lastPictureHash = hash;
            repaint();
        }
    }

    void OutputMeter::resetClip()
    {
        for (auto& ch : channels)
            ch.clipped = false;

        lastPictureHash = 0;
        repaint();
    }

    void OutputMeter::mouseDown (const juce::MouseEvent&)
    {
        resetClip();
    }

    float OutputMeter::dbToY (float db, juce::Rectangle<float> bar) const noexcept
    {
        const auto norm = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        return bar.getBottom() - bar.getHeight() * norm;
    }

    void OutputMeter::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (area, 3.0f);

        auto inner = area.reduced (3.0f, 3.0f);
        const auto clipArea = inner.removeFromTop (4.0f);
        inner.removeFromTop (2.0f);
        constexpr float gap = 2.0f;
        const auto barWidth = (inner.getWidth() - gap) * 0.5f;

        for (int c = 0; c < 2; ++c)
        {
            const auto& ch = channels[(size_t) c];
            const auto x = inner.getX() + (float) c * (barWidth + gap);
            const juce::Rectangle<float> bar (x, inner.getY(), barWidth, inner.getHeight());

            g.setColour (Palette::grid);
            g.fillRect (bar);

            // The lit part, one pixel row at a time in that row's colour (a smooth vertical blend
            // without a ColourGradient, which would allocate).
            const auto top = dbToY (ch.level, bar);

            for (auto y = bar.getBottom(); y > top; y -= 1.0f)
            {
                const auto rowTop = juce::jmax (top, y - 1.0f);
                const auto db = minDb + (maxDb - minDb) * (bar.getBottom() - (rowTop + y) * 0.5f) / bar.getHeight();
                g.setColour (colourFor (db));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getX(), rowTop, bar.getRight(), y));
            }

            // 0 dBFS tick and the peak hold.
            g.setColour (Palette::textMuted.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), dbToY (0.0f, bar) - 0.5f, bar.getWidth(), 1.0f));

            if (ch.hold > minDb + 0.5f)
            {
                g.setColour (colourFor (ch.hold).brighter (0.2f));
                g.fillRect (juce::Rectangle<float> (bar.getX(), dbToY (ch.hold, bar) - 0.75f, bar.getWidth(), 1.5f));
            }

            const juce::Rectangle<float> light (x, clipArea.getY(), barWidth, clipArea.getHeight());
            g.setColour (ch.clipped ? meterRed : Palette::grid);
            g.fillRoundedRectangle (light, 1.0f);
        }
    }
}
