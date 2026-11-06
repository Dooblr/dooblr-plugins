#include "WaveformView.h"
#include "Theme.h"

namespace dooblrgenerate::ui
{
    void WaveformView::setSample (std::shared_ptr<const engine::Sample> s)
    {
        sample = std::move (s);
        rebuild();
        repaint();
    }

    void WaveformView::setStartFraction (float f)
    {
        f = juce::jlimit (0.0f, 1.0f, f);
        if (! juce::approximatelyEqual (f, startFraction))
        {
            startFraction = f;
            repaint();
        }
    }

    void WaveformView::rebuild()
    {
        columns.clear();
        const auto width = juce::jmax (1, getWidth() - 2);

        if (sample == nullptr || sample->audio.getNumSamples() == 0)
            return;

        const auto& audio = sample->audio;
        const auto n = audio.getNumSamples();
        columns.resize ((size_t) width);

        for (int x = 0; x < width; ++x)
        {
            const auto a = (int) ((juce::int64) n * x / width);
            const auto b = juce::jmax (a + 1, (int) ((juce::int64) n * (x + 1) / width));
            float lo = 0.0f, hi = 0.0f;

            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const auto r = juce::FloatVectorOperations::findMinAndMax (audio.getReadPointer (ch, a), b - a);
                lo = juce::jmin (lo, r.getStart());
                hi = juce::jmax (hi, r.getEnd());
            }

            columns[(size_t) x] = { lo, hi };
        }
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (Palette::divider);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        const auto plot = bounds.reduced (1.0f, 8.0f);
        const auto mid = plot.getCentreY();
        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

        if (columns.empty())
            return;

        const auto colour = sample != nullptr && sample->isTest ? Palette::textMuted : Palette::accent;
        const auto half = plot.getHeight() * 0.5f;
        juce::Path path;

        for (size_t x = 0; x < columns.size(); ++x)
        {
            const auto px = plot.getX() + (float) x;
            const auto top = mid - juce::jlimit (-1.0f, 1.0f, columns[x].getEnd()) * half;
            const auto bottom = mid - juce::jlimit (-1.0f, 1.0f, columns[x].getStart()) * half;
            path.addRectangle (px, top, 1.0f, juce::jmax (1.0f, bottom - top));
        }

        g.setColour (colour.withAlpha (0.85f));
        g.fillPath (path);

        if (startFraction > 0.0f)
        {
            const auto x = plot.getX() + startFraction * plot.getWidth();
            g.setColour (Palette::background.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), x - plot.getX(), plot.getHeight()));
            g.setColour (Palette::dynamic);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY() + 2.0f, 2.0f, bounds.getHeight() - 4.0f));
        }
    }

    bool WaveformView::dragOut()
    {
        if (sample == nullptr || ! sample->file.existsAsFile())
            return false;

        return juce::DragAndDropContainer::performExternalDragDropOfFiles ({ sample->file.getFullPathName() }, false, this);
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragStarted && e.getDistanceFromDragStart() > 6)
        {
            dragStarted = true;
            dragOut();
        }
    }
}
