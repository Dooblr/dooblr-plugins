#include "Displays.h"

#include <cmath>
#include <complex>

#include "../DSP/Primitives.h"
#include "Theme.h"

namespace dooblrprophet::ui
{
    juce::Colour pageColour (int page)
    {
        switch (page)
        {
            case 0: return Palette::accent;
            case 1: return Palette::dynamic;
            case 2: return Palette::master;
            case 3: return Palette::frozen;
            default: return Palette::spectrum;
        }
    }

    namespace
    {
        constexpr int plotMargin = 10;

        bool sameEnv (const dsp::EnvSettings& a, const dsp::EnvSettings& b)
        {
            return juce::exactlyEqual (a.attack, b.attack) && juce::exactlyEqual (a.decay, b.decay) && juce::exactlyEqual (a.sustain, b.sustain) && juce::exactlyEqual (a.release, b.release);
        }

        bool sameOsc (const dsp::OscSettings& a, const dsp::OscSettings& b)
        {
            return a.switched == b.switched && juce::exactlyEqual (a.shape, b.shape) && juce::exactlyEqual (a.pulseWidth, b.pulseWidth) && a.saw == b.saw
                && a.tri == b.tri && a.pulse == b.pulse;
        }

        juce::String formatHz (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::String formatSeconds (float s)
        {
            return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s";
        }

        const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
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
        // Hidden graphs (other pages) skip the poll; an explicit refresh() always polls.
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
            p.preallocateSpace (3 * 1200);
    }

    bool OscillatorDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return ! sameOsc (a.osc1, b.osc1) || ! sameOsc (a.osc2, b.osc2);
    }

