#include "SubDisplay.h"

#include "Theme.h"

#include <cmath>

namespace dooblrsub::ui
{
    void SubDisplay::setFrame (const std::array<float, dsp::SystemCheck::spectrumBins>& spec,
                               const std::array<float, dsp::numSubBands>& b,
                               float hz)
    {
        spectrum = spec;
        bands = b;
        trackedHz = hz;
        repaint();
    }

    void SubDisplay::resized()
    {
        background = {};
        lastW = lastH = lastScale = 0;
    }

    void SubDisplay::rebuildBackground()
    {
        const auto scale = juce::jmax (1, (int) std::round (juce::Desktop::getInstance().getDisplays()
                                                                .getDisplayForRect (getScreenBounds()) != nullptr
                                                            ? juce::Desktop::getInstance().getDisplays()
                                                                  .getDisplayForRect (getScreenBounds())->scale
                                                            : 1.0));
        if (background.isValid() && lastW == getWidth() && lastH == getHeight() && lastScale == scale)
            return;

        lastW = getWidth();
        lastH = getHeight();
        lastScale = scale;
        background = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth() * scale),
                                  juce::jmax (1, getHeight() * scale), true);
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale ((float) scale));

        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (Palette::grid);
        for (int i = 1; i < 4; ++i)
        {
            const auto y = bounds.getY() + bounds.getHeight() * 0.62f * (float) i / 4.0f;
            g.fillRect (bounds.getX() + 8.0f, y, bounds.getWidth() - 16.0f, 1.0f);
        }
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("20", bounds.removeFromBottom (14).removeFromLeft (28).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    void SubDisplay::paint (juce::Graphics& g)
    {
        rebuildBackground();
        if (background.isValid())
            g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / (float) juce::jmax (1, lastScale)));

        auto bounds = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
        auto meters = bounds.removeFromRight (juce::jlimit (90.0f, 140.0f, bounds.getWidth() * 0.28f));
        bounds.removeFromRight (8.0f);

        // Spectrum fill, 20-200 Hz, 0..-72 dB.
        juce::Path path;
        const auto n = (int) spectrum.size();
        auto dbToY = [&] (float mag)
        {
            const auto db = mag > 1.0e-8f ? 20.0f * std::log10 (mag) : -90.0f;
            return juce::jmap (juce::jlimit (-72.0f, 0.0f, db), -72.0f, 0.0f, bounds.getBottom(), bounds.getY());
        };
        for (int i = 0; i < n; ++i)
        {
            const auto x = bounds.getX() + bounds.getWidth() * (float) i / (float) juce::jmax (1, n - 1);
            const auto y = dbToY (spectrum[(size_t) i]);
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        auto fill = path;
        fill.lineTo (bounds.getRight(), bounds.getBottom());
        fill.lineTo (bounds.getX(), bounds.getBottom());
        fill.closeSubPath();
        g.setColour (Palette::spectrum.withAlpha (0.28f));
        g.fillPath (fill);
        g.setColour (Palette::spectrum);
        g.strokePath (path, juce::PathStrokeType (1.4f));

        if (trackedHz > 1.0f)
        {
            const auto t = std::log (trackedHz / 20.0f) / std::log (200.0f / 20.0f);
            const auto x = bounds.getX() + juce::jlimit (0.0f, 1.0f, (float) t) * bounds.getWidth();
            g.setColour (Palette::accent.withAlpha (0.85f));
            g.fillRect (x - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());
        }

        const auto slot = meters.getWidth() / (float) dsp::numSubBands;
        for (int i = 0; i < dsp::numSubBands; ++i)
        {
            auto cell = meters.removeFromLeft (slot).reduced (2.0f, 0.0f);
            auto label = cell.removeFromBottom (14.0f);
            g.setColour (Palette::grid);
            g.fillRoundedRectangle (cell, 2.0f);
            const auto db = bands[(size_t) i] > 1.0e-8f ? 20.0f * std::log10 (bands[(size_t) i]) : -72.0f;
            const auto h = juce::jmap (juce::jlimit (-72.0f, 0.0f, db), -72.0f, 0.0f, 0.0f, cell.getHeight());
            g.setColour (bandColour (i));
            g.fillRoundedRectangle (cell.removeFromBottom (h), 2.0f);
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (9.0f));
            g.drawFittedText (dsp::subBandNames[i], label.toNearestInt(), juce::Justification::centred, 1);
        }
    }
}
