#include "ProcessingVisuals.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#include "../DSP/Effects.h"
#include "Theme.h"

namespace dooblrkick::ui
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;
        const juce::Colour driveColour { 0xffd9785b };

        float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
        float gainToDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

        juce::String formatDb (float db)
        {
            return db <= -119.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
        }

        void drawFrame (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::divider);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
    }

    //==============================================================================
    DriveDisplay::DriveDisplay (juce::AudioProcessorValueTreeState& state) : gesture (state)
    {
        setOpaque (true);
        rebuild();
    }

    uint64_t DriveDisplay::paramHash() const
    {
        uint64_t h = 1469598103934665603ull;
        for (const auto* id : { "dist_on", "dist_type", "dist_drive", "dist_bits", "dist_out", "dist_mix" })
        {
            const auto v = gesture.get (id);
            uint32_t bits = 0;
            std::memcpy (&bits, &v, sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
        return h;
    }

    float DriveDisplay::transfer (float x) const
    {
        if (gesture.get ("dist_on") < 0.5f)
            return x;

        const auto type = std::clamp (juce::roundToInt (gesture.get ("dist_type")), 0, numTypes - 1);
        const auto gain = dbToGain (gesture.get ("dist_drive"));
        const auto out = dbToGain (gesture.get ("dist_out"));
        const auto mix = std::clamp (gesture.get ("dist_mix"), 0.0f, 1.0f);
        float shaped;

        if (type == dsp::DistortionSettings::bitcrush)
        {
            const auto levels = std::pow (2.0f, std::clamp (gesture.get ("dist_bits"), 1.0f, 16.0f) - 1.0f);
            shaped = std::round (std::clamp (x * gain, -1.0f, 1.0f) * levels) / levels;
        }
        else
        {
            shaped = dsp::Distortion::shape (type, x, gain);
        }

        return x * (1.0f - mix) + shaped * out * mix;
    }

    float DriveDisplay::getHarmonicDb (int k) const
    {
        return k >= 1 && k <= numHarmonics ? harmonicDb[(size_t) (k - 1)] : -120.0f;
    }

    juce::Rectangle<float> DriveDisplay::getTypeArea (int type) const
    {
        const auto w = typeBar.getWidth() / (float) numTypes;
        return { typeBar.getX() + w * (float) type, typeBar.getY(), w, typeBar.getHeight() };
    }

    void DriveDisplay::rebuild()
    {
        lastHash = paramHash();
        curve.clear();
        before.clear();
        after.clear();

        if (! plot.isEmpty())
        {
            const auto steps = std::max (64, (int) plot.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const auto x = -1.0f + 2.0f * (float) i / (float) steps;
                const auto y = std::clamp (transfer (x), -1.05f, 1.05f);
                const juce::Point<float> pt { plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(),
                                              plot.getCentreY() - y * 0.5f * plot.getHeight() };
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
        }

        // One cycle of a 0 dBFS sine through the curve, and its harmonics (DFT of 512 points).
        constexpr int n = 512;
        std::array<float, n> cycle {};
        for (int i = 0; i < n; ++i)
            cycle[(size_t) i] = transfer (std::sin (2.0f * pi * (float) i / (float) n));

        for (int k = 1; k <= numHarmonics; ++k)
        {
            std::complex<double> acc {};
            for (int i = 0; i < n; ++i)
                acc += (double) cycle[(size_t) i] * std::polar (1.0, -2.0 * 3.14159265358979 * k * i / n);
            harmonicDb[(size_t) (k - 1)] = gainToDb ((float) (std::abs (acc) * 2.0 / n));
        }

        if (! wave.isEmpty())
        {
            const auto steps = std::max (64, (int) wave.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const auto phase = (float) i / (float) steps;
                const auto x = wave.getX() + phase * wave.getWidth();
                const auto in = std::sin (2.0f * pi * phase);
                const auto yIn = wave.getCentreY() - in * 0.45f * wave.getHeight();
                const auto yOut = wave.getCentreY() - std::clamp (cycle[(size_t) std::min (n - 1, (int) (phase * n))], -1.1f, 1.1f) * 0.45f * wave.getHeight();
                if (i == 0) { before.startNewSubPath (x, yIn); after.startNewSubPath (x, yOut); }
                else        { before.lineTo (x, yIn); after.lineTo (x, yOut); }
            }
        }
    }

    void DriveDisplay::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (6.0f);
        typeBar = area.removeFromTop (24.0f);
        area.removeFromTop (6.0f);
        readout = area.removeFromBottom (18.0f);

        const auto side = std::min (area.getHeight(), area.getWidth() * 0.5f);
        plot = area.removeFromLeft (side).withSizeKeepingCentre (side, side).reduced (4.0f);
        area.removeFromLeft (10.0f);
        wave = area.removeFromTop (area.getHeight() * 0.5f).reduced (2.0f, 4.0f);
        harmonics = area.reduced (2.0f, 4.0f);
        rebuild();
    }

    void DriveDisplay::reset()
    {
        shownInDb = -120.0f;
        framesReceived = 0;
        repaint();
    }

    void DriveDisplay::update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds)
    {
        auto batch = -120.0f;
        for (int i = 0; i < numFrames; ++i)
            batch = std::max (batch, frames[i].driveInDb);

        framesReceived += numFrames;
        const auto previous = shownInDb;
        shownInDb = std::max (batch, std::max (-120.0f, shownInDb - (float) (30.0 * elapsedSeconds)));

        if (paramHash() != lastHash)
        {
            rebuild();
            repaint();
        }
        else if (std::abs (previous - shownInDb) > 0.05f)
        {
            repaint (plot.expanded (8.0f).getSmallestIntegerContainer());
            repaint (readout.getSmallestIntegerContainer());
        }
    }

    void DriveDisplay::paint (juce::Graphics& g)
    {
        if (paramHash() != lastHash) // automation, undo or a preset changed the curve since the last update
            rebuild();

        g.fillAll (Palette::background);
        const auto on = gesture.get ("dist_on") > 0.5f;
        const auto type = juce::roundToInt (gesture.get ("dist_type"));
        const auto colour = on ? driveColour : Palette::textMuted;

        // Type selector.
        juce::StringArray names;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (gesture.param ("dist_type")))
            names = choice->choices;

        g.setFont (juce::FontOptions (12.0f));
        for (int t = 0; t < numTypes; ++t)
        {
            const auto r = getTypeArea (t).reduced (2.0f, 0.0f);
            g.setColour (t == type ? colour.withAlpha (0.85f) : Palette::panelRaised);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (t == type ? Palette::background : Palette::textPrimary);
            g.drawText (names[t], r, juce::Justification::centred);
        }

        // Transfer curve.
        drawFrame (g, plot);
        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
        g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
        const float dash[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ plot.getBottomLeft(), plot.getTopRight() }, dash, 2, 1.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (plot.getSmallestIntegerContainer());
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            if (shownInDb > -90.0f)
            {
                const auto x = std::clamp (dbToGain (shownInDb), 0.0f, 1.0f);
                const auto y = std::clamp (transfer (x), -1.05f, 1.05f);
                const juce::Point<float> dot { plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(), plot.getCentreY() - y * 0.5f * plot.getHeight() };
                g.setColour (Palette::textPrimary);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot));
            }
        }
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("in", plot.withTrimmedLeft (4).withTrimmedBottom (2), juce::Justification::bottomRight);
        g.drawText ("out", plot.reduced (4.0f, 2.0f), juce::Justification::topLeft);
        if (! on)
            g.drawText ("Drive off - switch it on from the Drive tab", plot.reduced (6.0f).withTrimmedTop (plot.getHeight() * 0.6f), juce::Justification::centred);

        // Before / after cycle.
        drawFrame (g, wave);
        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (wave.getSmallestIntegerContainer());
            juce::Path dashed;
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, before, dash, 2);
            g.setColour (Palette::textMuted);
            g.fillPath (dashed);
            g.setColour (colour);
            g.strokePath (after, juce::PathStrokeType (1.8f));
        }
        g.setColour (Palette::textMuted);
        g.drawText ("0 dBFS sine: before (dashed) / after", wave.reduced (6.0f, 3.0f), juce::Justification::topLeft);

        // Harmonics.
        drawFrame (g, harmonics);
        const auto inner = harmonics.reduced (8.0f, 6.0f).withTrimmedTop (14.0f).withTrimmedBottom (14.0f);
        const auto barW = inner.getWidth() / (float) numHarmonics;
        for (int k = 1; k <= numHarmonics; ++k)
        {
            const auto db = std::clamp (harmonicDb[(size_t) (k - 1)], -80.0f, 6.0f);
            const auto h = (db + 80.0f) / 86.0f * inner.getHeight();
            const auto x = inner.getX() + barW * (float) (k - 1);
            g.setColour (k == 1 ? Palette::textMuted : colour);
            g.fillRect (juce::Rectangle<float> (x + barW * 0.2f, inner.getBottom() - h, barW * 0.6f, h));
            g.setColour (Palette::textMuted);
            g.drawText (juce::String (k), juce::Rectangle<float> (x, inner.getBottom() + 1.0f, barW, 12.0f), juce::Justification::centred);
        }
        g.drawText ("harmonics (dBFS, -80..+6)", harmonics.reduced (6.0f, 3.0f), juce::Justification::topLeft);

        // Readout.
        auto thd = 0.0;
        for (int k = 2; k <= numHarmonics; ++k)
            thd += std::pow (10.0, harmonicDb[(size_t) (k - 1)] / 10.0);
        const auto fundamental = std::pow (10.0, harmonicDb[0] / 20.0);
        const auto thdPct = fundamental > 1.0e-6 ? 100.0 * std::sqrt (thd) / fundamental : 0.0;
        g.setColour (Palette::textPrimary);
        g.drawText ((on ? "Drive " + juce::String (gesture.get ("dist_drive"), 1) + " dB   Mix " + juce::String (juce::roundToInt (gesture.get ("dist_mix") * 100.0f)) + " %   THD "
                            + juce::String (thdPct, 1) + " %" : juce::String ("Drive off"))
                        + "   In " + formatDb (shownInDb),
                    readout, juce::Justification::centredLeft);
    }

    void DriveDisplay::mouseDown (const juce::MouseEvent& e)
    {
        for (int t = 0; t < numTypes; ++t)
            if (getTypeArea (t).contains (e.position))
            {
                gesture.begin ({ "dist_type" });
                gesture.set ("dist_type", (float) t);
                gesture.end();
                rebuild();
                repaint();
                return;
            }

        if (plot.contains (e.position))
        {
            dragging = true;
            dragDrive = gesture.get ("dist_drive");
            dragMix = gesture.get ("dist_mix");
            // Editing never switches Drive on: that is the Drive tab's power toggle.
            gesture.begin ({ "dist_drive", "dist_mix" });
        }
    }

    void DriveDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        const auto d = e.position - e.mouseDownPosition;
        gesture.set ("dist_drive", std::clamp (dragDrive - d.y / plot.getHeight() * 48.0f, 0.0f, 48.0f));
        gesture.set ("dist_mix", std::clamp (dragMix + d.x / plot.getWidth(), 0.0f, 1.0f));
        rebuild();
        repaint();
    }

    void DriveDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (dragging)
            gesture.end();
        dragging = false;
        rebuild();
        repaint();
    }

    void DriveDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (! plot.contains (e.position))
            return;

        // Double-click: back to the default drive (12 dB, full mix).
        gesture.begin ({ "dist_drive", "dist_mix" });
        gesture.set ("dist_drive", 12.0f);
        gesture.set ("dist_mix", 1.0f);
        gesture.end();
        rebuild();
        repaint();
    }

    //==============================================================================
    MixDisplay::MixDisplay (juce::AudioProcessorValueTreeState& state) : gesture (state)
    {
        setOpaque (true);
    }

    void MixDisplay::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (6.0f, 4.0f);
        meters = area.removeFromRight (std::min (150.0f, area.getWidth() * 0.3f));
        area.removeFromRight (8.0f);
        plot = area;
    }

    float MixDisplay::getMarkerX() const
    {
        const auto inner = plot.reduced (10.0f, 18.0f);
        return inner.getX() + std::clamp (gesture.get ("mix_drywet"), 0.0f, 1.0f) * inner.getWidth();
    }

    void MixDisplay::reset()
    {
        shown = { -120.0f, -120.0f, -120.0f };
        framesReceived = 0;
        repaint();
    }

    void MixDisplay::update (const dsp::MeterFrame* frames, int numFrames, double elapsedSeconds)
    {
        std::array<float, 3> batch { -120.0f, -120.0f, -120.0f };
        for (int i = 0; i < numFrames; ++i)
        {
            batch[0] = std::max (batch[0], frames[i].dryDb);
            batch[1] = std::max (batch[1], frames[i].wetDb);
            batch[2] = std::max (batch[2], frames[i].outputDb);
        }

        framesReceived += numFrames;
        auto changed = false;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto next = std::max (batch[i], std::max (-120.0f, shown[i] - (float) (20.0 * elapsedSeconds)));
            changed = changed || std::abs (next - shown[i]) > 0.05f;
            shown[i] = next;
        }

        if (changed)
            repaint (meters.expanded (2.0f).getSmallestIntegerContainer());

        if (std::abs (gesture.get ("mix_drywet") - lastMix) > 1.0e-5f)
            repaint();
    }

    void MixDisplay::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::background);
        const auto mix = std::clamp (gesture.get ("mix_drywet"), 0.0f, 1.0f);
        lastMix = mix;

        drawFrame (g, plot);
        const auto inner = plot.reduced (10.0f, 18.0f);
        auto toPoint = [&] (float m, float gain) { return juce::Point<float> (inner.getX() + m * inner.getWidth(), inner.getBottom() - gain * inner.getHeight()); };

        g.setColour (Palette::textMuted.withAlpha (0.8f));
        g.drawLine ({ toPoint (0, 1), toPoint (1, 0) }, 1.5f);
        g.setColour (Palette::master);
        g.drawLine ({ toPoint (0, 0), toPoint (1, 1) }, 2.0f);

        const auto x = getMarkerX();
        g.setColour (Palette::textPrimary.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, inner.getY() - 4.0f, inner.getBottom() + 4.0f);
        g.setColour (Palette::textMuted);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (toPoint (mix, 1.0f - mix)));
        g.setColour (Palette::master);
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (toPoint (mix, mix)));

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (Palette::textMuted);
        g.drawText ("DRY " + juce::String (juce::roundToInt ((1.0f - mix) * 100.0f)) + " %", plot.reduced (8.0f, 2.0f), juce::Justification::topLeft);
        g.setColour (Palette::master);
        g.drawText ("WET " + juce::String (juce::roundToInt (mix * 100.0f)) + " %", plot.reduced (8.0f, 2.0f), juce::Justification::topRight);
        g.setColour (Palette::textMuted);
        g.drawText ("equal-gain crossfade: dry = layers before the chain, wet = after", plot.reduced (8.0f, 2.0f), juce::Justification::bottomLeft);

        // Meters.
        drawFrame (g, meters);
        const char* labels[] = { "Dry", "Wet", "Out" };
        const auto m = meters.reduced (6.0f, 4.0f);
        const auto rowH = m.getHeight() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto row = juce::Rectangle<float> (m.getX(), m.getY() + rowH * (float) i, m.getWidth(), rowH).reduced (0.0f, 2.0f);
            const auto label = row.removeFromLeft (28.0f);
            const auto text = row.removeFromRight (56.0f);
            const auto bar = row.withSizeKeepingCentre (row.getWidth(), std::min (10.0f, row.getHeight()));
            g.setColour (Palette::panelRaised);
            g.fillRect (bar);
            const auto fraction = std::clamp ((shown[(size_t) i] + 60.0f) / 60.0f, 0.0f, 1.0f);
            g.setColour (i == 0 ? Palette::textMuted : i == 1 ? Palette::master : Palette::accent);
            g.fillRect (bar.withWidth (bar.getWidth() * fraction));
            g.setColour (Palette::textMuted);
            g.drawText (labels[i], label, juce::Justification::centredLeft);
            g.drawText (formatDb (shown[(size_t) i]), text, juce::Justification::centredRight);
        }
    }

    void MixDisplay::setFromX (float x)
    {
        const auto inner = plot.reduced (10.0f, 18.0f);
        gesture.set ("mix_drywet", std::clamp ((x - inner.getX()) / inner.getWidth(), 0.0f, 1.0f));
        repaint();
    }

    void MixDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (! plot.contains (e.position))
            return;
        gesture.begin ({ "mix_drywet" });
        setFromX (e.position.x);
    }

    void MixDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (gesture.isActive())
            setFromX (e.position.x);
    }

    void MixDisplay::mouseUp (const juce::MouseEvent&)
    {
        gesture.end();
    }

    void MixDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (! plot.contains (e.position))
            return;
        gesture.begin ({ "mix_drywet" });
        gesture.set ("mix_drywet", 1.0f);
        gesture.end();
        repaint();
    }
}
