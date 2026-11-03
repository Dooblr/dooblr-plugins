#include "Displays.h"

#include <cmath>
#include <complex>

#include "../DSP/Primitives.h"
#include "Theme.h"

namespace dooblrbass::ui
{
    juce::Colour pageColour (int page)
    {
        switch (page)
        {
            case 0: return Palette::accent;
            case 1: return Palette::dynamic;
            case 2: return Palette::master;
            default: return Palette::spectrum;
        }
    }

    namespace
    {
        constexpr int plotMargin = 10;

        bool sameEnv (const dsp::EnvSettings& a, const dsp::EnvSettings& b)
        {
            return juce::exactlyEqual (a.attack, b.attack) && juce::exactlyEqual (a.decay, b.decay)
                && juce::exactlyEqual (a.sustain, b.sustain) && juce::exactlyEqual (a.release, b.release);
        }

        juce::String formatHz (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                                 : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::String formatSeconds (float s)
        {
            return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s";
        }

        dsp::WaveWeights weightsForOsc (const dsp::Settings& s, int which)
        {
            if (which == 0) return s.osc1Wave == dsp::Wave::square ? dsp::WaveWeights::forSquare() : dsp::WaveWeights::forSaw();
            if (which == 1) return s.osc2Wave == dsp::Wave::square ? dsp::WaveWeights::forSquare() : dsp::WaveWeights::forSaw();
            switch (s.osc3Wave)
            {
                case dsp::Osc3Wave::triangle: return dsp::WaveWeights::forTriangle();
                case dsp::Osc3Wave::square:   return dsp::WaveWeights::forSquare();
                case dsp::Osc3Wave::saw:      return dsp::WaveWeights::forSaw();
            }
            return dsp::WaveWeights::forSaw();
        }

        float pulseWidthFor (const dsp::Settings& s, int which)
        {
            return which == 0 ? s.osc1PW : which == 1 ? s.osc2PW : 0.5f;
        }

        juce::Range<float> resonanceTravel (juce::Rectangle<float> plot) noexcept
        {
            const auto inset = FilterDisplay::handleRadius + 1.0f;
            return { plot.getY() + inset, juce::jmax (plot.getY() + inset + 1.0f, plot.getBottom() - inset) };
        }

        float logSpan() noexcept { return std::log (FilterDisplay::maxHz / FilterDisplay::minHz); }
    }

    //==============================================================================
    GraphView::GraphView (SettingsSource settingsSource, const juce::String& accessibleTitle)
        : source (std::move (settingsSource))
    {
        setOpaque (true);
        setTitle (accessibleTitle);
        setDescription (accessibleTitle);
        if (source)
            settings = source();
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { if (isShowing()) refresh(); });
    }

    GraphView::~GraphView() = default;

    bool GraphView::changed (const dsp::Settings&, const dsp::Settings&) const { return true; }

    bool GraphView::refresh()
    {
        bool dirty = false;

        if (source)
        {
            const auto now = source();
            if (changed (settings, now))
            {
                settings = now;
                dirty = true;
            }
        }

        const auto live = liveSignature();
        if (live != lastLiveSignature)
        {
            lastLiveSignature = live;
            dirty = true;
        }

        if (dirty)
            repaint();
        return dirty;
    }

