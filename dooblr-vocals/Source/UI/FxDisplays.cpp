#include "FxDisplays.h"

#include "../DSP/TimeFx.h"
#include "Theme.h"

#include <cmath>

namespace dooblrvocals::ui
{
    namespace
    {
        juce::String sep() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }

        juce::String formatMs (double ms)
        {
            return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
        }

        juce::String formatHz (double hz)
        {
            return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k" : juce::String (juce::roundToInt (hz));
        }

        double niceStep (double span)
        {
            for (const auto step : { 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0 })
                if (span / step <= 8.0)
                    return step;
            return 10.0;
        }

        void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> plot, double span)
        {
            const auto step = niceStep (span);
            g.setFont (juce::FontOptions (10.0f));

            for (auto t = 0.0; t <= span + 1.0e-9; t += step)
            {
                const auto x = plot.getX() + (float) (t / span) * plot.getWidth();
                g.setColour (Palette::grid);
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.setColour (Palette::textMuted.withAlpha (0.8f));
                g.drawText (formatMs (t * 1000.0), juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 14.0f, 60.0f, 12.0f),
                            juce::Justification::centredLeft);
            }
        }

        /** Off state (design system \u00a77: no help captions on graphs): the plot is dimmed, the
            tab's own power toggle says why. */
        void dimPlot (juce::Graphics& g, juce::Rectangle<float> area)
        {
            g.setColour (Palette::background.withAlpha (0.55f));
            g.fillRoundedRectangle (area, 6.0f);
        }

        /** Physical pixels per logical pixel of the context being painted into. */
        float physicalScale (juce::Graphics& g)
        {
            return juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        }

        juce::uint64 mix (juce::uint64 hash, double value)
        {
            return (hash * 1000003u) ^ (juce::uint64) (juce::int64) std::llround (value * 1000.0);
        }

        /** Draws a cached picture, re-rendering it only when its key, size or scale changed. */
        template <typename Draw>
        bool drawCached (juce::Graphics& g, juce::Component& c, juce::Image& picture, float& pictureScale,
                         juce::uint64& builtKey, juce::uint64 key, Draw&& draw)
        {
            const auto scale = physicalScale (g);
            const auto w = juce::jmax (1, juce::roundToInt ((float) c.getWidth() * scale));
            const auto h = juce::jmax (1, juce::roundToInt ((float) c.getHeight() * scale));
            const auto rebuild = ! picture.isValid() || picture.getWidth() != w || picture.getHeight() != h
                              || ! juce::approximatelyEqual (pictureScale, scale) || builtKey != key;

            if (rebuild)
            {
                picture = juce::Image (juce::Image::ARGB, w, h, true, juce::NativeImageType());
                juce::Graphics pg (picture);
                pg.addTransform (juce::AffineTransform::scale (scale));
                draw (pg);
                pictureScale = scale;
                builtKey = key;
            }

            g.drawImage (picture, c.getLocalBounds().toFloat());
            return rebuild;
        }

        void drawReadout (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour)
        {
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (colour);
            g.drawText (text, area.removeFromTop (16.0f).reduced (8.0f, 0.0f), juce::Justification::centredRight);
        }
    }

    //==============================================================================
    ReverbDisplay::ReverbDisplay (juce::AudioProcessorValueTreeState& s) : reader (s)
    {
        setComponentID ("reverbDisplay");
        setOpaque (true);
    }

    double ReverbDisplay::spanSeconds (double rt60, double preDelayMs) noexcept
    {
        return juce::jlimit (0.3, 14.0, preDelayMs * 0.001 + rt60 * 1.15 + 0.05);
    }

    juce::uint64 ReverbDisplay::pictureKey() const
    {
        const auto& s = shown;
        auto h = (juce::uint64) 0x5eb;
        for (const auto v : { (double) s.enabled, (double) s.preDelayMs, (double) s.preDelaySync, (double) s.decaySeconds,
                              (double) s.size, (double) s.damping, (double) s.lowCutHz, (double) s.highCutHz,
                              (double) s.earlyLate, (double) s.diffusion, (double) s.duckDb,
                              s.duckDb > 0.05f ? std::round (std::min (0.0f, shownDuckDb) * 10.0f) / 10.0 : 0.0,
                              s.preDelaySync > 0 ? shownBpm : 0.0 })
            h = mix (h, v);
        return h;
    }

    void ReverbDisplay::refresh (float duckDb, double bpm)
    {
        shown = reader.read().reverb;
        shownDuckDb = duckDb;
        shownBpm = bpm;
        haveShown = true;

        const auto key = pictureKey();
        if (key != shownKey)
        {
            shownKey = key;
            ++repaintRequests;
            repaint();
        }
    }

    void ReverbDisplay::paint (juce::Graphics& g)
    {
        if (! haveShown)
        {
            shown = reader.read().reverb;
            haveShown = true;
            shownKey = pictureKey();
        }

        if (drawCached (g, *this, picture, pictureScale, pictureBuiltKey, shownKey, [this] (juce::Graphics& pg) { drawPicture (pg); }))
            ++pictureBuilds;
    }

    void ReverbDisplay::drawPicture (juce::Graphics& g) const
    {
        g.fillAll (Palette::background);
        auto area = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (area, 6.0f);

        auto inner = area.reduced (10.0f, 8.0f);
        const auto band = inner.removeFromBottom (22.0f);
        inner.removeFromBottom (6.0f);
        auto plot = inner.withTrimmedTop (18.0f);

        const auto& s = shown;
        const auto pre = dsp::VocalReverb::preDelayMsFor (s, shownBpm);
        const auto rt60 = (double) juce::jlimit (0.2f, 12.0f, s.decaySeconds);
        const auto span = spanSeconds (rt60, pre);
        constexpr float floorDb = -60.0f;
        const auto xFor = [&] (double t) { return plot.getX() + (float) juce::jlimit (0.0, 1.0, t / span) * plot.getWidth(); };
        const auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (floorDb, 0.0f, db), floorDb, 0.0f, plot.getBottom(), plot.getY()); };

        for (const auto db : { -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (Palette::grid);
            g.fillRect (plot.getX(), yFor (db), plot.getWidth(), 1.0f);
        }
        drawTimeGrid (g, plot, span);

        const auto colour = Palette::fx;
        const auto balance = (double) juce::jlimit (0.0f, 1.0f, s.earlyLate) * juce::MathConstants<double>::halfPi;
        const auto earlyLevel = std::min (1.0, std::cos (balance) * std::sqrt (2.0));
        const auto lateLevel = std::min (1.0, std::sin (balance) * std::sqrt (2.0));

        // The dry hit and the pre-delay.
        g.setColour (Palette::textPrimary.withAlpha (0.8f));
        g.fillRect (xFor (0.0), plot.getY(), 2.0f, plot.getHeight());
        const auto preX = xFor (pre * 0.001);
        if (pre > 0.5)
        {
            const auto y = plot.getY() + 6.0f;
            g.setColour (colour.withAlpha (0.8f));
            g.drawArrow ({ xFor (0.0) + 3.0f, y, preX, y }, 1.2f, 5.0f, 5.0f);
        }

        // The late tail: mid decay (filled) and the faster high-frequency decay (line).
        const auto lateStart = pre * 0.001 + 0.0313 * (0.4 + 1.2 * (double) s.size);
        const auto lateTop = (float) juce::Decibels::gainToDecibels (lateLevel * 0.7, -100.0);
        const auto hfRatio = 1.0 - 0.85 * (double) juce::jlimit (0.0f, 1.0f, s.damping);
        juce::Path tail, highs;
        tail.startNewSubPath (xFor (lateStart), plot.getBottom());
        const auto steps = 60;
        for (int i = 0; i <= steps; ++i)
        {
            const auto t = lateStart + (span - lateStart) * (double) i / steps;
            const auto dt = t - lateStart;
            // A short build-up (diffusion) then the exponential decay.
            const auto build = 1.0 - std::exp (-dt / (0.012 + 0.03 * (double) s.diffusion));
            const auto mid = lateTop + (float) (juce::Decibels::gainToDecibels (std::max (1.0e-5, build), -100.0) - 60.0 * dt / rt60);
            const auto hi = lateTop + (float) (juce::Decibels::gainToDecibels (std::max (1.0e-5, build), -100.0) - 60.0 * dt / (rt60 * hfRatio));
            tail.lineTo (xFor (t), yFor (mid));
            if (i == 0) highs.startNewSubPath (xFor (t), yFor (hi));
            else highs.lineTo (xFor (t), yFor (hi));
        }
        tail.lineTo (xFor (span), plot.getBottom());
        tail.closeSubPath();

        if (lateLevel > 0.001)
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillPath (tail);
            g.setColour (colour.withAlpha (0.85f));
            g.strokePath (tail, juce::PathStrokeType (1.4f));
            g.setColour (Palette::spectrum.withAlpha (0.65f));
            g.strokePath (highs, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Early reflections.
        if (earlyLevel > 0.001)
        {
            g.setColour (Palette::dynamic.withAlpha (0.9f));
            for (int t = 0; t < dsp::VocalReverb::numEarly; ++t)
            {
                const auto time = pre * 0.001 + dsp::VocalReverb::earlyTapMs (t, s.size) * 0.001;
                const auto db = (float) juce::Decibels::gainToDecibels (earlyLevel * 0.8 * (double) dsp::VocalReverb::earlyTapGain (t), -100.0);
                const auto x = xFor (time);
                g.fillRect (x, yFor (db), 1.6f, plot.getBottom() - yFor (db));
            }
        }

        // Legend and readouts.
        g.setFont (juce::FontOptions (10.5f));
        auto legend = inner.removeFromTop (16.0f).reduced (2.0f, 0.0f);
        g.setColour (Palette::dynamic);
        g.drawText ("early", legend.removeFromLeft (40.0f), juce::Justification::centredLeft);
        g.setColour (colour);
        g.drawText ("late (mids)", legend.removeFromLeft (70.0f), juce::Justification::centredLeft);
        g.setColour (Palette::spectrum);
        g.drawText ("highs", legend.removeFromLeft (40.0f), juce::Justification::centredLeft);

        juce::String readout = "RT60 " + juce::String (rt60, rt60 < 10.0 ? 2 : 1) + " s" + sep() + "Pre " + formatMs (pre);
        if (s.preDelaySync > 0)
            readout << " (" << dsp::preDelaySyncName (s.preDelaySync) << ")";
        if (s.duckDb > 0.05f)
            readout << sep() << "Duck " << juce::String (std::min (0.0f, shownDuckDb), 1) << " dB";
        drawReadout (g, area.reduced (0.0f, 6.0f), readout, Palette::textPrimary);

        // The wet band on a log frequency axis.
        const auto fx = [&] (double hz) { return band.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * band.getWidth(); };
        g.setColour (Palette::background);
        g.fillRoundedRectangle (band, 4.0f);
        const auto lo = juce::jlimit (20.0, 20000.0, (double) s.lowCutHz), hi = juce::jlimit (20.0, 20000.0, (double) s.highCutHz);
        g.setColour (colour.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (fx (lo), band.getY(), std::max (fx (lo) + 2.0f, fx (hi)), band.getBottom()), 4.0f);
        g.setColour (Palette::textPrimary.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Wet " + formatHz (lo) + " - " + formatHz (hi) + " Hz", band.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
        for (const auto hz : { 100.0, 1000.0, 10000.0 })
        {
            g.setColour (Palette::grid.brighter (0.2f));
            g.fillRect (fx (hz), band.getY(), 1.0f, band.getHeight());
        }

        if (! s.enabled)
            dimPlot (g, plot);
    }

    //==============================================================================
    DelayDisplay::DelayDisplay (juce::AudioProcessorValueTreeState& s) : reader (s)
    {
        setComponentID ("delayDisplay");
        setOpaque (true);
    }

    juce::uint64 DelayDisplay::pictureKey() const
    {
        const auto& s = shown;
        auto h = (juce::uint64) 0xde1a;
        for (const auto v : { (double) s.enabled, (double) s.mode, (double) s.sync, (double) s.noteL, (double) s.noteR,
                              (double) s.timeLMs, (double) s.timeRMs, (double) s.feedback, (double) s.mix, (double) s.duckDb,
                              s.duckDb > 0.05f ? std::round (std::min (0.0f, shownDuckDb) * 10.0f) / 10.0 : 0.0,
                              s.sync ? shownBpm : 0.0 })
            h = mix (h, v);
        return h;
    }

    void DelayDisplay::refresh (float duckDb, double bpm)
    {
        shown = reader.read().delay;
        shownDuckDb = duckDb;
        shownBpm = bpm;
        haveShown = true;

        const auto key = pictureKey();
        if (key != shownKey)
        {
            shownKey = key;
            ++repaintRequests;
            repaint();
        }
    }

    void DelayDisplay::paint (juce::Graphics& g)
    {
        if (! haveShown)
        {
            shown = reader.read().delay;
            haveShown = true;
            shownKey = pictureKey();
        }

        if (drawCached (g, *this, picture, pictureScale, pictureBuiltKey, shownKey, [this] (juce::Graphics& pg) { drawPicture (pg); }))
            ++pictureBuilds;
    }

    void DelayDisplay::drawPicture (juce::Graphics& g) const
    {
        g.fillAll (Palette::background);
        auto area = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (area, 6.0f);

        auto inner = area.reduced (10.0f, 8.0f);
        inner.removeFromTop (18.0f);
        const auto plot = inner;
        const auto& s = shown;
        constexpr double rate = 48000.0;
        const auto dL = dsp::VocalDelay::delaySamplesFor (s, 0, shownBpm, rate) / rate;
        const auto dR = dsp::VocalDelay::delaySamplesFor (s, 1, shownBpm, rate) / rate;
        const auto fb = (double) juce::jlimit (0.0f, dsp::VocalDelay::maxFeedback, s.feedback);
        constexpr double floorDb = -48.0;

        // Repeats until -48 dB (at most 16), at least two.
        auto repeats = fb > 0.001 ? (int) std::ceil (floorDb / juce::Decibels::gainToDecibels (fb, -100.0)) : 1;
        repeats = juce::jlimit (1, 16, repeats);
        const auto longest = s.mode == dsp::DelaySettings::pingPong ? dL : std::max (dL, dR);
        const auto span = juce::jlimit (0.1, 30.0, longest * (double) std::max (2, repeats) * 1.08);

        const auto midY = plot.getCentreY();
        const auto half = plot.getHeight() * 0.5f - 16.0f;
        const auto xFor = [&] (double t) { return plot.getX() + (float) juce::jlimit (0.0, 1.0, t / span) * plot.getWidth(); };
        const auto hFor = [&] (double gain) { return (float) juce::jlimit (0.0, 1.0, 1.0 - juce::Decibels::gainToDecibels (gain, -100.0) / floorDb) * half; };

        drawTimeGrid (g, plot, span);
        g.setColour (Palette::divider);
        g.fillRect (plot.getX(), midY, plot.getWidth(), 1.0f);

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (Palette::textMuted);
        g.drawText ("L", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getY(), 14.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getBottom() - 28.0f, 14.0f, 14.0f), juce::Justification::centredLeft);

        // The dry hit.
        g.setColour (Palette::textPrimary.withAlpha (0.8f));
        g.fillRect (xFor (0.0), midY - half, 2.0f, half * 2.0f);

        const auto colour = Palette::fx;
        const auto drawTap = [&] (double t, double gain, bool left)
        {
            const auto h = hFor (gain);
            const auto x = xFor (t);
            g.setColour (colour.withAlpha ((float) juce::jlimit (0.35, 1.0, 0.35 + gain)));
            g.fillRoundedRectangle (left ? juce::Rectangle<float> (x - 1.5f, midY - h, 3.0f, h)
                                         : juce::Rectangle<float> (x - 1.5f, midY + 1.0f, 3.0f, h), 1.0f);
        };

        const auto wetGain = (double) dsp::wetGainFor (s.mix);
        for (int k = 1; k <= repeats; ++k)
        {
            const auto gain = wetGain * std::pow (fb, (double) (k - 1));
            if (s.mode == dsp::DelaySettings::pingPong)
                drawTap (dL * k, gain, (k % 2) == 1);
            else
            {
                drawTap (dL * k, gain, true);
                drawTap (dR * k, gain, false);
            }
        }

        static const char* modes[] { "Stereo", "Ping-Pong", "Dual L/R" };
        const auto timeText = [&] (int channel, double seconds)
        {
            auto t = formatMs (seconds * 1000.0);
            if (s.sync)
                t << " (" << dsp::noteValueName (channel == 1 && s.mode == dsp::DelaySettings::dual ? s.noteR : s.noteL) << ")";
            return t;
        };

        juce::String readout = juce::String (modes[juce::jlimit (0, 2, s.mode)]) + sep();
        if (s.mode == dsp::DelaySettings::dual)
            readout << "L " << timeText (0, dL) << "  R " << timeText (1, dR);
        else
            readout << timeText (0, dL);
        if (s.sync)
            readout << "  @ " << juce::String (shownBpm, 1) << " BPM";
        readout << sep() << "FB " << juce::roundToInt (fb * 100.0) << "%";
        if (s.duckDb > 0.05f)
            readout << sep() << "Duck " << juce::String (std::min (0.0f, shownDuckDb), 1) << " dB";
        drawReadout (g, area.reduced (0.0f, 6.0f), readout, Palette::textPrimary);

        if (! s.enabled)
            dimPlot (g, plot);
    }

    //==============================================================================
    namespace
    {
        const char* routingName (int index)
        {
            static const char* names[] { "Parallel", "Dly > Rev", "Rev > Dly" };
            return names[juce::jlimit (0, 2, index)];
        }
    }

    FxRoutingView::FxRoutingView (juce::AudioProcessorValueTreeState& s)
        : segments (s, "fx_routing", { routingName (0), routingName (1), routingName (2) }, Palette::fx)
    {
        setComponentID ("fx_routing");
        const juce::String tip ("Delay / reverb routing: Parallel (both from the vocal), Delay > Reverb (the reverb also "
                                "blurs the echoes) or Reverb > Delay (the echoes repeat the reverb)");
        setTooltip (tip);
        segments.setComponentID ("fx_routing_segments");
        segments.setTooltip (tip);
        segments.setTitle ("FX routing");
        addAndMakeVisible (segments);

        // Only to redraw the sketch; the switch owns the edits.
        if (auto* p = s.getParameter ("fx_routing"))
        {
            sketchAttachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
            {
                routing = juce::jlimit (0, 2, juce::roundToInt (v));
                repaint();
            }, nullptr);
            sketchAttachment->sendInitialUpdate();
        }
    }

    FxRoutingView::~FxRoutingView() = default;

    void FxRoutingView::resized()
    {
        segments.setBounds (getLocalBounds().withTrimmedTop (16).withHeight (24));
    }

    juce::Rectangle<int> FxRoutingView::getSegmentArea (int index) const
    {
        return segments.getSegmentBounds (juce::jlimit (0, 2, index)) + segments.getPosition();
    }

    void FxRoutingView::choose (int index)
    {
        segments.select (juce::jlimit (0, 2, index));
    }

    void FxRoutingView::paint (juce::Graphics& g)
    {
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Palette::textMuted);
        g.drawText ("Routing", getLocalBounds().removeFromTop (16), juce::Justification::centred);

        // Signal-flow sketch of the chosen routing.
        auto sketch = getLocalBounds().withTrimmedTop (44).toFloat().reduced (4.0f, 2.0f);
        const auto box = [&] (juce::Rectangle<float> r, const char* text)
        {
            g.setColour (Palette::panelRaised);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Palette::fx);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (Palette::textPrimary);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (Palette::textMuted);
        const auto cy = sketch.getCentreY();
        const auto x0 = sketch.getX(), x1 = sketch.getRight();
        const auto bw = 44.0f, bh = 15.0f;

        if (routing == 0)
        {
            const auto mid = sketch.getCentreX();
            const auto top = juce::Rectangle<float> (mid - bw * 0.5f, sketch.getY(), bw, bh);
            const auto bottom = juce::Rectangle<float> (mid - bw * 0.5f, sketch.getBottom() - bh, bw, bh);
            g.drawLine (x0, cy, x0 + 20.0f, cy, 1.0f);
            g.drawLine (x0 + 20.0f, top.getCentreY(), x0 + 20.0f, bottom.getCentreY(), 1.0f);
            g.drawLine (x0 + 20.0f, top.getCentreY(), top.getX(), top.getCentreY(), 1.0f);
            g.drawLine (x0 + 20.0f, bottom.getCentreY(), bottom.getX(), bottom.getCentreY(), 1.0f);
            g.drawLine (top.getRight(), top.getCentreY(), x1 - 20.0f, top.getCentreY(), 1.0f);
            g.drawLine (bottom.getRight(), bottom.getCentreY(), x1 - 20.0f, bottom.getCentreY(), 1.0f);
            g.drawLine (x1 - 20.0f, top.getCentreY(), x1 - 20.0f, bottom.getCentreY(), 1.0f);
            g.drawArrow ({ x1 - 20.0f, cy, x1, cy }, 1.0f, 5.0f, 5.0f);
            box (top, "Delay");
            box (bottom, "Reverb");
        }
        else
        {
            const auto first = routing == 1 ? "Delay" : "Reverb", second = routing == 1 ? "Reverb" : "Delay";
            const auto gap = (sketch.getWidth() - 2.0f * bw) / 3.0f;
            const auto a = juce::Rectangle<float> (x0 + gap, cy - bh * 0.5f, bw, bh);
            const auto b = juce::Rectangle<float> (a.getRight() + gap, cy - bh * 0.5f, bw, bh);
            g.drawArrow ({ x0, cy, a.getX(), cy }, 1.0f, 5.0f, 5.0f);
            g.drawArrow ({ a.getRight(), cy, b.getX(), cy }, 1.0f, 5.0f, 5.0f);
            g.drawArrow ({ b.getRight(), cy, x1, cy }, 1.0f, 5.0f, 5.0f);
            box (a, first);
            box (b, second);
        }
    }
}