    void OscillatorDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto half = plot.getWidth() * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            auto area = plot.withX (plot.getX() + half * (float) i).withWidth (half - 12.0f);
            if (i == 1)
                area = area.withTrimmedLeft (12.0f).withWidth (half - 12.0f);
            drawHorizontalGrid (g, area, 4);
            g.setColour (Palette::divider);
            g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
            drawTitle (g, { area.getX(), 2.0f, area.getWidth(), (float) titleStripHeight - 2.0f }, i == 0 ? "Osc 1" : "Osc 2",
                       i == 0 ? Palette::accent : Palette::spectrum);
        }
    }

    void OscillatorDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto half = plot.getWidth() * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            const auto& osc = i == 0 ? settings.osc1 : settings.osc2;
            auto area = plot.withX (plot.getX() + half * (float) i).withWidth (half - 12.0f);
            if (i == 1)
                area = area.withTrimmedLeft (12.0f).withWidth (half - 12.0f);
            area = area.reduced (0.0f, 4.0f);

            const auto weights = osc.switched ? dsp::WaveWeights::fromSwitches (osc.saw, osc.tri, osc.pulse) : dsp::WaveWeights::fromShape (osc.shape);
            const auto pw = osc.pulseWidth; // the engine applies Pulse Width in both modes
            const auto peak = juce::jmax (1.0f, std::abs (weights.tri) + std::abs (weights.saw) + std::abs (weights.pulse));

            auto& path = paths[i];
            path.clear();
            const auto points = juce::jlimit (16, 1200, (int) area.getWidth());
            for (int n = 0; n <= points; ++n)
            {
                const auto x = (float) n / (float) points;
                auto phase = 2.0f * x;
                phase -= std::floor (phase);
                const auto v = dsp::Oscillator::naive (phase, pw, weights) / peak;
                const auto px = area.getX() + area.getWidth() * x, py = area.getCentreY() - v * area.getHeight() * 0.5f;
                if (n == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
            }

            g.setColour (i == 0 ? Palette::accent : Palette::spectrum);
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==============================================================================
    FilterDisplay::FilterDisplay (SettingsSource s, Parameters p) : GraphView (std::move (s), "Filter response"), parameters (p)
    {
        lowPath.preallocateSpace (3 * 1200);
        highPath.preallocateSpace (3 * 1200);
        combinedPath.preallocateSpace (3 * 1200);
    }

    bool FilterDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return ! juce::exactlyEqual (a.lpfCutoff, b.lpfCutoff) || ! juce::exactlyEqual (a.lpfReso, b.lpfReso) || a.lpfModel != b.lpfModel
            || ! juce::exactlyEqual (a.hpfCutoff, b.hpfCutoff) || ! juce::exactlyEqual (a.hpfReso, b.hpfReso);
    }

    float FilterDisplay::lowPassDb (const dsp::Settings& s, float hz) noexcept
    {
        // Linear ladder core (docs/analog-reference.md): H = 1 / ((1 + s/wc)^4 + k). No level
        // compensation: the bass loss with resonance (1 / (1 + k) at DC) is part of the sound.
        const auto k = dsp::model::ladderK (dsp::model::ladderModel (s.lpfModel), s.lpfReso);
        const std::complex<float> sj (0.0f, hz / juce::jmax (1.0f, s.lpfCutoff));
        const auto onePole = 1.0f + sj;
        const auto h = 1.0f / (onePole * onePole * onePole * onePole + k);
        return juce::Decibels::gainToDecibels (std::abs (h), -120.0f);
    }

    float FilterDisplay::highPassDb (const dsp::Settings& s, float hz) noexcept
    {
        const auto k = 2.0f - 1.88f * juce::jlimit (0.0f, 1.0f, s.hpfReso);
        const std::complex<float> sj (0.0f, hz / juce::jmax (1.0f, s.hpfCutoff));
        const auto h = sj * sj / (sj * sj + k * sj + 1.0f);
        return juce::Decibels::gainToDecibels (std::abs (h), -120.0f);
    }

    float FilterDisplay::xToHz (float x, juce::Rectangle<float> plot) const noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        return minHz * std::pow (maxHz / minHz, t);
    }

    namespace
    {
        // The handles' vertical travel keeps the whole handle inside the plot.
        juce::Range<float> resonanceTravel (juce::Rectangle<float> plot) noexcept
        {
            const auto inset = FilterDisplay::handleRadius + 1.0f;
            return { plot.getY() + inset, juce::jmax (plot.getY() + inset + 1.0f, plot.getBottom() - inset) };
        }

        float logSpan() noexcept { return std::log (FilterDisplay::maxHz / FilterDisplay::minHz); }
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

    juce::RangedAudioParameter* FilterDisplay::cutoffParameter (int handle) const noexcept
    {
        return handle == lowPassHandle ? parameters.lowCutoff : handle == highPassHandle ? parameters.highCutoff : nullptr;
    }

    juce::RangedAudioParameter* FilterDisplay::resonanceParameter (int handle) const noexcept
    {
        return handle == lowPassHandle ? parameters.lowResonance : handle == highPassHandle ? parameters.highResonance : nullptr;
    }

    float FilterDisplay::cutoffOf (int handle) const noexcept
    {
        if (auto* p = cutoffParameter (handle))
            return p->convertFrom0to1 (p->getValue());
        return handle == lowPassHandle ? settings.lpfCutoff : settings.hpfCutoff;
    }

    float FilterDisplay::resonanceOf (int handle) const noexcept
    {
        if (auto* p = resonanceParameter (handle))
            return p->convertFrom0to1 (p->getValue());
        return handle == lowPassHandle ? settings.lpfReso : settings.hpfReso;
    }

    juce::Point<float> FilterDisplay::getHandlePosition (int handle) const
    {
        // From the parameters themselves, so the handle never lags its knob.
        return positionFor (cutoffOf (handle), resonanceOf (handle), getPlotArea());
    }

    int FilterDisplay::handleAt (juce::Point<float> p) const
    {
        if (cutoffParameter (lowPassHandle) == nullptr)
            return noHandle;

        int best = noHandle;
        auto bestDistance = handleHitRadius;
        for (int h : { lowPassHandle, highPassHandle })
        {
            const auto d = getHandlePosition (h).getDistanceFrom (p);
            if (d <= bestDistance) { best = h; bestDistance = d; }
        }
        return best;
    }

    void FilterDisplay::setValues (int handle, float hz, float resonance)
    {
        auto* cutoff = cutoffParameter (handle);
        auto* reso = resonanceParameter (handle);
        if (cutoff == nullptr || reso == nullptr)
            return;
        cutoff->setValueNotifyingHost (cutoff->convertTo0to1 (hz));
        reso->setValueNotifyingHost (reso->convertTo0to1 (resonance));
        refresh();
    }

    void FilterDisplay::mouseMove (const juce::MouseEvent& e)
    {
        GraphView::mouseMove (e);
        int nearest = noHandle;
        if (cutoffParameter (lowPassHandle) != nullptr)
        {
            auto bestDistance = hoverProximity;
            for (int h : { lowPassHandle, highPassHandle })
            {
                const auto d = getHandlePosition (h).getDistanceFrom (e.position);
                if (d <= bestDistance) { nearest = h; bestDistance = d; }
            }
        }
        if (nearest != hoverHandle)
        {
            hoverHandle = nearest;
            setMouseCursor (nearest == noHandle ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void FilterDisplay::mouseExit (const juce::MouseEvent& e)
    {
        GraphView::mouseExit (e);
        if (dragHandle == noHandle)
            hoverHandle = noHandle;
        repaint();
    }

    void FilterDisplay::mouseDown (const juce::MouseEvent& e)
    {
        dragHandle = handleAt (e.position);
        if (dragHandle == noHandle)
            return;

        hoverHandle = dragHandle;
        const auto plot = getPlotArea();
        const auto at = getHandlePosition (dragHandle);
        const auto travel = resonanceTravel (plot);
        dragX = juce::jlimit (0.0f, 1.0f, (at.x - plot.getX()) / plot.getWidth());
        dragY = juce::jlimit (0.0f, 1.0f, (travel.getEnd() - at.y) / travel.getLength());
        lastDragPosition = e.position;
        // Both parameters in one gesture: the undo history commits them as one step.
        cutoffParameter (dragHandle)->beginChangeGesture();
        resonanceParameter (dragHandle)->beginChangeGesture();
    }

    void FilterDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragHandle == noHandle)
            return;

        // Relative movement, so Shift (fine) can be pressed or released mid-drag without a jump.
        const auto plot = getPlotArea();
        const auto travel = resonanceTravel (plot);
        const auto scale = e.mods.isShiftDown() ? fineFactor : 1.0f;
        const auto delta = e.position - lastDragPosition;
        lastDragPosition = e.position;
        dragX = juce::jlimit (0.0f, 1.0f, dragX + delta.x / plot.getWidth() * scale);
        dragY = juce::jlimit (0.0f, 1.0f, dragY - delta.y / travel.getLength() * scale);

        setValues (dragHandle, minHz * std::exp (logSpan() * dragX), dragY);
        setHoverPoint (getHandlePosition (dragHandle));
    }

    void FilterDisplay::mouseUp (const juce::MouseEvent& e)
    {
        if (dragHandle == noHandle)
            return;

        resonanceParameter (dragHandle)->endChangeGesture();
        cutoffParameter (dragHandle)->endChangeGesture();
        dragHandle = noHandle;
        if (! getLocalBounds().toFloat().contains (e.position))
            hoverHandle = noHandle;
        repaint();
    }

    void FilterDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto handle = handleAt (e.position);
        if (handle == noHandle)
            return;

        auto* cutoff = cutoffParameter (handle);
        auto* reso = resonanceParameter (handle);
        cutoff->beginChangeGesture();
        reso->beginChangeGesture();
        cutoff->setValueNotifyingHost (cutoff->getDefaultValue());
        reso->setValueNotifyingHost (reso->getDefaultValue());
        reso->endChangeGesture();
        cutoff->endChangeGesture();
        refresh();
    }

    void FilterDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        auto handle = handleAt (e.position);
        if (handle == noHandle)
            handle = hoverHandle;
        if (handle == noHandle)
        {
            GraphView::mouseWheelMove (e, wheel);
            return;
        }

        auto* reso = resonanceParameter (handle);
        const auto delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * wheelResonancePerUnit
                           * (e.mods.isShiftDown() ? fineFactor : 1.0f);
        const auto current = resonanceOf (handle);
        reso->beginChangeGesture();
        reso->setValueNotifyingHost (reso->convertTo0to1 (juce::jlimit (0.0f, 1.0f, current + delta)));
        reso->endChangeGesture();
        refresh();
    }

    void FilterDisplay::drawHandle (juce::Graphics& g, int handle, juce::Colour colour)
    {
        const auto centre = getHandlePosition (handle);
        const auto active = handle == hoverHandle || handle == dragHandle;
        if (active)
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillEllipse (juce::Rectangle<float> (2.0f * handleHitRadius, 2.0f * handleHitRadius).withCentre (centre));
        }
        const auto r = active ? handleRadius : handleRadius - 1.5f;
        const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre);
        g.setColour (Palette::background);
        g.fillEllipse (dot);
        g.setColour (colour);
        g.drawEllipse (dot.reduced (1.0f), 2.0f);
        if (active)
            g.fillEllipse (dot.reduced (3.5f));
    }

    void FilterDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        g.setColour (Palette::grid);
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto x = plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        for (float db = maxDb; db >= minDb; db -= 12.0f)
        {
            const auto y = juce::jmap (db, maxDb, minDb, plot.getY(), plot.getBottom());
            g.setColour (db == 0.0f ? Palette::divider.brighter (0.2f) : Palette::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        const auto titleArea = juce::Rectangle<float> (plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f);
        drawTitle (g, titleArea, "Low-pass ladder", Palette::dynamic);
        drawTitle (g, titleArea.withTrimmedLeft (150.0f), "High-pass", Palette::spectrum);
    }

    void FilterDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        lowPath.clear();
        highPath.clear();
        const auto points = juce::jlimit (16, 1200, (int) plot.getWidth());
        const auto toY = [plot] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), maxDb, minDb, plot.getY(), plot.getBottom()); };

        for (int n = 0; n <= points; ++n)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) n / (float) points;
            const auto hz = xToHz (x, plot);
            const auto lp = toY (lowPassDb (settings, hz)), hp = toY (highPassDb (settings, hz));
            if (n == 0) { lowPath.startNewSubPath (x, lp); highPath.startNewSubPath (x, hp); }
            else { lowPath.lineTo (x, lp); highPath.lineTo (x, hp); }
        }

        g.setColour (Palette::spectrum.withAlpha (0.85f));
        g.strokePath (highPath, juce::PathStrokeType (1.5f));
        g.setColour (Palette::dynamic);
        g.strokePath (lowPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (cutoffParameter (lowPassHandle) != nullptr)
        {
            // The active one on top, so it stays grabbable when the two overlap.
            const auto top = dragHandle != noHandle ? dragHandle : hoverHandle != noHandle ? hoverHandle : lowPassHandle;
            const auto under = top == lowPassHandle ? highPassHandle : lowPassHandle;
            drawHandle (g, under, under == lowPassHandle ? Palette::dynamic : Palette::spectrum);
            drawHandle (g, top, top == lowPassHandle ? Palette::dynamic : Palette::spectrum);
        }
    }

    juce::String FilterDisplay::readoutAt (juce::Point<float> p) const
    {
        const auto handle = dragHandle != noHandle ? dragHandle : hoverHandle;
        if (handle != noHandle)
        {
            const auto hz = cutoffOf (handle);
            const auto reso = resonanceOf (handle);
            return juce::String (handle == lowPassHandle ? "Low-pass " : "High-pass ") + formatHz (hz) + "   Res "
                 + juce::String (juce::roundToInt (reso * 100.0f)) + "%";
        }
        const auto plot = getPlotArea();
        if (! plot.contains (p))
            return {};
        const auto hz = xToHz (p.x, plot);
        const auto db = lowPassDb (settings, hz) + highPassDb (settings, hz);
        return formatHz (hz) + "   " + juce::String (db, 1) + " dB";
    }

    //==============================================================================
    EnvelopeDisplay::EnvelopeDisplay (SettingsSource s) : GraphView (std::move (s), "Filter and amp envelopes")
    {
        filterPath.preallocateSpace (3 * 1200);
        ampPath.preallocateSpace (3 * 1200);
    }

    bool EnvelopeDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return ! sameEnv (a.filterEnv, b.filterEnv) || ! sameEnv (a.ampEnv, b.ampEnv);
    }

    float EnvelopeDisplay::spanSeconds (const dsp::Settings& s) noexcept
    {
        const auto length = [] (const dsp::EnvSettings& e) { return e.attack + e.decay + e.release; };
        const auto longest = juce::jmax (length (s.filterEnv), length (s.ampEnv));
        return juce::jmax (0.05f, longest * 1.25f);
    }

    void EnvelopeDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        drawHorizontalGrid (g, plot, 4);
        const auto titleArea = juce::Rectangle<float> (plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f);
        drawTitle (g, titleArea, "Filter envelope", Palette::dynamic);
        drawTitle (g, titleArea.withTrimmedLeft (150.0f), "Amp envelope", Palette::master);
    }

    void EnvelopeDisplay::trace (juce::Path& path, const dsp::EnvSettings& env, juce::Rectangle<float> plot, float span, float hold) const
    {
        path.clear();
        const auto points = juce::jlimit (16, 1200, (int) plot.getWidth());
        const auto pointRate = (double) points / span; // one envelope step per pixel
        dsp::Envelope e;
        e.setTimes (pointRate, env.attack, env.decay, env.sustain, env.release);
        e.noteOn();
        const auto releaseAt = juce::roundToInt ((env.attack + env.decay + hold) * pointRate);

        for (int n = 0; n <= points; ++n)
        {
            if (n == releaseAt)
                e.noteOff();
            const auto level = e.next();
            const auto x = plot.getX() + plot.getWidth() * (float) n / (float) points;
            const auto y = plot.getBottom() - juce::jlimit (0.0f, 1.0f, level) * plot.getHeight();
            if (n == 0) path.startNewSubPath (x, plot.getBottom()); else path.lineTo (x, y);
        }
    }

    void EnvelopeDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto span = spanSeconds (settings);
        // Both hold their sustain until the same moment (the longer attack + decay), like one held key.
        const auto keyUp = juce::jmax (settings.filterEnv.attack + settings.filterEnv.decay, settings.ampEnv.attack + settings.ampEnv.decay) + 0.15f * span;
        trace (filterPath, settings.filterEnv, plot, span, keyUp - settings.filterEnv.attack - settings.filterEnv.decay);
        trace (ampPath, settings.ampEnv, plot, span, keyUp - settings.ampEnv.attack - settings.ampEnv.decay);

        g.setColour (Palette::dynamic);
        g.strokePath (filterPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::master);
        g.strokePath (ampPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto x = plot.getX() + plot.getWidth() * keyUp / span;
        g.setColour (Palette::textMuted.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    juce::String EnvelopeDisplay::readoutAt (juce::Point<float> p) const
    {
        const auto plot = getPlotArea();
        if (! plot.contains (p))
            return {};
        const auto t = (p.x - plot.getX()) / plot.getWidth() * spanSeconds (settings);
        return formatSeconds (t);
    }

    //==============================================================================
    LfoDisplay::LfoDisplay (SettingsSource s, const dsp::SynthEngine::VoiceTap& voiceTap)
        : GraphView (std::move (s), "LFO wave"), tap (voiceTap)
    {
        path.preallocateSpace (3 * 1200);
    }

    bool LfoDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return a.lfoWave != b.lfoWave || ! juce::exactlyEqual (a.lfoNoiseMix, b.lfoNoiseMix);
    }

    int LfoDisplay::liveSignature() const
    {
        return juce::roundToInt (tap.lfo.load (std::memory_order_relaxed) * 200.0f);
    }

    void LfoDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        drawHorizontalGrid (g, plot, 4);
        drawTitle (g, { plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f }, "LFO", Palette::frozen);
    }

    void LfoDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto area = plot.reduced (0.0f, 4.0f).withTrimmedRight (28.0f);
        path.clear();
        const auto points = juce::jlimit (16, 1200, (int) area.getWidth());
        dsp::Random random;
        random.seed (0x51ab1e);
        float held = random.bipolar();
        int lastCycle = 0;

        for (int n = 0; n <= points; ++n)
        {
            const auto x = (float) n / (float) points;
            const auto cycles = 2.0f * x;
            const auto cycle = juce::jmin (1, (int) cycles);
            if (cycle != lastCycle) { held = random.bipolar(); lastCycle = cycle; }
            const auto p = cycles - std::floor (cycles);
            float v = 0.0f;
            switch (settings.lfoWave)
            {
                case dsp::LfoWave::triangle:   v = 1.0f - 4.0f * std::abs (p - 0.5f); break;
                case dsp::LfoWave::saw:        v = 2.0f * p - 1.0f; break;
                case dsp::LfoWave::reverseSaw: v = 1.0f - 2.0f * p; break;
                case dsp::LfoWave::square:     v = p < 0.5f ? 1.0f : -1.0f; break;
                case dsp::LfoWave::sampleHold: v = held; break;
            }
            const auto px = area.getX() + area.getWidth() * x, py = area.getCentreY() - v * area.getHeight() * 0.5f;
            if (n == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }

        g.setColour (Palette::frozen.withAlpha (settings.lfoNoiseMix > 0.5f ? 0.5f : 1.0f));
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));

        // The applied modulation now (wave x amount, after the noise mix): a bar at the right.
        const auto bar = juce::Rectangle<float> (plot.getRight() - 14.0f, area.getY(), 8.0f, area.getHeight());
        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (bar, 3.0f);
        const auto v = juce::jlimit (-1.0f, 1.0f, tap.lfo.load (std::memory_order_relaxed));
        const auto y0 = bar.getCentreY(), y1 = bar.getCentreY() - v * bar.getHeight() * 0.5f;
        g.setColour (Palette::frozen);
        g.fillRect (juce::Rectangle<float> (bar.getX(), juce::jmin (y0, y1), bar.getWidth(), juce::jmax (1.0f, std::abs (y1 - y0))));
    }

    //==============================================================================
    VoiceDisplay::VoiceDisplay (SettingsSource s, const dsp::SynthEngine::VoiceTap& voiceTap)
        : GraphView (std::move (s), "Voice activity"), tap (voiceTap)
    {
        for (int n = 0; n < 128; ++n)
            noteLabels[(size_t) n] = juce::String (noteNames[n % 12]) + juce::String (n / 12 - 1);
        for (int i = 0; i < dsp::maxVoices; ++i)
            laneLabels[(size_t) i] = juce::String (i + 1);
    }

    bool VoiceDisplay::changed (const dsp::Settings& a, const dsp::Settings& b) const
    {
        return a.voices != b.voices || a.mode != b.mode || a.unisonVoices != b.unisonVoices;
    }

    int VoiceDisplay::liveSignature() const
    {
        int hash = 17;
        for (int i = 0; i < dsp::maxVoices; ++i)
        {
            hash = hash * 31 + tap.notes[(size_t) i].load (std::memory_order_relaxed);
            hash = hash * 31 + juce::roundToInt (tap.levels[(size_t) i].load (std::memory_order_relaxed) * 60.0f);
        }
        return hash;
    }

    void VoiceDisplay::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        drawTitle (g, { plot.getX(), 2.0f, plot.getWidth(), (float) titleStripHeight - 2.0f }, "Voices", Palette::spectrum);
    }

    void VoiceDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto inUse = settings.mode == dsp::VoiceMode::poly ? settings.voices
                         : settings.mode == dsp::VoiceMode::unison ? settings.unisonVoices : 1;
        const auto laneWidth = plot.getWidth() / (float) dsp::maxVoices;

        for (int i = 0; i < dsp::maxVoices; ++i)
        {
            const auto lane = juce::Rectangle<float> (plot.getX() + laneWidth * (float) i, plot.getY(), laneWidth, plot.getHeight()).reduced (4.0f, 0.0f);
            const auto enabled = i < inUse;
            g.setColour (enabled ? Palette::panel : Palette::background.brighter (0.03f));
            g.fillRoundedRectangle (lane, 4.0f);

            const auto note = tap.notes[(size_t) i].load (std::memory_order_relaxed);
            const auto level = juce::jlimit (0.0f, 1.0f, tap.levels[(size_t) i].load (std::memory_order_relaxed));
            const auto meter = lane.reduced (6.0f).withTrimmedTop (22.0f).withTrimmedBottom (20.0f);

            if (note >= 0)
            {
                g.setColour (Palette::spectrum.withAlpha (0.85f));
                g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * level), 2.0f);
            }

            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (enabled ? Palette::textMuted : Palette::textMuted.withAlpha (0.3f));
            g.drawText (laneLabels[(size_t) i], lane.withHeight (20.0f), juce::Justification::centred);

            if (note >= 0)
            {
                g.setColour (Palette::textPrimary);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (noteLabels[(size_t) juce::jlimit (0, 127, note)], lane.withTop (lane.getBottom() - 20.0f),
                            juce::Justification::centred);
            }
        }
    }
}