    juce::Rectangle<float> GraphView::getPlotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop ((float) titleStripHeight).reduced ((float) plotMargin, (float) plotMargin * 0.6f);
    }

    void GraphView::resized()
    {
        background = {};
        backgroundScale = 0.0f;
    }

    void GraphView::mouseMove (const juce::MouseEvent& e)
    {
        hover = true;
        hoverPoint = e.position;
        repaint();
    }

    void GraphView::mouseExit (const juce::MouseEvent&)
    {
        hover = false;
        repaint();
    }

    void GraphView::paint (juce::Graphics& g)
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();

        if (! background.isValid() || ! juce::exactlyEqual (backgroundScale, scale))
        {
            backgroundScale = scale;
            const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
            const auto h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
            background = juce::Image (juce::Image::RGB, w, h, true);
            juce::Graphics bg (background);
            bg.addTransform (juce::AffineTransform::scale (scale));
            bg.fillAll (Palette::background);
            drawBackground (bg, getPlotArea());
        }

        g.drawImage (background, getLocalBounds().toFloat());
        drawCurves (g, getPlotArea());

        if (hover)
        {
            const auto text = readoutAt (hoverPoint);
            if (text.isNotEmpty())
            {
                const auto plot = getPlotArea();
                g.setColour (Palette::textMuted.withAlpha (0.5f));
                g.drawVerticalLine (juce::roundToInt (juce::jlimit (plot.getX(), plot.getRight(), hoverPoint.x)), plot.getY(), plot.getBottom());
                g.setFont (juce::FontOptions (12.0f));
                const auto box = juce::Rectangle<float> (plot.getRight() - 210.0f, plot.getY() + 2.0f, 206.0f, 18.0f);
                g.setColour (Palette::panelRaised.withAlpha (0.9f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (Palette::textPrimary);
                g.drawText (text, box.reduced (6.0f, 0.0f), juce::Justification::centredRight);
            }
        }
    }

    void GraphView::drawTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft);
    }

    void GraphView::drawHorizontalGrid (juce::Graphics& g, juce::Rectangle<float> plot, int divisions)
    {
        g.setColour (Palette::grid);
        for (int i = 0; i <= divisions; ++i)
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) i / (float) divisions), plot.getX(), plot.getRight());
    }

    //==============================================================================
    OscillatorDisplay::OscillatorDisplay (SettingsSource s) : GraphView (std::move (s), "Oscillator waveforms")
    {
        for (auto& p : paths)
            p.preallocateSpace (3 * 800);
    }

    bool OscillatorDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return a.osc1Wave != b.osc1Wave || a.osc2Wave != b.osc2Wave || a.osc3Wave != b.osc3Wave
            || ! juce::exactlyEqual (a.osc1PW, b.osc1PW) || ! juce::exactlyEqual (a.osc2PW, b.osc2PW);
    }

    void OscillatorDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto third = plot.getWidth() / 3.0f;
        const char* titles[] = { "Osc 1", "Osc 2", "Osc 3" };
        const juce::Colour colours[] { Palette::accent, Palette::spectrum, Palette::frozen };

        for (int i = 0; i < 3; ++i)
        {
            auto area = plot.withX (plot.getX() + third * (float) i).withWidth (third - 8.0f);
            drawHorizontalGrid (g, area, 4);
            g.setColour (Palette::divider);
            g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
            drawTitle (g, { area.getX(), 2.0f, area.getWidth(), (float) titleStripHeight - 2.0f }, titles[i], colours[i]);
        }
    }

    void OscillatorDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto third = plot.getWidth() / 3.0f;
        const juce::Colour colours[] { Palette::accent, Palette::spectrum, Palette::frozen };

        for (int i = 0; i < 3; ++i)
        {
            auto area = plot.withX (plot.getX() + third * (float) i).withWidth (third - 8.0f).reduced (0.0f, 4.0f);
            const auto weights = weightsForOsc (settings, i);
            const auto pw = pulseWidthFor (settings, i);
            const auto peak = juce::jmax (1.0f, std::abs (weights.tri) + std::abs (weights.saw) + std::abs (weights.pulse));

            auto& path = paths[i];
            path.clear();
            const auto points = juce::jlimit (16, 800, (int) area.getWidth());
            for (int n = 0; n <= points; ++n)
            {
                const auto x = (float) n / (float) points;
                auto phase = 2.0f * x;
                phase -= std::floor (phase);
                const auto v = dsp::Oscillator::naive (phase, pw, weights) / peak;
                const auto px = area.getX() + area.getWidth() * x, py = area.getCentreY() - v * area.getHeight() * 0.5f;
                if (n == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
            }

            g.setColour (colours[i]);
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==============================================================================
    FilterDisplay::FilterDisplay (SettingsSource s, juce::RangedAudioParameter* cutoff, juce::RangedAudioParameter* resonance)
        : GraphView (std::move (s), "Filter response"), cutoffParam (cutoff), resoParam (resonance)
    {
        path.preallocateSpace (3 * 1200);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    bool FilterDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return ! juce::exactlyEqual (a.lpfCutoff, b.lpfCutoff) || ! juce::exactlyEqual (a.lpfReso, b.lpfReso);
    }

    float FilterDisplay::lowPassDb (const dsp::Settings& s, float hz) noexcept
    {
        const auto k = dsp::model::ladderK (s.lpfReso);
        const std::complex<float> sj (0.0f, hz / juce::jmax (1.0f, s.lpfCutoff));
        const auto onePole = 1.0f + sj;
        const auto h = 1.0f / (onePole * onePole * onePole * onePole + k);
        return juce::Decibels::gainToDecibels (std::abs (h), -120.0f);
    }

    juce::Point<float> FilterDisplay::positionFor (float hz, float resonance, juce::Rectangle<float> plot) noexcept
    {
        const auto t = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / logSpan();
        const auto travel = resonanceTravel (plot);
        return { plot.getX() + plot.getWidth() * t, travel.getEnd() - travel.getLength() * juce::jlimit (0.0f, 1.0f, resonance) };
    }

    float FilterDisplay::cutoffAt (float x, juce::Rectangle<float> plot) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        return minHz * std::exp (logSpan() * t);
    }

    float FilterDisplay::resonanceAt (float y, juce::Rectangle<float> plot) noexcept
    {
        const auto travel = resonanceTravel (plot);
        return juce::jlimit (0.0f, 1.0f, (travel.getEnd() - y) / travel.getLength());
    }

    float FilterDisplay::cutoffOf() const
    {
        return cutoffParam != nullptr ? cutoffParam->convertFrom0to1 (cutoffParam->getValue()) : settings.lpfCutoff;
    }

    float FilterDisplay::resonanceOf() const
    {
        return resoParam != nullptr ? resoParam->convertFrom0to1 (resoParam->getValue()) : settings.lpfReso;
    }

    juce::Point<float> FilterDisplay::getHandlePosition() const
    {
        return positionFor (cutoffOf(), resonanceOf(), getPlotArea());
    }

    void FilterDisplay::setValues (float hz, float resonance)
    {
        if (cutoffParam == nullptr || resoParam == nullptr)
            return;
        cutoffParam->setValueNotifyingHost (cutoffParam->convertTo0to1 (hz));
        resoParam->setValueNotifyingHost (resoParam->convertTo0to1 (resonance));
        refresh();
    }

    void FilterDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        g.setColour (Palette::grid);
        for (auto hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const auto t = std::log (hz / minHz) / logSpan();
            g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * t), plot.getY(), plot.getBottom());
        }
        drawHorizontalGrid (g, plot, 4);
        drawTitle (g, { plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f }, "Low-pass", Palette::dynamic);
    }

    void FilterDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        path.clear();
        const auto points = juce::jlimit (32, 1200, (int) plot.getWidth());
        constexpr float minDb = -48.0f, maxDb = 24.0f;
        for (int n = 0; n <= points; ++n)
        {
            const auto t = (float) n / (float) points;
            const auto hz = minHz * std::pow (maxHz / minHz, t);
            const auto db = juce::jlimit (minDb, maxDb, lowPassDb (settings, hz));
            const auto x = plot.getX() + plot.getWidth() * t;
            const auto y = plot.getBottom() - plot.getHeight() * (db - minDb) / (maxDb - minDb);
            if (n == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        g.setColour (Palette::dynamic);
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto handle = getHandlePosition();
        g.setColour (Palette::background);
        g.fillEllipse (handle.x - handleRadius, handle.y - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f);
        g.setColour ((hovering || dragging) ? Palette::textPrimary : Palette::dynamic);
        g.drawEllipse (handle.x - handleRadius, handle.y - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f, 1.5f);
        g.fillEllipse (handle.x - 2.5f, handle.y - 2.5f, 5.0f, 5.0f);
    }

    juce::String FilterDisplay::readoutAt (juce::Point<float> p) const
    {
        const auto plot = getPlotArea();
        if (! plot.contains (p))
            return {};
        const auto hz = cutoffAt (p.x, plot);
        return formatHz (hz) + "   " + juce::String (lowPassDb (settings, hz), 1) + " dB";
    }

    void FilterDisplay::mouseMove (const juce::MouseEvent& e)
    {
        GraphView::mouseMove (e);
        const auto near = getHandlePosition().getDistanceFrom (e.position) <= handleRadius + 6.0f;
        if (near != hovering)
        {
            hovering = near;
            setMouseCursor (near ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void FilterDisplay::mouseExit (const juce::MouseEvent& e)
    {
        GraphView::mouseExit (e);
        if (! dragging)
            hovering = false;
        repaint();
    }

    void FilterDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (getHandlePosition().getDistanceFrom (e.position) > handleRadius + 6.0f || cutoffParam == nullptr)
            return;

        dragging = hovering = true;
        const auto plot = getPlotArea();
        const auto at = getHandlePosition();
        const auto travel = resonanceTravel (plot);
        dragX = juce::jlimit (0.0f, 1.0f, (at.x - plot.getX()) / plot.getWidth());
        dragY = juce::jlimit (0.0f, 1.0f, (travel.getEnd() - at.y) / travel.getLength());
        lastDragPosition = e.position;
        cutoffParam->beginChangeGesture();
        resoParam->beginChangeGesture();
    }

    void FilterDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        const auto plot = getPlotArea();
        auto delta = e.position - lastDragPosition;
        if (e.mods.isShiftDown())
            delta *= 0.12f;
        lastDragPosition = e.position;
        dragX = juce::jlimit (0.0f, 1.0f, dragX + delta.x / plot.getWidth());
        dragY = juce::jlimit (0.0f, 1.0f, dragY - delta.y / resonanceTravel (plot).getLength());
        setValues (minHz * std::exp (logSpan() * dragX), dragY);
    }

    void FilterDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (! dragging)
            return;
        dragging = false;
        cutoffParam->endChangeGesture();
        resoParam->endChangeGesture();
    }

    void FilterDisplay::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (cutoffParam == nullptr || resoParam == nullptr)
            return;
        cutoffParam->beginChangeGesture();
        resoParam->beginChangeGesture();
        cutoffParam->setValueNotifyingHost (cutoffParam->getDefaultValue());
        resoParam->setValueNotifyingHost (resoParam->getDefaultValue());
        cutoffParam->endChangeGesture();
        resoParam->endChangeGesture();
        refresh();
    }

    void FilterDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        if (resoParam == nullptr)
            return;
        resoParam->beginChangeGesture();
        resoParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, resoParam->getValue() + wheel.deltaY * 0.08f));
        resoParam->endChangeGesture();
        refresh();
    }

    //==============================================================================
    EnvelopeDisplay::EnvelopeDisplay (SettingsSource s) : GraphView (std::move (s), "Envelopes")
    {
        filterPath.preallocateSpace (3 * 800);
        ampPath.preallocateSpace (3 * 800);
    }

    bool EnvelopeDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return ! sameEnv (a.filterEnv, b.filterEnv) || ! sameEnv (a.ampEnv, b.ampEnv) || a.sharedDecay != b.sharedDecay;
    }

    void EnvelopeDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        drawHorizontalGrid (g, plot, 4);
        g.setColour (Palette::divider);
        g.drawHorizontalLine (juce::roundToInt (plot.getBottom()), plot.getX(), plot.getRight());
        drawTitle (g, { plot.getX(), 2.0f, plot.getWidth() * 0.5f, (float) titleStripHeight - 2.0f }, "Filter", Palette::dynamic);
        drawTitle (g, { plot.getX() + plot.getWidth() * 0.5f, 2.0f, plot.getWidth() * 0.5f, (float) titleStripHeight - 2.0f }, "Amp", Palette::master);
    }

    void EnvelopeDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto drawEnv = [&] (juce::Path& path, const dsp::EnvSettings& env, float releaseOverride, juce::Colour colour)
        {
            const auto release = releaseOverride >= 0.0f ? releaseOverride : env.release;
            const auto total = env.attack + env.decay + 0.35f + release;
            auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * (t / total); };
            auto yOf = [&] (float v) { return plot.getBottom() - plot.getHeight() * v; };

            path.clear();
            path.startNewSubPath (xOf (0.0f), yOf (0.0f));
            path.lineTo (xOf (env.attack), yOf (1.0f));
            path.lineTo (xOf (env.attack + env.decay), yOf (env.sustain));
            path.lineTo (xOf (env.attack + env.decay + 0.35f), yOf (env.sustain));
            path.lineTo (xOf (total), yOf (0.0f));
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        const auto fRelease = settings.sharedDecay ? settings.filterEnv.decay : -1.0f;
        const auto aRelease = settings.sharedDecay ? settings.ampEnv.decay : -1.0f;
        drawEnv (filterPath, settings.filterEnv, fRelease, Palette::dynamic);
        drawEnv (ampPath, settings.ampEnv, aRelease, Palette::master);
        juce::ignoreUnused (formatSeconds);
    }

    //==============================================================================
    LfoDisplay::LfoDisplay (SettingsSource s, dsp::SynthEngine::Tap& t)
        : GraphView (std::move (s), "LFO"), tap (&t)
    {
        path.preallocateSpace (3 * 800);
    }

    bool LfoDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return a.lfoWave != b.lfoWave || ! juce::exactlyEqual (a.lfoRate, b.lfoRate) || a.lfoSync != b.lfoSync;
    }

    int LfoDisplay::liveSignature() const
    {
        return tap != nullptr ? juce::roundToInt (tap->lfo.load() * 40.0f) : 0;
    }

    void LfoDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        drawHorizontalGrid (g, plot, 4);
        g.setColour (Palette::divider);
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
        drawTitle (g, { plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f }, "LFO", Palette::spectrum);
    }

    void LfoDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        path.clear();
        const auto points = juce::jlimit (16, 800, (int) plot.getWidth());
        for (int n = 0; n <= points; ++n)
        {
            const auto t = (float) n / (float) points;
            float v = 0.0f;
            switch (settings.lfoWave)
            {
                case dsp::LfoWave::triangle:    v = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; break;
                case dsp::LfoWave::saw:         v = 2.0f * t - 1.0f; break;
                case dsp::LfoWave::reverseSaw:  v = 1.0f - 2.0f * t; break;
                case dsp::LfoWave::square:      v = t < 0.5f ? 1.0f : -1.0f; break;
                case dsp::LfoWave::sampleHold:  v = t < 0.33f ? 0.6f : t < 0.66f ? -0.4f : 0.15f; break;
            }
            const auto x = plot.getX() + plot.getWidth() * t;
            const auto y = plot.getCentreY() - v * plot.getHeight() * 0.42f;
            if (n == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        g.setColour (Palette::spectrum);
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (tap != nullptr)
        {
            const auto live = juce::jlimit (-1.0f, 1.0f, tap->lfo.load());
            const auto y = plot.getCentreY() - live * plot.getHeight() * 0.42f;
            g.setColour (Palette::textPrimary.withAlpha (0.85f));
            g.fillEllipse (plot.getRight() - 10.0f, y - 3.5f, 7.0f, 7.0f);
        }
    }
}
