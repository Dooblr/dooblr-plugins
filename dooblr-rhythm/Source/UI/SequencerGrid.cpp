#include "SequencerGrid.h"

#include "../Parameters.h"

#include <cmath>

namespace dooblrrhythm::ui
{
    namespace Palette = dooblrrhythm::ui::Palette;

    SequencerGrid::SequencerGrid (DooblrRhythmProcessor& p) : proc (p)
    {
        setComponentID ("sequencer");
        for (int i = 0; i < numInstruments; ++i)
        {
            auto& r = rows[(size_t) i];
            r.name.setText (instrumentNames[i], juce::dontSendNotification);
            r.name.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            r.name.setColour (juce::Label::textColourId, Palette::textPrimary);
            r.name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (r.name);

            auto setup = [this, i] (juce::ToggleButton& b, const char* suffix, const juce::String& tip)
            {
                const auto pid = params::instrumentParam (i, suffix);
                b.setComponentID (pid);
                b.setTooltip (tip);
                b.setButtonText (suffix[0] == 'm' ? "M" : suffix[0] == 's' ? "S" : "L");
                addAndMakeVisible (b);
            };
            setup (r.mute, "mute", "Mute");
            setup (r.solo, "solo", "Solo");
            setup (r.lock, "lock", "Pattern lock");
            r.muteA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getState(), params::instrumentParam (i, "mute"), r.mute);
            r.soloA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getState(), params::instrumentParam (i, "solo"), r.solo);
            r.lockA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getState(), params::instrumentParam (i, "lock"), r.lock);
        }
    }

    SequencerGrid::~SequencerGrid() = default;

    void SequencerGrid::setSelectedInstrument (int index)
    {
        selected = juce::jlimit (0, numInstruments - 1, index);
        if (onSelectInstrument)
            onSelectInstrument (selected);
        rebuildBackground();
        repaint();
    }

    void SequencerGrid::refresh()
    {
        lastPattern = proc.currentPattern();
        rebuildBackground();
        resized();
        repaint();
    }

    juce::Component* SequencerGrid::findControl (const juce::String& parameterId) const
    {
        for (int i = 0; i < numInstruments; ++i)
        {
            const auto& r = rows[(size_t) i];
            for (auto* c : { (juce::Component*) &r.mute, (juce::Component*) &r.solo, (juce::Component*) &r.lock })
                if (c->getComponentID() == parameterId)
                    return c;
        }
        return nullptr;
    }

    int SequencerGrid::rowAt (juce::Point<int> pos) const
    {
        if (! getLocalBounds().contains (pos))
            return -1;
        const int y = pos.y + scrollY;
        const int row = y / rowH;
        return juce::isPositiveAndBelow (row, numInstruments) ? row : -1;
    }

    int SequencerGrid::stepAt (juce::Point<int> pos) const
    {
        if (! gridArea.contains (pos))
            return -1;
        const auto p = lastPattern;
        const int steps = juce::jmax (1, p.totalSteps());
        const float w = (float) gridArea.getWidth() / (float) steps;
        const int s = (int) std::floor ((pos.x - gridArea.getX()) / juce::jmax (0.001f, w));
        return juce::jlimit (0, steps - 1, s);
    }

    juce::Rectangle<int> SequencerGrid::cellBounds (int row, int step) const
    {
        const int steps = juce::jmax (1, lastPattern.totalSteps());
        const float w = (float) gridArea.getWidth() / (float) steps;
        const int y = row * rowH - scrollY;
        return { gridArea.getX() + (int) std::floor (w * (float) step), y,
                 juce::jmax (2, (int) std::ceil (w)), rowH - 1 };
    }

    void SequencerGrid::resized()
    {
        auto b = getLocalBounds();
        labelArea = b.removeFromLeft (labelW + btnW * 3 + 8);
        gridArea = b;
        const int visible = juce::jmax (1, getHeight() / rowH);
        const int maxScroll = juce::jmax (0, numInstruments - visible) * rowH;
        scrollY = juce::jlimit (0, maxScroll, scrollY);

        for (int i = 0; i < numInstruments; ++i)
        {
            auto& r = rows[(size_t) i];
            const int y = i * rowH - scrollY;
            const bool show = y >= 0 && y + rowH <= getHeight();
            r.name.setVisible (show);
            r.mute.setVisible (show);
            r.solo.setVisible (show);
            r.lock.setVisible (show);
            if (! show)
                continue;
            auto row = juce::Rectangle<int> (0, y, labelArea.getWidth(), rowH);
            r.mute.setBounds (row.removeFromLeft (btnW).reduced (1));
            r.solo.setBounds (row.removeFromLeft (btnW).reduced (1));
            r.lock.setBounds (row.removeFromLeft (btnW).reduced (1));
            row.removeFromLeft (4);
            r.name.setBounds (row);
        }
        rebuildBackground();
    }

    void SequencerGrid::rebuildBackground()
    {
        const auto p = lastPattern.hits.empty() && lastPattern.bars == 0 ? proc.currentPattern() : lastPattern;
        lastPattern = p;
        const int w = juce::jmax (1, getWidth());
        const int h = juce::jmax (1, getHeight());
        const int steps = juce::jmax (1, p.totalSteps());
        background = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics g (background);
        g.fillAll (Palette::background);

        const int spb = juce::jmax (1, p.stepsPerBar);
        const float stepW = (float) gridArea.getWidth() / (float) steps;

        for (int i = 0; i < numInstruments; ++i)
        {
            const int y = i * rowH - scrollY;
            if (y + rowH < 0 || y > h)
                continue;
            const auto rowR = juce::Rectangle<float> ((float) gridArea.getX(), (float) y,
                                                      (float) gridArea.getWidth(), (float) rowH);
            g.setColour (i == selected ? Palette::panelRaised : Palette::panel);
            g.fillRect (rowR);

            for (int s = 0; s < steps; ++s)
            {
                const auto cell = juce::Rectangle<float> ((float) gridArea.getX() + stepW * (float) s,
                                                          (float) y, stepW, (float) rowH);
                const bool barStart = (s % spb) == 0;
                const bool beat = (s % juce::jmax (1, spb / juce::jmax (1, beatsPerBar (p.timeSignature)))) == 0;
                g.setColour (barStart ? Palette::divider.brighter (0.25f) : beat ? Palette::grid.brighter (0.15f) : Palette::grid);
                g.drawVerticalLine ((int) cell.getX(), cell.getY(), cell.getBottom());
            }
            g.setColour (Palette::divider);
            g.fillRect (0, y + rowH - 1, w, 1);
        }

        for (const auto& hit : p.hits)
        {
            const int i = (int) hit.instrument;
            const int y = i * rowH - scrollY;
            if (y + rowH < 0 || y > h)
                continue;
            auto cell = juce::Rectangle<float> ((float) gridArea.getX() + stepW * (float) hit.step + 1.0f,
                                                (float) y + 2.0f, juce::jmax (2.0f, stepW - 2.0f), (float) rowH - 4.0f);
            const auto colour = bandColour (i).withAlpha (0.35f + 0.65f * hit.velocity);
            g.setColour (colour);
            g.fillRoundedRectangle (cell, 2.0f);
            g.setColour (Palette::textPrimary.withAlpha (0.9f));
            g.setFont (juce::FontOptions (juce::jmin (11.0f, cell.getHeight() - 1.0f)));
            const auto glyph = juce::String::charToString (
                (juce::juce_wchar) articulationGlyph[juce::jlimit (0, (int) Articulation::count - 1, (int) hit.articulation)]);
            if (cell.getWidth() >= 8.0f)
                g.drawText (glyph, cell.toNearestInt(), juce::Justification::centred, false);
        }

        bgW = w;
        bgH = h;
        bgSteps = steps;
        bgSeed = (int) p.seed;
    }

    void SequencerGrid::paint (juce::Graphics& g)
    {
        if (background.isNull() || background.getWidth() != getWidth() || background.getHeight() != getHeight())
            rebuildBackground();
        g.drawImageAt (background, 0, 0);

        const auto p = lastPattern;
        const int steps = juce::jmax (1, p.totalSteps());
        if (playhead >= 0 && steps > 0)
        {
            const float stepW = (float) gridArea.getWidth() / (float) steps;
            const float x = (float) gridArea.getX() + stepW * (float) (playhead % steps);
            g.setColour (Palette::textPrimary.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (x, 0.0f, 2.0f, (float) getHeight()));
        }
    }

    void SequencerGrid::mouseDown (const juce::MouseEvent& e)
    {
        const int row = rowAt (e.getPosition());
        if (row >= 0 && row != selected)
            setSelectedInstrument (row);

        const int step = stepAt (e.getPosition());
        if (row >= 0 && step < 0 && e.mods.isLeftButtonDown())
        {
            midiDragRow = row;
            return;
        }
        if (row < 0 || step < 0)
            return;
        dragInst = row;
        dragStep = step;
        proc.toggleHit (row, step, 0.85f);
        lastPattern = proc.currentPattern();
        rebuildBackground();
        repaint();
    }

    void SequencerGrid::mouseDrag (const juce::MouseEvent& e)
    {
        if (midiDragRow >= 0 && e.getDistanceFromDragStart() > 8 && onDragInstrumentMidi)
        {
            const int row = midiDragRow;
            midiDragRow = -1;
            onDragInstrumentMidi (row);
            return;
        }
        if (dragInst < 0 || dragStep < 0)
            return;
        const auto cell = cellBounds (dragInst, dragStep);
        if (cell.getHeight() <= 0)
            return;
        const float vel = juce::jlimit (0.05f, 1.0f, 1.0f - (float) (e.y - cell.getY()) / (float) cell.getHeight());
        proc.setHitVelocity (dragInst, dragStep, vel);
        lastPattern = proc.currentPattern();
        rebuildBackground();
        repaint();
    }

    void SequencerGrid::mouseUp (const juce::MouseEvent&)
    {
        dragInst = dragStep = -1;
        midiDragRow = -1;
    }

    void SequencerGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        const int visible = juce::jmax (1, getHeight() / rowH);
        const int maxScroll = juce::jmax (0, numInstruments - visible) * rowH;
        scrollY = juce::jlimit (0, maxScroll, scrollY - (int) std::round (wheel.deltaY * 48.0f));
        resized();
        repaint();
    }
}
