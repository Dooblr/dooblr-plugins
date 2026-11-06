#include "Board.h"

namespace dooblrdistressor::ui
{
    namespace
    {
        float dbToY (float db, juce::Rectangle<float> bar, float minDb, float maxDb) noexcept
        {
            const auto t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return bar.getBottom() - t * bar.getHeight();
        }

        juce::Colour meterColour (float db) noexcept
        {
            if (db >= 0.0f) return juce::Colour (0xffe0574f);
            if (db >= -6.0f) return juce::Colour (0xffe2c25a);
            return juce::Colour (0xff5fbf8a);
        }
    }

    GrLedMeter::GrLedMeter()
    {
        setTooltip ("Gain reduction");
        setInterceptsMouseClicks (false, false);
    }

    void GrLedMeter::setGainReductionDb (float db)
    {
        db = juce::jmax (0.0f, db);
        if (std::abs (db - grDb) < 0.05f)
            return;
        grDb = db;
        repaint();
    }

    void GrLedMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto label = bounds.removeFromTop (16.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("GAIN REDUCTION", label.toNearestInt(), juce::Justification::centredLeft);
        g.setColour (Palette::dynamic);
        g.drawText (grDb < 0.05f ? "0 dB" : (juce::String (grDb, 1) + " dB"),
                    label.toNearestInt(), juce::Justification::centredRight);

        bounds.reduce (8.0f, 4.0f);
        const auto gap = 3.0f;
        const auto w = (bounds.getWidth() - gap * (float) (numLeds - 1)) / (float) numLeds;

        for (int i = 0; i < numLeds; ++i)
        {
            const auto threshold = minDb + (maxDb - minDb) * (float) i / (float) (numLeds - 1);
            const auto on = grDb >= threshold;
            auto led = juce::Rectangle<float> (w, bounds.getHeight()).withX (bounds.getX() + (w + gap) * (float) i).withY (bounds.getY());
            led = led.withSizeKeepingCentre (led.getWidth(), juce::jmin (led.getHeight(), 14.0f));
            g.setColour (on ? Palette::dynamic : Palette::grid);
            g.fillRoundedRectangle (led, 2.0f);
        }
    }

    DistLeds::DistLeds()
    {
        setTooltip ("Distortion indicators: 1 % THD (UA) and 3 % / clip");
        setInterceptsMouseClicks (false, false);
    }

    void DistLeds::setState (bool led1pct, bool led3pct)
    {
        if (led1 == led1pct && led3 == led3pct)
            return;
        led1 = led1pct;
        led3 = led3pct;
        repaint();
    }

    void DistLeds::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 4.0f);
        bounds.reduce (8.0f, 6.0f);

        auto paintLamp = [&g] (juce::Rectangle<float> area, bool on, juce::Colour colour, const juce::String& text)
        {
            auto lamp = area.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
            g.setColour (on ? colour : Palette::grid);
            g.fillEllipse (lamp);
            g.setColour (on ? Palette::textPrimary : Palette::textMuted);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (text, area.toNearestInt(), juce::Justification::centredLeft);
        };

        auto top = bounds.removeFromTop (bounds.getHeight() * 0.5f);
        paintLamp (top, led1, juce::Colour (0xffe2c25a), "1%");
        paintLamp (bounds, led3, juce::Colour (0xffe0574f), "3%");
    }

    StereoMeter::StereoMeter (juce::String captionToUse) : caption (std::move (captionToUse))
    {
        setTooltip (caption + " peak");
        setInterceptsMouseClicks (false, false);
    }

    void StereoMeter::pushPeaks (float left, float right, double nowMs)
    {
        const auto peaks = std::array<float, 2> { left, right };
        const auto dt = lastMs > 0.0 ? juce::jmax (0.0, (nowMs - lastMs) * 0.001) : 0.0;
        lastMs = nowMs;
        const auto release = (float) (26.0 * dt);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto db = peaks[(size_t) ch] > 1.0e-8f ? 20.0f * std::log10 (peaks[(size_t) ch]) : minDb;
            db = juce::jlimit (minDb, maxDb, db);
            auto& c = channels[(size_t) ch];
            c.level = db > c.level ? db : juce::jmax (minDb, c.level - release);
        }
        repaint();
    }

    void StereoMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (caption, bounds.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred);

        bounds.reduce (8.0f, 4.0f);
        const auto gap = 3.0f;
        const auto w = (bounds.getWidth() - gap) * 0.5f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (w, bounds.getHeight()).withX (bounds.getX() + (w + gap) * (float) ch).withY (bounds.getY());
            g.setColour (Palette::background);
            g.fillRoundedRectangle (bar, 2.0f);
            const auto y = dbToY (channels[(size_t) ch].level, bar, minDb, maxDb);
            auto fill = bar.withTop (y);
            g.setColour (meterColour (channels[(size_t) ch].level));
            g.fillRoundedRectangle (fill, 2.0f);
        }
    }
}
